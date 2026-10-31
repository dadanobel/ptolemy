// Block translator PowerPC -> ARM64 without an intermediate representation.
// PPC registers live in ARM64 registers, blocks are linked by direct branches,
// indirect branches are looked up in an inline jump cache; everything not implemented goes to the interpreter.
#define _GNU_SOURCE
#include "ppc.h"
#include "vmx_neon.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#ifdef __linux__
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#endif

enum { X_LINK = 1, X_LOOKUP, X_SC, X_INTERP, X_FAULT = JIT_FAULT, X_IRQ, X_MODE, X_FSF };   // X_MODE: mtmsr/mtsr, check the mode and interrupts; X_FSF: jit_fsf_fail
enum { ENV = 27, MEM = 28, T0 = 16, T1 = 17, T2 = 30, H_CTR = 13, H_LR = 14, H_CR0 = 29, ZR = 31 };
// r0-r12 and r31 are pinned statically (hot in any PPC code); the others use the block cache from POOL.
static const int8_t GMAP[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,26};
// x18 is the macOS platform register; it is free in the Linux stand and in the VM. A macOS user-mode host would need it out of the pool.
static int8_t POOL[16] = {15, 18, 19, 20, 21, 22, 23, 24, 25};
static int NPOOL = 9, zbase;          // zbase: guest address = host address (base 0), x28 is free -> into the pool
static int interp_batch = 1, jsys, vm_rfi, vm_sc, vm_msr; static uint32_t vm_vec;   // vm_vec: MSR.VEC and VRSAVE without an exit (PT_VMVEC=0: off)
                                                     // vm_*: rfi/sc/mtmsr directly in the VM (PT_VMRFI/PT_VMSC/PT_VMMSR=0: through the environment; =2: also on the
                                                     // bare-metal stand; to bisect faults: rfi =3 only into supervisor, =4 only into user; sc =3 from supervisor, =4 from user)
static int8_t cmap[32];            // block cache: guest -> host register, -1
static uint32_t cwr;               // cached guest registers the block writes
static int dry; static unsigned cnt[32];
#define OFF(f) ((uint32_t)offsetof(CPU, f))
#define OGPR(g) (OFF(gpr) + 4u * (g))
#define OCR(f) (OFF(cr) + 8u * (f))

// --- ARM64 encodings ---------------------------------------------------------
#define ADDw 0x0B000000u
#define SUBw 0x4B000000u
#define ADDSw 0x2B000000u
#define SUBSw 0x6B000000u
#define ADCSw 0x3A000000u
#define SBCSw 0x7A000000u
#define ANDw 0x0A000000u
#define BICw 0x0A200000u
#define ORRw 0x2A000000u
#define ORNw 0x2A200000u
#define EORw 0x4A000000u
#define EONw 0x4A200000u
#define MULw 0x1B007C00u
#define SDIVw 0x1AC00C00u
#define UDIVw 0x1AC00800u
#define LSLVx 0x9AC02000u
#define LSRVx 0x9AC02400u
#define RORVw 0x1AC02C00u
#define SMULL 0x9B207C00u
#define UMULL 0x9BA07C00u
#define ANDi 0x12000000u
#define ORRi 0x32000000u
#define EORi 0x52000000u
#define ANDSi 0x72000000u
#define LDRw_r 0xB8604800u
#define STRw_r 0xB8204800u
#define LDRB_r 0x38604800u
#define STRB_r 0x38204800u
#define LDRH_r 0x78604800u
#define STRH_r 0x78204800u
#define REVw 0x5AC00800u
#define REV16w 0x5AC00400u
enum { EQ = 0, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE };

static uint32_t *buf, *bufend, *cp, *exit_common, *code0;   // code0: blocks start here, after the trampolines (and settle routine)
static int (*enter)(CPU *, void *);
static uint32_t jmsr=MSR_750;
static unsigned gen;
typedef struct { uint64_t pc; uint64_t code; uint32_t gen, pad; uint64_t pad2; } JC;   // 32 bytes; gen: EA page generation (tlbie)
static JC jc_store[1 << 16], *jc = jc_store;
static uint16_t jc_used[1 << 16]; static uint32_t njc;
/* Two ways per set, cells 2k and 2k+1, the newest in way 0 (with one way, 92% of lookup exits found their cell holding
 * another target). The set is a multiplicative hash of pc ^ SR: the plain low bits put XNU's hottest low-memory
 * routines, 0x1xxxx, together with every kernel routine 128 KiB apart, and a shifted fold made new conflicting triples.
 * PT_JC_WAYS=1: one way. */
static int jc_ways = 2;
// VM mode (hvf.c): code is written to buf on the host and runs at buf+jit_delta inside the VM;
// a block exits with brk #0 and is entered through jit_vm_enter(). jit_zbase: guest addresses = executor addresses.
void *jit_vm_buf; size_t jit_vm_size; int64_t jit_delta; JC *jit_vm_jc; uint64_t jit_vm_jc_va;
/* VM: jit_pggen, the EA page generations (tlbie bumps them) at ENV + jit_pggen_off; a jump-cache cell is valid while its
 * generation matches. jit_mapped_off: the environment's bitmap of pages an ARM table may hold (hvf.c). */
uint32_t *jit_pggen; uint32_t jit_pggen_off, jit_mapped_off;
int (*jit_vm_enter)(CPU *, void *); uint32_t *jit_tramp, *jit_tlbi_eret; int jit_zbase = -1;   /* jit_tlbi_eret: hvf.c resumes a faulted access through it */
#define HT (1u << 20)
static struct { uint32_t pc, pg, epoch; uint32_t *code; } ht[HT];   // key: pc | PR<<1 | IR and the code's physical page
static uint64_t st_blocks, st_exit[8], st_so, st_so_ins;   // st_so*: XER.SO=1 episodes and the instructions they sent to the interpreter
// Profiler (PT_PROF=1, Linux stand): host PC sampled on SIGPROF -> block -> PPC address.
#define MAXB (1 << 18)
static struct { uint32_t *code, *end; uint32_t pc, n; uint64_t hits; uint32_t im; int8_t cmap[32];
                uint32_t key, ppg; int32_t ppnext; uint32_t *dead; uint8_t isdead; } blk[MAXB];
// ppnext: the next block of the same physical page ppg; dead: the exit that replaces a dropped block
static int nblk; static uint64_t prof_other, prof_total;
// Precise access faults: for each PPC instruction of a block, the offset of its ARM code from the block start (imap[blk.im + i]).
#define NIM (1 << 22)
static uint16_t imap[NIM]; static uint32_t nim;
static uint32_t kir, kpr;             // key flags: bit 0 = MSR.IR, bit 1 = MSR.PR; with IR=1 jump-cache keys also hold the segment's SR value
#define KF (kpr << 1 | kir)
void (*jit_dsr)(CPU *c); void (*jit_dtlbie)(CPU *c, const uint32_t *ea, int n);
static int32_t pp_head[1 << 20]; static uint32_t pp_gen[1 << 20];   // physical page -> blocks (chained by ppnext)
static int32_t dead_ix[1 << 16];   // key -> last dropped block + 1: a new translation of the same key takes over its entry
static uint64_t st_srsw, st_tlbie, st_cflush, st_pkill, st_xflush;
static struct { uint32_t pc1; uint32_t n; } hot[1024]; static int nhot;   // firmware (pc >= 0xFFF00000) with frequent device accesses:
// after 256 VM exits at one instruction the interpreter runs it (an access is then a call, not a VM exit): OpenBIOS reads ATA word by word
static int is_hot(uint32_t pc) {
    for (uint32_t i = pc >> 2 & 1023, k = 0; k < 8; k++, i = (i + 1) & 1023) { if (hot[i].pc1 == pc + 1) return hot[i].n >= 256; if (!hot[i].pc1) return 0; }
    return 0;
}
static uint8_t untr[64 * 1024], untr_spr[2][1024];   // op/xo the JIT does not translate (learned while translating), + mfspr/mtspr by SPR number: interpreter batches continue through them
void (*jit_dmap)(CPU *c, int flush); void *jit_fault_exit;
_Static_assert(offsetof(CPU, irq) < 16384, "c->irq uses ldr w with a 12-bit offset");
#ifdef __linux__
static void on_prof(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_; uint32_t *pc = (uint32_t *)uc->uc_mcontext.pc; prof_total++;
    int lo = 0, hi = nblk - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (pc < blk[m].code) hi = m - 1; else if (pc >= blk[m].end) lo = m + 1; else { blk[m].hits++; return; } }
    prof_other++;
}
#endif
static int byhits(const void *a, const void *b) { uint64_t x = ((const typeof(blk[0]) *)a)->hits, y = ((const typeof(blk[0]) *)b)->hits; return x < y ? 1 : x > y ? -1 : 0; }
static void prof_report(CPU *c) {
    fprintf(stderr, "profile: %llu samples, outside JIT code %.1f%%\n", (unsigned long long)prof_total, 100.0 * prof_other / (prof_total ? prof_total : 1));
    FILE *f = fopen("prof.bin", "wb");
    qsort(blk, nblk, sizeof blk[0], byhits);
    for (int i = 0; i < nblk && i < 400 && blk[i].hits; i++) {
        if (i < 25) fprintf(stderr, "  %5.1f%%  pc %08x  ppc %3u  arm %3u\n", 100.0 * blk[i].hits / prof_total, blk[i].pc, blk[i].n, (unsigned)(blk[i].end - blk[i].code));
        uint32_t hdr[4] = {blk[i].pc, blk[i].n, (uint32_t)(blk[i].end - blk[i].code), (uint32_t)blk[i].hits};
        if (f) { fwrite(hdr, 4, 4, f); fwrite(c->mem + blk[i].pc, 4, blk[i].n, f); fwrite(blk[i].code, 4, hdr[2], f); }
    }
    if (f) fclose(f);
}
static int maxblk = 128, check, nolink, nojc;                // check: compare every instruction with the interpreter
static int chk_arm; static uint32_t chk_lo, chk_hi;          // PT_CHECK=lo-hi: the comparison starts when pc first enters [lo, hi)
unsigned long jit_mmio_n;                                     // JIT-code device accesses (counted by the environment)

static inline void E(uint32_t x) { *cp++ = x; }
static void ldrw(int t, int n, uint32_t off) { E(0xB9400000 | (off / 4) << 10 | n << 5 | t); }
static void strw(int t, int n, uint32_t off) { E(0xB9000000 | (off / 4) << 10 | n << 5 | t); }
static void ldrx(int t, int n, uint32_t off) { E(0xF9400000 | (off / 8) << 10 | n << 5 | t); }
static void strx(int t, int n, uint32_t off) { E(0xF9000000 | (off / 8) << 10 | n << 5 | t); }
static void mov32(int d, int m) { if (d != m) E(0x2A0003E0 | m << 16 | d); }
static void op3(uint32_t op, int d, int n, int m) { E(op | m << 16 | n << 5 | d); }
static void cset(int d, int cond) { E(0x1A9F07E0 | (uint32_t)(cond ^ 1) << 12 | d); }
static void ubfm(int d, int n, int r, int s) { E(0x53000000 | r << 16 | s << 10 | n << 5 | d); }
static void sbfm(int d, int n, int r, int s) { E(0x13000000 | r << 16 | s << 10 | n << 5 | d); }
static void bfm(int d, int n, int r, int s)  { E(0x33000000 | r << 16 | s << 10 | n << 5 | d); }
static void rorw(int d, int n, int s) { if (s & 31) E(0x13800000 | n << 16 | (s & 31) << 10 | n << 5 | d); else mov32(d, n); }
static void sxtw(int d, int n) { E(0x93407C00 | n << 5 | d); }
static void b_at(uint32_t *at, const uint32_t *to) { *at = 0x14000000 | ((uint32_t)(to - at) & 0x3ffffff); }
static void b_to(const uint32_t *to) { E(0); b_at(cp - 1, to); }
static void fix(uint32_t *at, const uint32_t *to) {           // patch the offset of a conditional branch
    uint32_t d = (uint32_t)(to - at);
    int32_t s = (int32_t)d, tb = (*at & 0x7E000000u) == 0x36000000u;    /* never wrap silently into other code */
    if (tb ? (s < -8192 || s > 8191) : (s < -262144 || s > 262143)) { fprintf(stderr, "ptolemy: JIT branch %08x out of range (%d words)\n", *at, s); abort(); }
    if ((*at & 0x7E000000u) == 0x36000000u) *at = (*at & ~(0x3fffu << 5)) | (d & 0x3fff) << 5;   // tbz/tbnz
    else *at = (*at & ~(0x7ffffu << 5)) | (d & 0x7ffff) << 5;                                       // b.cond/cbz/cbnz
}
static void movw(int d, uint32_t v) {
    if (!(v >> 16)) E(0x52800000 | (v & 0xffff) << 5 | d);
    else if (!(v & 0xffff)) E(0x52A00000 | (v >> 16) << 5 | d);
    else if (!(~v >> 16)) E(0x12800000 | (~v & 0xffff) << 5 | d);
    else { E(0x52800000 | (v & 0xffff) << 5 | d); E(0x72A00000 | (v >> 16) << 5 | d); }
}
static int bm32(uint32_t m, uint32_t *f) {                    // ARM64 logical immediate (a rotated run of ones)
    if (m == 0 || m == ~0u) return 0;
    for (int r = 0; r < 32; r++) {
        uint32_t x = r ? m << r | m >> (32 - r) : m;
        if (!(x & (x + 1))) { *f = (uint32_t)r << 16 | (uint32_t)(__builtin_popcount(x) - 1) << 10; return 1; }
    }
    return 0;
}
static void logi(uint32_t opi, uint32_t opr, int d, int n, uint32_t imm) {   // n != T1
    uint32_t f;
    if (bm32(imm, &f)) E(opi | f | n << 5 | d); else { movw(T1, imm); op3(opr, d, n, T1); }
}
static void addi32(int d, int n, int32_t v) {                  // n != T1
    uint32_t a = v < 0 ? 0u - (uint32_t)v : (uint32_t)v, op = v < 0 ? 0x51000000u : 0x11000000u;
    if (!v) mov32(d, n);
    else if (a < 4096) E(op | a << 10 | n << 5 | d);
    else if (!(a & 0xfff) && a < 1u << 24) E(op | 1u << 22 | (a >> 12) << 10 | n << 5 | d);
    else { movw(T1, (uint32_t)v); op3(ADDw, d, n, T1); }
}

// --- guest registers ---------------------------------------------------------
static int hreg(int g) { if (dry && GMAP[g] < 0) cnt[g]++; return GMAP[g] >= 0 ? GMAP[g] : cmap[g]; }
static int src(int g, int tmp) { int h = hreg(g); if (h >= 0) return h; ldrw(tmp, ENV, OGPR(g)); return tmp; }
static int dst(int g) { int h = hreg(g); if (h >= 0 && GMAP[g] < 0) cwr |= 1u << g; return h >= 0 ? h : T0; }
static void put(int g, int h) { int r = GMAP[g] >= 0 ? GMAP[g] : cmap[g]; if (r < 0) strw(h, ENV, OGPR(g)); else { mov32(r, h); if (GMAP[g] < 0) cwr |= 1u << g; } }
static void flush_mask(uint32_t m) { for (int g = 0; g < 32; g++) if (cmap[g] >= 0 && m >> g & 1) strw(cmap[g], ENV, OGPR(g)); }
static void flush_cache_regs(void) { flush_mask(cwr); }
static int kill_cr0;                                            // the next instruction overwrites cr0
static void rec(int h) { if (!kill_cr0) sxtw(H_CR0, h); }                              // Rc=1: cr0 = sext(result)
static void set_ca(void) { cset(T2, CS); strw(T2, ENV, OFF(ca)); }                     // CA = the ARM carry flag
static void load_guest(void) {
    for (int g = 0; g < 32; g++) if (GMAP[g] >= 0) ldrw(GMAP[g], ENV, OGPR(g));
    ldrw(H_CTR, ENV, OFF(ctr)); ldrw(H_LR, ENV, OFF(lr)); ldrx(H_CR0, ENV, OCR(0));
}
static void store_guest(void) {
    for (int g = 0; g < 32; g++) if (GMAP[g] >= 0) strw(GMAP[g], ENV, OGPR(g));
    strw(H_CTR, ENV, OFF(ctr)); strw(H_LR, ENV, OFF(lr)); strx(H_CR0, ENV, OCR(0));
}

// --- block table --------------------------------------------------------------
// A block's code is a function of the bytes of its physical page, its EA (links within the page) and the mode, so the
// key is pc | mode plus the physical page: processes with the same code (dyld, a program after fork) share translations.
// Direct links stay within the page (in any address space the whole page maps to the same physical page); between
// pages the jump cache is used (key: the segment's SR and pc), and its cells are checked against the EA page generation
// that tlbie bumps.
static uint32_t hix(uint32_t pc, uint32_t pg) { return (pc >> 2 ^ pg * 0x9E3779B1u >> 8) & (HT - 1); }
static uint32_t *lookup(uint32_t pc, uint32_t pg) {
    for (uint32_t i = hix(pc, pg); ht[i].epoch == gen; i = (i + 1) & (HT - 1)) if (ht[i].pc == pc && ht[i].pg == pg) return ht[i].code;
    return NULL;
}
static void insert(uint32_t pc, uint32_t pg, uint32_t *code) {
    uint32_t i = hix(pc, pg);
    while (ht[i].epoch == gen) i = (i + 1) & (HT - 1);
    ht[i].pc = pc; ht[i].pg = pg; ht[i].code = code; ht[i].epoch = gen;
}
static void jc_reset(void) { for (uint32_t i = 0; i < njc; i++) for (int w = 0; w < jc_ways; w++) jc[jc_used[i] + w] = (JC){~0ull, 0, 0, 0, 0}; njc = 0; }   // without the VM: a tlbie resets the whole jump cache
static void flush_all(void) {
    nblk = 0; nim = 0;
    // Full invalidation without clearing 17 MiB on every SR change/tlbie.
    if (!++gen) { memset(ht, 0, sizeof ht); memset(pp_gen, 0, sizeof pp_gen); gen = 1; }
    memset(dead_ix, 0, sizeof dead_ix);             // so a generation wrap cannot revive old blocks
    jc_reset();
    cp = code0 ? code0 : exit_common + 64;                      // the trampolines (and the settle routine) stay at the buffer start
    st_cflush++;
}

// --- block exits --------------------------------------------------------------
static struct { uint32_t *site; uint32_t target; } stub[512];
static int nstub;
static uint32_t blk_pc0, blk_pg; static uint32_t *blk_start, *loop_start;
static struct { uint32_t *br; uint32_t target; int8_t mf, msign, ma, mb; int32_t mimm; } side[128];   // side exits; mf>=0: compute the CR field at the exit
static int nside;
static void exit_ind(void);
_Static_assert(offsetof(CPU, tlbi_va) + 8 * 256 <= 32760 && offsetof(CPU, tlbi_n) < 16384, "tlbi fields need scaled 12-bit offsets");
static void tlbi_run(void) {                    // x16, x30: hvf.c's queued invalidations, by VA for every ASID; then none queued
    ldrw(T0, ENV, OFF(tlbi_n)); uint32_t *z = cp; E(0x34000000 | T0); E(0xD5033A9F);                     // cbz w16; dsb ishst
    uint32_t *loop = cp; E(0x8B000000 | T0 << 16 | 3u << 10 | ENV << 5 | T2); ldrx(T2, T2, OFF(tlbi_va) - 8);   // x30 = va[n - 1]
    E(0xD5088360 | T2); E(0x51000400 | T0 << 5 | T0); E(0x35000000 | ((uint32_t)(loop - cp) & 0x7ffff) << 5 | T0);   // tlbi vaae1is; n--; cbnz
    E(0xD5033B9F); E(0xD5033FDF); strw(ZR, ENV, OFF(tlbi_n)); fix(z, cp);                                  // dsb ish; isb
}
/* x16: SR << 32 | pc. x17 = the set's first cell: (pc ^ SR) * 0x9E3779B1, its top 15 bits (16 with one way), as jc_set(). */
static void jc_index(void) {
    E(0xCA400000 | T0 << 16 | 32u << 10 | T0 << 5 | T1); movw(T2, 0x9E3779B1u); E(0x1B007C00 | T2 << 16 | T1 << 5 | T1);   // eor; mul w17, w17, w30
    E(0x53007C00 | (jc_ways == 2 ? 17u : 16u) << 16 | T1 << 5 | T1); ldrx(T2, ENV, OFF(jit_jc));                       // lsr w17, #17
    E(0x8B000000 | T1 << 16 | (jc_ways == 2 ? 6u : 5u) << 10 | T2 << 5 | T1);                                           // add x17, x30, x17, lsl #6
}
static void jc_tail(void) {                     // x17: the jump-cache cell, key already compared (flags); w16: pc. Check the page generation, then jump
    uint32_t *m0 = NULL, *m1, *m2 = NULL;
    if (jc_ways == 2) { m0 = cp; E(0x54000000 | EQ); E(0xF8420C00 | T1 << 5 | T2); E(0xEB00001F | T0 << 16 | T2 << 5); }   // way 1: ldr x30, [x17, #32]!; cmp x30, x16
    m1 = cp; E(0x54000000 | NE);
    if (m0) fix(m0, cp);
    if (jit_pggen) {                                                            // VM: the generation table is at ENV + jit_pggen_off
        E(0xD3400000 | 12u << 16 | 27u << 10 | T0 << 5 | T0);                    // ubfx x16, x16, #12, #16: page within the segment (tlbie ignores the segment)
        E(0x8B000000 | T0 << 16 | 2u << 10 | ENV << 5 | T0);                    // add x16, x27, x16, lsl #2
        E(0x91400000 | (jit_pggen_off >> 12) << 10 | T0 << 5 | T0);             // add x16, x16, #off, lsl #12
        ldrw(T0, T0, 0); ldrw(T2, T1, 16); E(0x6B00001F | T2 << 16 | T0 << 5); m2 = cp; E(0x54000000 | NE);
    }
    ldrx(T1, T1, 8); E(0xD61F0000 | T1 << 5);                                    // br x17
    fix(m1, cp); if (m2) fix(m2, cp); movw(T1, X_LOOKUP); strw(T1, ENV, OFF(jit_exit)); b_to(exit_common);
}
static void exit_to(uint32_t t) {                               // direct, linkable
    if (t == blk_pc0 && !check && !nolink) { b_to(loop_start); return; }   // self-loop: cached registers stay in registers
    if ((t ^ blk_pc0) >> 12 && !check && !nolink) { movw(T0, t); exit_ind(); return; }   // to another page: through the jump cache
    flush_cache_regs();
    uint32_t *code = check || nolink || (t ^ blk_pc0) >> 12 ? NULL : lookup(t | KF, blk_pg);   // links only within the page
    if (code) { b_to(code); return; }
    stub[nstub].site = cp; stub[nstub++].target = t; E(0x14000000);
}
static void exit_why(uint32_t pc, int why) {
    flush_cache_regs();
    movw(T0, pc); strw(T0, ENV, OFF(pc)); movw(T1, why); strw(T1, ENV, OFF(jit_exit)); b_to(exit_common);
}
static void exit_ind(void) {                                    // target in w16 (aligned)
    flush_cache_regs();
    strw(T0, ENV, OFF(pc));
    if (kir) {                                                  // cache key: the target segment's SR in the upper 32 bits
        ubfm(T2, T0, 28, 31); E(0x8B000000 | T2 << 16 | 2u << 10 | ENV << 5 | T2);    // x30 = ENV + 4 * (pc >> 28)
        ldrw(T2, T2, OFF(sr)); E(0xAA000000 | T2 << 16 | 32u << 10 | T0 << 5 | T0);   // orr x16, x16, x30, lsl #32
    }
    jc_index(); ldrx(T2, T1, 0);
    if (KF) E(KF == 1 ? 0xB2400210 : KF == 2 ? 0xB27F0210 : 0xB2400610);   // orr x16, x16, #KF
    E(0xEB00001F | T0 << 16 | T2 << 5);                         // cmp x30, x16
    jc_tail();
}
static void crbit_to(int d, int bi) {           // CR bit bi -> wd (0/1); clobbers x16 if the field is not 0
    int f = bi >> 2, h = H_CR0;
    if (f) { ldrx(T0, ENV, OCR(f)); h = T0; }
    switch (bi & 3) {
    case 0: E(0xD37EF800 | h << 5 | d); break;                                                    // LT: bit 62
    case 1: E(0xF100001F | h << 5); cset(d, GT); break;                                           // GT: V > 0
    case 2: E(0x7100001F | h << 5); cset(d, EQ); break;                                           // EQ: (uint32)V == 0
    default: E(0xCA000000 | h << 16 | 2u << 10 | h << 5 | d); E(0xD37FFC00 | d << 5 | d); break;  // SO: bit 61 ^ bit 63
    }
}
static void jump_dyn(void) {                 // jump to w16 with the MSR in w17 (the mode may have changed): the mode's TTBR0 and the cache key follow the MSR
    strw(T0, ENV, OFF(pc));                  // call after flush_cache_regs(): x15, x18, x19 (cache pool) are free here
    ubfm(15, T1, 4, 4); ubfm(18, T1, 14, 14); op3(ANDw, 18, 18, 15); op3(ADDw, 15, 15, 18);     // k = DR ? 1 + PR : 0
    strw(15, ENV, OFF(ttbr_k));
    E(0x8B000000 | 15u << 16 | 3u << 10 | ENV << 5 | 15); ldrx(15, 15, OFF(ttbr)); E(0xD5182000 | 15); E(0xD5033FDF);   // msr ttbr0_el1; isb
    ubfm(18, T1, 5, 5); ubfm(19, T1, 14, 14); E(ORRw | 19u << 16 | 1u << 10 | 18 << 5 | 19);   // w19 = PR << 1 | IR
    uint32_t *b = cp; E(0x34000000 | 18);                                                       // IR=0: no SR in the key
    ubfm(15, T0, 28, 31); E(0x8B000000 | 15u << 16 | 2u << 10 | ENV << 5 | 15); ldrw(15, 15, OFF(sr));
    E(0xAA000000 | 15u << 16 | 32u << 10 | T0 << 5 | T0); fix(b, cp);
    jc_index(); ldrx(T2, T1, 0);
    E(0xAA000000 | 19u << 16 | T0 << 5 | T0);                                                  // | flags
    E(0xEB00001F | T0 << 16 | T2 << 5); jc_tail();
}
static uint32_t *br_crbit(int bi, int want) {                   // branch if CR bit == want; fix() patches the offset later
    int f = bi >> 2, h = H_CR0;
    if (f) { ldrx(T0, ENV, OCR(f)); h = T0; }
    switch (bi & 3) {
    case 0: E(0xF242001F | h << 5); E(0x54000000 | (want ? NE : EQ)); break;                 // tst x, #1<<62 (LT); b.ne/eq
    case 1: E(0xF100001F | h << 5); E(0x54000000 | (want ? GT : LE)); break;                 // cmp x,#0; b.gt/le
    case 2: E((want ? 0x34000000u : 0x35000000u) | h); break;                                 // cb(n)z w
    default: E(0xCA000000 | h << 16 | 2u << 10 | h << 5 | T1);                                // eor x17, x, x, lsl #2
             E(0xF241001F | T1 << 5); E(0x54000000 | (want ? NE : EQ)); break;               // tst x17, #1<<63 (SO); b.ne/eq
    }
    return cp - 1;   /* b.cond reaches +-1 MiB; tbz/tbnz reach only +-32 KiB, and a side exit past a large FP block
                      * wrapped into unrelated code (Doom 3: bge cr7 at 0x1727d0 landed in another block's stfs, DAR 0x1). */
}

// --- addresses and memory ----------------------------------------------------------------
static int ea_d(int a, int32_t d) {
    if (!a) { movw(T0, (uint32_t)d); return T0; }
    int ra = src(a, T0);
    if (!d) return ra;
    addi32(T0, ra, d); return T0;
}
static int ea_x(int a, int b) {
    if (!a) return src(b, T0);
    int ra = src(a, T0), rb = src(b, T1);
    op3(ADDw, T0, ra, rb); return T0;
}
static void ldst(uint32_t op, int t, int ea) { E(op | ea << 16 | MEM << 5 | t); }
// Address: zbase -> [x<base>, #off]; otherwise base = a register holding the 32-bit EA, access [x28, w<base>, uxtw].
typedef struct { int base; int32_t off; } Addr;
// d >= 0 only: [x<rA>, #-d] with rA < d is a 64-bit VA below 0, which no alias maps (dfault: exit). Negative d goes
// through ea_d (32-bit wrap, as the ISA). Cost: one add per negative-offset access; a TTBR1 alias of the top 4 GiB
// would avoid it.
static int fits(int32_t d, int sz) { return d >= 0 && ((d % sz == 0 && d / sz < 4096) || d < 256); }
static Addr addr_d(int a, int32_t d, int sz, int need_ea) {
    if (zbase && a && !need_ea && fits(d, sz)) return (Addr){src(a, T0), d};
    return (Addr){ea_d(a, d), 0};
}
// size: 4/2/1; load: 1 = load; U: base LDR/STR (unsigned imm) opcodes per size
static void mem(int load, int sz, int t, Addr ad) {
    static const uint32_t U[3][2] = {{0x39000000, 0x39400000}, {0x79000000, 0x79400000}, {0xB9000000, 0xB9400000}};   // str/ldr imm
    static const uint32_t R[3][2] = {{STRB_r, LDRB_r}, {STRH_r, LDRH_r}, {STRw_r, LDRw_r}};
    int k = sz == 1 ? 0 : sz == 2 ? 1 : 2;
    if (!zbase) { ldst(R[k][load], t, ad.base); return; }
    int32_t d = ad.off;
    if (d >= 0 && d % sz == 0 && d / sz < 4096) E(U[k][load] | (uint32_t)(d / sz) << 10 | ad.base << 5 | t);
    else E((U[k][load] & ~0x01000000u) | ((uint32_t)d & 0x1ff) << 12 | ad.base << 5 | t);       // ldur/stur
}
static void load(int kind, int d, Addr ad) {                    // kind: 0 w, 1 b, 2 h, 3 ha, 4 w without byte swap, 5 h without
    int hr = hreg(d), h = hr >= 0 ? hr : T1;
    if (hr >= 0 && GMAP[d] < 0) cwr |= 1u << d;
    switch (kind) {
    case 0: mem(1, 4, h, ad); E(REVw | h << 5 | h); break;
    case 1: mem(1, 1, h, ad); break;
    case 2: mem(1, 2, h, ad); E(REV16w | h << 5 | h); break;
    case 3: mem(1, 2, h, ad); E(REV16w | h << 5 | h); sbfm(h, h, 0, 15); break;
    case 4: mem(1, 4, h, ad); break;
    case 5: mem(1, 2, h, ad); break;
    }
    if (hr < 0) strw(T1, ENV, OGPR(d));
}
static void store(int kind, int s, Addr ad) {
    int h = src(s, T1);
    switch (kind) {
    case 0: E(REVw | h << 5 | T1); mem(0, 4, T1, ad); break;
    case 1: mem(0, 1, h, ad); break;
    case 2: E(REV16w | h << 5 | T1); mem(0, 2, T1, ad); break;
    case 4: mem(0, 4, h, ad); break;
    case 5: mem(0, 2, h, ad); break;
    }
}
static void upd(int a, int e) { if (e == T0) put(a, T0); }       // update form: rA = EA

// --- light CR data-flow analysis within a block ---
static const uint8_t *gm; static int cur_n, skip_next;
static int writes_field(uint32_t in, int f) {
    int op = in >> 26, D = in >> 21 & 31, rc = in & 1, xo = in >> 1 & 0x3ff, xo9 = xo & 0x1ff;
    if (op == 10 || op == 11) return !(D & 1) && D >> 2 == f;
    if (op == 31 && (xo == 0 || xo == 32)) return !(D & 1) && D >> 2 == f;
    if (f) return 0;
    if (op == 13 || op == 28 || op == 29) return 1;
    if ((op == 20 || op == 21 || op == 23) && rc) return 1;
    if (op == 31 && rc) {
        switch (xo9) { case 266: case 40: case 104: case 235: case 75: case 11: case 491: case 459: case 10: case 8:
                       case 138: case 136: case 202: case 200: case 234: case 232: return !(in >> 10 & 1); }
        switch (xo) { case 28: case 60: case 444: case 412: case 316: case 476: case 124: case 284: case 24: case 536:
                      case 954: case 922: case 26: case 824: return 1; }
    }
    return 0;
}
static uint32_t pg_end;                                         // end of the current block's PPC page (4 KB)
static int has_next(uint32_t pc, int k) { return pc + 4u * k > pc && pc + 4u * k < pg_end; }
static uint32_t nexti(uint32_t pc, int k) { return be32(gm + pc + 4 * k); }   // only after has_next()
static int next_kills(uint32_t pc, int f) { return !check && cur_n + 1 < maxblk && has_next(pc, 1) && writes_field(nexti(pc, 1), f); }
static void subimm64(int v, int a, int64_t imm, int sign) {         // xv = xa - imm
    if (!imm) { if (v != a) E(0xAA0003E0 | a << 16 | v); }
    else if (imm > 0 && imm < 4096) E(0xD1000000 | (uint32_t)imm << 10 | a << 5 | v);
    else if (imm < 0 && imm > -4096) E(0x91000000 | (uint32_t)-imm << 10 | a << 5 | v);
    else { movw(T1, (uint32_t)imm); E((sign ? 0xCB20C000u : 0xCB000000u) | T1 << 16 | a << 5 | v); }
}
// A compare directly followed by bc on the same field, then an instruction that overwrites the field: ARM flags, the field only at the exit.
static int try_fuse(int f, int sign, int a, int b, int32_t imm, uint32_t pc) {
    if (check || nolink || nside == 128 || cur_n + 2 >= maxblk || !has_next(pc, 2)) return 0;
    uint32_t bc = nexti(pc, 1); int bo = bc >> 21 & 31, bi = bc >> 16 & 31;
    if (bc >> 26 != 16 || (bc & 3) || !(bo & 4) || (bo & 16) || bi >> 2 != f || (bi & 3) == 3) return 0;
    if (!writes_field(nexti(pc, 2), f)) return 0;
    uint32_t t = pc + 4 + (uint32_t)(int32_t)(int16_t)(bc & 0xfffc);
    if (t == blk_pc0) return 0;
    if (b >= 0) E(0x6B00001F | b << 16 | a << 5);
    else if (imm >= 0 && imm < 4096) E(0x7100001F | (uint32_t)imm << 10 | a << 5);
    else if (imm < 0 && imm > -4096) E(0x3100001F | (uint32_t)-imm << 10 | a << 5);
    else { movw(T1, (uint32_t)imm); E(0x6B00001F | T1 << 16 | a << 5); }
    int want = bo >> 3 & 1, k = bi & 3, cond;
    if (k == 2) cond = want ? EQ : NE;
    else if (k == 0) cond = sign ? (want ? LT : GE) : (want ? CC : CS);
    else cond = sign ? (want ? GT : LE) : (want ? HI : LS);
    E(0x54000000 | cond);
    side[nside].br = cp - 1; side[nside].target = t;
    side[nside].mf = f; side[nside].msign = sign; side[nside].ma = a; side[nside].mb = b; side[nside++].mimm = imm;
    skip_next = 1;
    return 1;
}

// --- translating one instruction: 0 = continue, 1 = block ended, -1 = to the interpreter ---
uint32_t ppc_mask(int mb, int me);
int ppc_store_window(const CPU *c, uint32_t in, uint32_t *ea);
/* v25-v31 are caller-saved and disjoint from scalar/VMX scratch v0-v24.
 * Write through: CPU.fpr stays precise at every DSI, IRQ and interpreter exit.
 * Each block establishes its own values (loaded in its prologue, kept across the self-loop); no cache state crosses a
 * link. */
static int fcache_enabled, fcache_active, fp_hoist_enabled, fp_round_fast, cr_fuse;
static int fp_lazy_enabled, fp_lazy_active;
static int fp_hoist, fp_checked, fp_guarded;
static int8_t fmap[32];
static unsigned fcnt[32];
static uint32_t fvalid, fpro;                  // fpro: cache registers loaded by the block prologue
static uint32_t fpr_at = OFF(fpr);             // FPR file; the settle routine emits over CPU.fs_x instead
static int owe_gen;                            // emitting the settle routine: slow paths return failure to its caller
static uint32_t *settle_fn, *settle_fail;   // the settle routine (settle_gen) and its failure return
/* FPRs the block caches in single format (fsf): every access its own code makes is single precision (lfs, stfs, single
 * arithmetic), so the cache register holds the single and the value stays single: no conversion on a dependency chain
 * (Dot Product's sum: fcvt, fmadd, fcvt, 10 cycles a step), no input check, stfs stores the bits. The FPR file stays
 * double (write-through), and out-of-line and eager code reads it there. A value the block reads before writing it is
 * checked in the prologue: not exactly single -> the interpreter for that instruction. PT_FP_SGL=0: off. */
static uint32_t fsf, fdbl, fwr, frbw, fsw;   // single format; dry pass: accessed as double, written, read before written, written as a single
static uint32_t nofsf[1024]; static uint8_t nofsf_n[1024];   // pc + 1 of blocks whose entry value was not a single, how often (jit_fsf_fail)
static uint64_t st_fsf_blocks;          // translations with a single-format FPR (fixtures)
static int fsgl, fool, fsf_enabled;     // the access being emitted is single precision; out-of-line code (not classified)
#define PT_JIT_FSF 1
static void fnote(int g, int rd) {
    if (!dry || fool) return;
    if (!fsgl) fdbl |= 1u << g;
    if (rd && !(fwr >> g & 1)) frbw |= 1u << g;
    if (!rd) fwr |= 1u << g;
    if (!rd && fsgl) fsw |= 1u << g;
}
static int fsf_off(uint32_t pc) {
    for (uint32_t i = pc >> 2 & 1023, k = 0; k < 16; k++, i = (i + 1) & 1023) { if (nofsf[i] == pc + 1) return nofsf_n[i] >= 4; if (!nofsf[i]) return 0; }
    return 1;                                                                    // a full neighbourhood: off
}
static int fget(int v, int g) {
    if (dry) fcnt[g]++;
    fnote(g, 1);
    int h = fcache_active ? fmap[g] : -1;
    if (h >= 0 && fsf >> g & 1) {
        if (!fsgl) { E(0xFD400000 | (fpr_at / 8 + (uint32_t)g) << 10 | ENV << 5 | (uint32_t)v); return v; }   // a double: the file
        if (!(fvalid >> g & 1)) { E(0xFD400000 | (fpr_at / 8 + (uint32_t)g) << 10 | ENV << 5 | (uint32_t)h); E(0x1E624000 | (uint32_t)h << 5 | (uint32_t)h); fvalid |= 1u << g; }
        return h;                                                                // ldr; fcvt sH, dH: exact (a single)
    }
    if (h < 0) h = v;
    if (!fcache_active || fmap[g] < 0 || !(fvalid >> g & 1)) {
        E(0xFD400000 | (fpr_at / 8 + (uint32_t)g) << 10 | ENV << 5 | (uint32_t)h);
        if (fcache_active && fmap[g] >= 0) fvalid |= 1u << g;
    }
    return h;
}
static void fld(int v, int g) {                 // a copy as a double; a single-format value converted (exact): any reader
    int s = fsgl; fsgl = 1; int h = fget(v, g); fsgl = s;
    if (h != v) E((fcache_active && fsf >> g & 1 ? 0x1E22C000u : 0x1E604000u) | (uint32_t)h << 5 | (uint32_t)v); /* fcvt dV,sH | fmov dV,dH */
}
static void fcache_write(int g, int v, int bits) {
    if (dry) fcnt[g]++;
    fnote(g, 0);
    if (fcache_active && fmap[g] >= 0) {
        uint32_t h = (uint32_t)fmap[g];
        if (!(fsf >> g & 1)) E((bits ? 0x9E670000u : 0x1E604000u) | (uint32_t)v << 5 | h);
        else if (!fsgl) { if (bits) E(0x9E670000u | (uint32_t)v << 5 | h); E(0x1E624000 | (bits ? h : (uint32_t)v) << 5 | h); }   // fsgl: the caller wrote it
        fvalid |= 1u << g;
    }
}
static void fst(int v, int g) {
    E(0xFD000000 | (fpr_at / 8 + (uint32_t)g) << 10 | ENV << 5 | (uint32_t)v);
    fcache_write(g, v, 0);
}
static void fbits_store(int g, int x) { strx(x, ENV, fpr_at + 8u * g); fcache_write(g, x, 1); }
static void fbits_load(int x, int g) {
    fnote(g, 1);
    if (fcache_active && fmap[g] >= 0 && (fvalid >> g & 1) && !(fsf >> g & 1))
        E(0x9E660000 | (uint32_t)fmap[g] << 5 | (uint32_t)x);
    else ldrx(x, ENV, fpr_at + 8u * g);
    if (dry) fcnt[g]++;
}
static void fp_check(uint32_t pc) {          // MSR.FP=0 (machine): the interpreter runs the FP instruction and raises "FP unavailable"
    if (!jsys || (fp_hoist && fp_checked)) return;
    fp_checked = 1;
    ldrw(T1, ENV, OFF(msr)); uint32_t *b = cp; E(0x37000000 | 13u << 19 | T1);   // tbnz w17, #13 (FP), continue
    exit_why(pc, X_INTERP); fix(b, cp);
}
static void fload(int sgl, int d, int ea) {  // lfs/lfd: EA in register ea (not x17/x30) -> c->fpr[d]; lfs as sgl2dbl (an SNaN is not quieted)
    /* Guest memory only through GPR loads and stores: an MMIO data abort is emulated from its syndrome (ISV), which
     * SIMD&FP accesses do not have (hvf.c). lfs ends in d0 and fst stores it (and fills the FPR cache
     * from d0): no FP -> GPR move on its hot path; the Inf/NaN bits path moves x17 into d0. */
    if (!sgl) { if (zbase) E(0xF9400000 | ea << 5 | T1); else E(0xF8604800 | ea << 16 | MEM << 5 | T1); E(0xDAC00C00 | T1 << 5 | T1); fbits_store(d, T1); return; }
    mem(1, 4, T1, (Addr){ea, 0}); E(REVw | T1 << 5 | T1);
    uint32_t h = fcache_active && fsf >> d & 1 ? (uint32_t)fmap[d] : 0;
    if (h) E(0x1E270000 | T1 << 5 | h);                                                                  // single format: the bits
    ubfm(T2, T1, 23, 30); E(0x7100001F | 0xffu << 10 | T2 << 5); uint32_t *bn = cp; E(0x54000000 | EQ);   // Inf/NaN: by bits
    if (h) E(0x1E22C000 | h << 5 | 0); else { E(0x1E270000 | T1 << 5 | 0); E(0x1E22C000 | 0 << 5 | 0); }  // fmov s0, w17; fcvt d0, s0
    uint32_t *bd = cp; E(0x14000000); fix(bn, cp);
    logi(ANDi, ANDw, T2, T1, 0x7fffff); E(0xD3400000 | 35u << 16 | 34u << 10 | T2 << 5 | T2);          // x30 = mantissa << 29
    E(0xB2400000 | 12u << 16 | 10u << 10 | T2 << 5 | T2); ubfm(T1, T1, 31, 31);                          // | 0x7FF0...; w17 = sign
    E(0xAA000000 | T1 << 16 | 63u << 10 | T2 << 5 | T1); E(0x9E670000 | T1 << 5);                        // x17 = x30 | sign << 63; fmov d0, x17
    b_at(bd, cp); fsgl = 1; fst(0, d); fsgl = 0;
}
static void fstore(int sgl, int s, int ea, uint32_t pc) {   // stfs/stfd: stfs per Book I (bits), denormalization included
    (void)pc;
    if (sgl && fcache_active && fsf >> s & 1) {                                                          // the single's bits
        fsgl = 1; uint32_t h = (uint32_t)fget(0, s); fsgl = 0;
        E(0x1E260000 | h << 5 | T1); E(REVw | T1 << 5 | T1); mem(0, 4, T1, (Addr){ea, 0}); return;
    }
    fsgl = 1; fbits_load(T1, s); fsgl = 0;                                                               // the file is exact
    if (!sgl) { E(0xDAC00C00 | T1 << 5 | T1); if (zbase) E(0xF9000000 | ea << 5 | T1); else E(0xF8204800 | ea << 16 | MEM << 5 | T1); return; }
    E(0xD3400000 | 52u << 16 | 62u << 10 | T1 << 5 | T2); E(0x7100001F | 896u << 10 | T2 << 5);            // e = ubfx(52, 11); e > 896?
    uint32_t *b1 = cp; E(0x54000000 | HI);
    E(0xD3400000 | 63u << 16 | 62u << 10 | T1 << 5 | T2); uint32_t *b2 = cp; E(0xB4000000 | T2);             // zero (apart from the sign)?
    /* 0 < |x| < 2^-126, as dbl2sgl: sign | (1.m >> (926 - e)) & 0x7FFFFF, signed zero for e < 874. d0 keeps the bits
     * (FP scratch, as in fload); ea may be x16, so only x17/x30. Doom 3 made 17.5k interpreter exits per 5 s here. */
    E(0x9E670000 | T1 << 5); E(0xD3400000 | 52u << 16 | 62u << 10 | T1 << 5 | T2);                          // fmov d0, x17; x30 = e
    E(0x51000000 | 926u << 10 | T2 << 5 | T2); E(0x4B0003E0 | T2 << 16 | T2);                                // x30 = 926 - e
    E(0x7100001F | 53u << 10 | T2 << 5); uint32_t *bz = cp; E(0x54000000 | CS);                              // >= 53: mantissa 0
    E(0xD3400000 | 51u << 10 | T1 << 5 | T1); E(0xB2400000 | 12u << 16 | T1 << 5 | T1);                      // x17 = 1.m (53 bits)
    E(0x9AC02400 | T2 << 16 | T1 << 5 | T1); logi(ANDi, ANDw, T1, T1, 0x7fffff);                              // >> (926 - e)
    uint32_t *bj = cp; E(0x14000000); fix(bz, cp); movw(T1, 0); b_at(bj, cp);
    E(0x9E660000 | 0u << 5 | T2); E(0xD3400000 | 63u << 16 | 63u << 10 | T2 << 5 | T2);                      // x30 = sign
    E(0x2A000000 | T2 << 16 | 31u << 10 | T1 << 5 | T1); uint32_t *bs = cp; E(0x14000000);                   // w17 |= sign << 31
    fix(b1, cp); fix(b2, cp);
    E(0xD3400000 | 29u << 16 | 58u << 10 | T1 << 5 | T2); E(0xD37EFC00 | T1 << 5 | T1);                     // x30 = bits 29..58; x17 = bits 62..63
    E(0x2A000000 | T1 << 16 | 30u << 10 | T2 << 5 | T1); b_at(bs, cp); E(REVw | T1 << 5 | T1); mem(0, 4, T1, (Addr){ea, 0});
}
// --- FP arithmetic: the fast path does what the interpreter does when there is no special case ---------------------------
// Special cases (a NaN input, invalid operation, divide by zero, overflow, underflow; for single precision an inexact
// intermediate double; FPSCR with enables, NI or RN != 0) go to the interpreter with nothing changed.
// Fast path: the host result (FPCR = 0: round to nearest, no flush-to-zero), FI = IXC, XX/FX, FPRF by class.
static uint32_t *fsl[16]; static int nfsl;                       // branches to the slow path of the current instruction
static void fslow(uint32_t br) { fsl[nfsl++] = cp; E(br); }
static void fp_guard(void) {
    ldrw(T0, ENV, OFF(fpscr));
    if (!fp_hoist || !fp_guarded) { uint32_t f; bm32(0xff, &f); E(ANDSi | f | T0 << 5 | ZR); fslow(0x54000000 | NE); fp_guarded = 1; }
}
static void fpsr_zero(void) { E(0xD51B4420 | ZR); }                                     // msr fpsr, xzr
static void fpsr_ok(void) { uint32_t f; E(0xD53B4420 | T1); bm32(0xf, &f); E(ANDSi | f | T1 << 5 | ZR); fslow(0x54000000 | NE); }   // IOC DZC OFC UFC
static void fp_done_kind(uint32_t pc, int fprf, int sgl, int v, int finite) {   // finite: 1 excludes Inf, 2 also excludes denormals
    ubfm(T1, T1, 4, 5); logi(ANDi, ANDw, T0, T0, ~(fprf ? 0x7F000u : 0x60000u));          // FR, FI, FPRF: recomputed
    E(ORRw | T1 << 16 | 17u << 10 | T0 << 5 | T0);                                        // FI = IXC (bit 4), FR = bit 5
    uint32_t *nx = cp; E(0x34000000 | T1); uint32_t *hx = cp; E(0x37000000 | 25u << 19 | T0);
    logi(ORRi, ORRw, T0, T0, 0x80000000u); fix(hx, cp); logi(ORRi, ORRw, T0, T0, 0x02000000u); fix(nx, cp);   // XX; FX if XX is new
    if (fprf) {                                                                           // class: C|FPCC
        E(0x9E660000 | (uint32_t)v << 5 | T2); E(0xD37FF800 | T2 << 5 | T1);              // x30 = bits; x17 = x30 << 1
        uint32_t *nz = cp; E(0xB5000000 | T1);
        movw(T1, 0x02); uint32_t *s0 = cp; E(0xB6F80000 | T2); movw(T1, 0x12); uint32_t *d0 = cp; E(0x14000000);   // ±0
        fix(nz, cp); uint32_t *s1 = NULL, *d1 = NULL;
        if (finite < 2) E(0xD374F800 | T2 << 5 | T1);
        if (!finite) {
            E(0x7100001F | 0x7FFu << 10 | T1 << 5); uint32_t *ni = cp; E(0x54000000 | NE);
            movw(T1, 0x05); s1 = cp; E(0xB6F80000 | T2); movw(T1, 0x09); d1 = cp; E(0x14000000); fix(ni, cp);
        }
        if (finite < 2) E(0x7100001F | (sgl ? 897u : 1u) << 10 | T1 << 5);
        movw(T1, 0x04);
        if (finite < 2) { uint32_t *nn = cp; E(0x54000000 | CS); movw(T1, 0x14); fix(nn, cp); }
        uint32_t *s2 = cp; E(0xB6F80000 | T2); E(0x11001000 | T1 << 5 | T1);
        fix(s0, cp); if (s1) fix(s1, cp); fix(s2, cp); b_at(d0, cp); if (d1) b_at(d1, cp);
        E(ORRw | T1 << 16 | 12u << 10 | T0 << 5 | T0);
    }
    strw(T0, ENV, OFF(fpscr));
    if (fprf && fp_lazy_enabled) E(0x79000000u | (OFF(fs_kind) / 2) << 10 | ENV << 5 | ZR);   // strh wzr: FR FI FPRF stored, nothing owed
    if (!nfsl) return;
    uint32_t *over = cp; E(0x14000000);
    for (int i = 0; i < nfsl; i++) fix(fsl[i], cp);
    if (owe_gen) b_to(settle_fail); else exit_why(pc, X_INTERP);
    b_at(over, cp);
}
static void fp_done(uint32_t pc,int fprf,int sgl,int v) { fp_done_kind(pc,fprf,sgl,v,0); }
/* FPSCR.FR (the rounding incremented |result|) as bit 5 of w17 next to FI (bit 4); fp_done_kind stores both. RN only
 * (fp_guard). PT_FP_FR=0 (fp_fr_on = 0, as in interp.c) emits none of it: FR = 0. Computed for every op: skipping it
 * when the next op rewrites FR is unsound, since that op's bailouts and the lazy bailout exit are points where an
 * interrupt can see the state (tests/fpscr-lazy-jit.c). */
extern int fp_fr_on;
static void fr_sign(int sgl, int err, int r, int b) {   /* exact error err = exact - r (b >= 0: err / b): FR = FI and sign(err) != sign(r) */
    if (!fp_fr_on) return;
    E(0x2E201C00 | (uint32_t)r << 16 | (uint32_t)err << 5 | 7);                  // eor v7.8b, verr.8b, vr.8b
    if (b >= 0) E(0x2E201C00 | (uint32_t)b << 16 | 7u << 5 | 7);                 // division: ^ sign(b)
    if (sgl) { E(0x1E260000 | 7u << 5 | T2); ubfm(T2, T2, 31, 31); }             // w30 = sign bit of s7
    else { E(0x9E660000 | 7u << 5 | T2); E(0xD37FFC00 | T2 << 5 | T2); }          // x30 = sign bit of d7
    E(0x0A400000 | T1 << 16 | 4u << 10 | T2 << 5 | T2);                           // and w30, w30, w17, lsr #4 (FI)
    E(ORRw | T2 << 16 | 5u << 10 | T1 << 5 | T1);                                 // orr w17, w17, w30, lsl #5
}
static void fr_conv(int src) {   /* s3 = RN-single(d<src>), d<src> exact or rounded on the same side: FR = |s3| > |d<src>| */
    if (!fp_fr_on) return;
    E(0x1E22C000 | 3u << 5 | 7); E(0x1E60C000 | (uint32_t)src << 5 | 6); E(0x1E60C000 | 7u << 5 | 7);   // d7 = |double(s3)|, d6 = |src|
    E(0x1E602000 | 6u << 16 | 7u << 5); cset(T2, GT); E(ORRw | T2 << 16 | 5u << 10 | T1 << 5 | T1);
}
static void fcvt_s3(void) {   /* s3 = single(d3); with FR on d6 keeps d3 for fr_conv(6): writing s3 clears the rest of v3 */
    if (fp_fr_on) E(0x1E604000 | 3u << 5 | 6);
    E(0x1E624000 | 3u << 5 | 3);
}
static void fr_inexact_slow(void) { if (fp_fr_on) fslow(0x37000000 | 4u << 19 | T1); }   /* FPSR path without an error term */
// fadd fsub fmul fdiv (and the single forms) without FPSR: msr/mrs FPSR serialize the core (~12 ns per operation,
// measured). Inexactness (FI) comes from the exact error: TwoSum for addition, fma for multiply (a*b - p) and divide
// (a - q*b). Special results (Inf/NaN; for multiply and divide also denormals and exponents below lo, where the error is
// not representable) go to the interpreter; a zero from a zero factor or dividend is exact and stays here (x*0 is
// common in games). fp_guard guarantees RN = 0 and disabled exceptions. PT_FP_OLD=1: the FPSR path (for A/B comparison).
static int fp_old;
/* PT_FP_MIXED=1 (in Doom's loading stalls, fadds with a non-single operand left the JIT ~10000 times a second):
 * fadds/fsubs with a non-single input whose double result is inexact: the exact sum is no single, so FI = 1, and
 * rounding the double result to single gives the correctly rounded single unless that double is a single midpoint
 * (a midpoint strictly between the exact sum and its nearest double would be a nearer double) -- only then the
 * interpreter. Tiny and huge results: interpreter. */
static int fp_mixed;
static void fexp(int sgl, int v, int lo) {      // exponent of s<v>/d<v> in w17: Inf/NaN and (with lo) an exponent < lo go to the interpreter
    if (sgl) { E(0x1E260000 | (uint32_t)v << 5 | T2); ubfm(T1, T2, 23, 30); E(0x7100001F | 0xffu << 10 | T1 << 5); }
    else { E(0x9E660000 | (uint32_t)v << 5 | T2); E(0xD3400000 | 52u << 16 | 62u << 10 | T2 << 5 | T1); E(0x7100001F | 0x7ffu << 10 | T1 << 5); }
    fslow(0x54000000 | EQ);
    if (lo) { E(0x7100001F | (uint32_t)lo << 10 | T1 << 5); fslow(0x54000000 | CC); }
}
static void fma_inf(int sgl, uint32_t v, const uint32_t *in, int n, uint32_t **inf);
static int fp_fast(uint32_t in, uint32_t pc, int sgl) {
    static const uint32_t OP[32] = { [18] = 0x1E201800, [20] = 0x1E203800, [21] = 0x1E202800, [25] = 0x1E200800 };
    int xo5 = in >> 1 & 31, d = in >> 21 & 31, a = in >> 16 & 31, b = xo5 == 25 ? in >> 6 & 31 : in >> 11 & 31;
    uint32_t ty = sgl ? 0 : 1u << 22, x = 0, y = 1, *nd[2], *over = NULL;   // type: single / double; inputs d0, d1 or s4, s5
    fp_check(pc); nfsl = 0; fp_guard();
    if (sgl) { fld(0, a); fld(1, b); } else { x = (uint32_t)fget(0, a); y = (uint32_t)fget(1, b); }
    E(0x1E602000 | y << 16 | x << 5); fslow(0x54000000 | VS);   // NaN: interpreter
    if (sgl) {                                   // inputs exactly representable as single: one rounding, directly in single
        for (uint32_t r = 0; r < 2; r++) { E(0x1E624000 | r << 5 | (4 + r)); E(0x1E22C000 | (4 + r) << 5 | 7); E(0x1E602000 | r << 16 | 7u << 5); nd[r] = cp; E(0x54000000 | NE); }
        x = 4; y = 5;
    }
    int lo = xo5 == 18 || xo5 == 25 ? (sgl ? 32 : 64) : 0;
    E(OP[xo5] | ty | y << 16 | x << 5 | 3);                                  // r3 = a op b
    uint32_t *zok = NULL, *inf, din[2] = {sgl ? 0 : x, sgl ? 1 : y};
    fma_inf(sgl, 3, din, 2, &inf);                                          // NaN, overflow: interpreter; Inf from Inf: exact
    if (lo) {                                       // multiply, divide: an exponent < lo only for an exact zero from a zero
        E(0x7100001F | (uint32_t)lo << 10 | T1 << 5); uint32_t *big = cp; E(0x54000000 | CS);   // factor/dividend
        if (sgl) E(0x53000000 | 31u << 16 | 30u << 10 | T2 << 5 | T2); else E(0xD37FF800 | T2 << 5 | T2);   // without the sign
        fslow((sgl ? 0x35000000u : 0xB5000000u) | T2);                     // not zero (denormal, tiny)
        E(0x1E202008 | ty | x << 5);                                        // fcmp a, #0
        if (xo5 == 25) { uint32_t *az = cp; E(0x54000000 | EQ); E(0x1E202008 | ty | y << 5); fslow(0x54000000 | NE); fix(az, cp); }
        else fslow(0x54000000 | NE);                                        // 0/b, a*0, 0*b are exact: FI = 0
        movw(T1, 0); zok = cp; E(0x14000000);
        fix(big, cp);
        if (xo5 == 18) fexp(sgl, (int)x, lo);                               // for divide, the dividend too
    }
    if (xo5 == 25) E(0x1F208000 | ty | y << 16 | 3u << 10 | x << 5 | 6);     // e = a*b - p    (fnmsub)
    else if (xo5 == 18) E(0x1F008000 | ty | y << 16 | x << 10 | 3u << 5 | 6);   // e = a - q*b (fmsub)
    else {                                                                  // TwoSum(a, ±b), bb = s - a, aa = s - bb
        uint32_t ADD = 0x1E202800 | ty, SUB = 0x1E203800 | ty;
        E(SUB | x << 16 | 3u << 5 | 6); E(SUB | 6u << 16 | 3u << 5 | 7);
        E((xo5 == 21 ? SUB : ADD) | 6u << 16 | y << 5 | 2);                  // fadd: db = b - bb; fsub: b + bb = -db
        E(SUB | 7u << 16 | x << 5 | 7);                                     // da = a - aa
        E((xo5 == 21 ? ADD : SUB) | 2u << 16 | 7u << 5 | 6);                // e = da + db
    }
    E(0x1E202008 | ty | 6u << 5); cset(T1, NE); ubfm(T1, T1, 28, 27);      // FI = (e != 0) -> bit 4, like IXC for fp_done
    fr_sign(sgl, 6, 3, xo5 == 18 ? (int)y : -1);                            // add: s + e, mul: p + e, div: q + e / b
    if (zok) b_at(zok, cp);
    b_at(inf, cp);
    if (sgl) {
        E(0x1E22C000 | 3u << 5 | 3); over = cp; E(0x14000000);              // fcvt d3, s3
        fix(nd[0], cp); fix(nd[1], cp);                                     // an input is not a single: the path through double and FPSR
        fpsr_zero(); E(OP[xo5] | 1u << 22 | 1u << 16 | 3); fpsr_ok();
        uint32_t *mixed_done = NULL;
        if (fp_mixed && (xo5 == 20 || xo5 == 21)) {                         // PT_FP_MIXED (above)
            uint32_t *ex = cp; E(0x36000000 | 4u << 19 | T1);               // the double result is exact: the path without PT_FP_MIXED
            E(0x9E660000 | 3u << 5 | T2); E(0x12007000 | T2 << 5 | T2);
            if (fp_fr_on) fslow(0x34000000 | T2);                           // d3 is a single: FR needs the double's error
            E(0x52040000 | T2 << 5 | T2);                                   // the 29 bits ^ midpoint
            fslow(0x34000000 | T2);                                         // a single midpoint: interpreter
            fpsr_zero(); fcvt_s3(); fpsr_ok();                              // s3 = single(d3), RN; OFC, UFC: interpreter
            fexp(1, 3, 1);                                                  // and a denormal one (UX, as the interpreter)
            movw(T1, 0x10); fr_conv(6); E(0x1E22C000 | 3u << 5 | 3);        // FI = 1, FR: exact and d3 share a side; d3 = s3
            mixed_done = cp; E(0x14000000);
            fix(ex, cp);
        }
        fslow(0x37000000 | 4u << 19 | T1); fpsr_zero(); fcvt_s3(); fpsr_ok(); fr_conv(6); E(0x1E22C000 | 3u << 5 | 3);
        if (mixed_done) b_at(mixed_done, cp);
        b_at(over, cp);
    }
    fst(3, d); fp_done_kind(pc, 1, sgl, 3, 0);                               // the result may be Inf
    return 0;
}
// fmadd fmsub fnmadd fnmsub (and the single forms) without FPSR, only with PT_FMA_FAST=1. FI from ErrFma (Boldo,
// Muller 2011): r1 = a*c + y, (u1, u2) = TwoProduct(a, c), (a1, z) = TwoSum(y, u2), (b1, b2) = TwoSum(u1, a1),
// g = (b1 - r1) + b2, r2 = g + z; without underflow a*c + y - r1 = r2 + r3, |r3| <= ulp(r2)/2, so exact <=> r2 == 0.
// To the interpreter: Inf/NaN, and r1 or u1 with an exponent below lo (including zero and denormals). v16..v24: temporaries.
static int fma_fast, fma_single_direct;
/* FMA result v not finite: NaN or an overflow from finite inputs -> interpreter. Inf from an Inf input is exact
 * (FI = 0, no OX): w17 = 0 and *inf branches to the caller's join. Tiger libm and Cinebench R10 feed Inf on purpose
 * (~900k interpreter exits per 5 s in the R10 render without this). in[] are the double input registers.
 * Also used by fadd fsub fmul fdiv (n = 2): Xbench's FP Basic array is +-Inf and 0 after its first ~10 of 3680
 * passes, and 4 of its 6 operations per iteration left the JIT. */
static void fma_inf(int sgl, uint32_t v, const uint32_t *in, int n, uint32_t **inf) {
    if (sgl) { E(0x1E260000 | v << 5 | T2); ubfm(T1, T2, 23, 30); E(0x7100001F | 0xffu << 10 | T1 << 5); }
    else { E(0x9E660000 | v << 5 | T2); E(0xD3400000 | 52u << 16 | 62u << 10 | T2 << 5 | T1); E(0x7100001F | 0x7ffu << 10 | T1 << 5); }
    uint32_t *hit[3], *fin = cp; E(0x54000000 | NE);
    E(sgl ? 0x53000000u | 23u << 16 | 22u << 10 | T2 << 5 | T1 : 0xD374CC00u | T2 << 5 | T1);   // mantissa != 0: NaN
    fslow((sgl ? 0x35000000u : 0xB5000000u) | T1);
    E(0xD2EFFE00 | T1); E(0x9E670000 | T1 << 5 | 22);                                    // d22 = +Inf
    for (int i = 0; i < n; i++) { E(0x1E60C000 | in[i] << 5 | 23); E(0x1E602000 | 22u << 16 | 23u << 5); hit[i] = cp; E(0x54000000 | EQ); }
    fslow(0x54000000 | 14);                                                             // no Inf input: overflow
    for (int i = 0; i < n; i++) fix(hit[i], cp);
    movw(T1, 0); *inf = cp; E(0x14000000);
    fix(fin, cp);
}
static int fp_fma_fast(uint32_t in, uint32_t pc, int sgl) {
    int xo5 = in >> 1 & 31, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, cc = in >> 6 & 31;
    uint32_t ty = sgl ? 0 : 1u << 22, A = 0, X = 1, B = 2, *nd[3], *over = NULL;
    fp_check(pc); nfsl = 0; fp_guard();
    if (sgl) { fld(0, a); fld(1, cc); fld(2, b); }
    else { A = (uint32_t)fget(0, a); X = (uint32_t)fget(1, cc); B = (uint32_t)fget(2, b); }
    E(0x1E602000 | X << 16 | A << 5); fslow(0x54000000 | VS);
    E(0x1E602000 | B << 16 | B << 5); fslow(0x54000000 | VS);
    if (sgl) {
        for (uint32_t r = 0; r < 3; r++) { E(0x1E624000 | r << 5 | (4 + r)); E(0x1E22C000 | (4 + r) << 5 | 7); E(0x1E602000 | r << 16 | 7u << 5); nd[r] = cp; E(0x54000000 | NE); }
        A = 4; X = 5; B = 6;
    }
    const uint32_t din[3] = {sgl ? 0 : A, sgl ? 1 : X, sgl ? 2 : B}; uint32_t *inf;     // a, c, b as doubles
    if (sgl) {
        /* Single: inputs are singles, so p = a*c is exact in double and TwoSum gives s + e = p + y exactly. Rounding s
         * to single is then correctly rounded unless e != 0 and s lies on a single midpoint (interpreter). FI = e != 0
         * or s inexact as single. Unlike ErrFma, tiny products stay here (Geekbench Blur: ~2^-116). Inf/NaN and a
         * denormal single result go to the interpreter. d0 = a, d1 = c, d2 = b; result s3, FI in w17 bit 4. */
        uint32_t D = 1u << 22, DADD = 0x1E602800, DSUB = 0x1E603800, Yd = 2;
        if (xo5 == 28 || xo5 == 30) { E(0x1E614000 | 2u << 5 | 16); Yd = 16; }          // y = -b
        E(0x1E600800 | 1u << 16 | 0u << 5 | 17);                                          // p = a*c, exact
        E(DADD | Yd << 16 | 17u << 5 | 18);                                               // s = p + y
        E(DSUB | 17u << 16 | 18u << 5 | 19); E(DSUB | 19u << 16 | 18u << 5 | 20);          // bb = s - p; s - bb
        E(DSUB | 20u << 16 | 17u << 5 | 21); E(DSUB | 19u << 16 | Yd << 5 | 22);           // p - (s - bb); y - bb
        E(DADD | 22u << 16 | 21u << 5 | 23);                                              // e
        E(0x1E604000 | 18u << 5 | 22);                                                    // d22 = s (the midpoint step moves d18)
        if (fma_single_direct) {
            /* All operands were proved exactly single above. Hardware FMADD S
             * rounds the exact fused expression once, including midpoint ties. */
            E((xo5 & 1 ? 0x1f000000u : 0x1f208000u) | X << 16 | B << 10 | A << 5 | 3);
        } else E(0x1E624000 | 18u << 5 | 3);
        fma_inf(1, 3, din, 3, &inf);
        E(0x1E60C000 | 18u << 5 | 19); E(0xD2E70200 | T1); E(0x9E670000 | T1 << 5 | 20);   // d19 = |s|, d20 = 2^-126
        E(0x1E602000 | 20u << 16 | 19u << 5); uint32_t *big = cp; E(0x54000000 | GT);    // |s| > 2^-126: never tiny (UX)
        E(0x1E602008 | 18u << 5); fslow(0x54000000 | NE); fix(big, cp);                  // else only an exact zero
        if (fma_single_direct) {
            /* p is exact in double; TwoSum gives exact s+e. If e!=0 no single
             * can equal that sum; otherwise compare the actual fused result. */
            E(0x1e602008u | 23u << 5); uint32_t *exact=cp;E(0x54000000u|EQ);
            movw(T1,0x10);uint32_t *fi=cp;E(0x14000000u);
            fix(exact,cp);E(0x1e22c000u|3u<<5|24);E(0x1e602000u|24u<<16|18u<<5);
            cset(T1,NE);ubfm(T1,T1,28,27);b_at(fi,cp);
        } else {
        E(0x9E660000 | 18u << 5 | T2);                                                    // x30 = bits(s)
        E(0x1E202008 | D | 23u << 5); uint32_t *exact = cp; E(0x54000000 | EQ);           // e == 0?
        E(0x12007000 | T2 << 5 | T1); E(0x52040000 | T1 << 5 | T1);                      // the 29 bits single drops ^ midpoint
        uint32_t *notmid = cp; E(0x35000000 | T1);
        E(0x9E660000 | 23u << 5 | T1); E(0xCA000000 | T2 << 16 | T1 << 5 | T1);          // midpoint, e != 0: the exact value is
        uint32_t *down = cp; E(0xB7F80000 | T1);                                          // past it, towards e (sign(e) ^ sign(s))
        E(0xD2A20000 | T1); E(0x8B000000 | T1 << 16 | T2 << 5 | T2); uint32_t *mid = cp; E(0x14000000);   // away from 0
        fix(down, cp); E(0xD2A20000 | T1); E(0xCB000000 | T1 << 16 | T2 << 5 | T2); b_at(mid, cp);        // towards 0
        E(0x9E670000 | T2 << 5 | 18); E(0x1E624000 | 18u << 5 | 3); fix(notmid, cp);      // neighbour single, exact
        movw(T1, 0x10); uint32_t *fi = cp; E(0x14000000);                                // FI = 1
        fix(exact, cp); E(0x1E22C000 | 3u << 5 | 24); E(0x1E602000 | 24u << 16 | 18u << 5);   // FI = double(s3) != s
        cset(T1, NE); ubfm(T1, T1, 28, 27); b_at(fi, cp);
        }
        if (fp_fr_on) {   /* exact - S3 = (s - S3) + e: s - S3 is exact and outweighs e unless S3 == s; its sign is FR's */
            E(0x1E22C000 | 3u << 5 | 24); E(DSUB | 24u << 16 | 22u << 5 | 20); E(DADD | 23u << 16 | 20u << 5 | 20);
            fr_sign(0, 20, 24, -1);
        }
        b_at(inf, cp);
    } else {
        uint32_t Y = B, FADD = 0x1E202800 | ty, FSUB = 0x1E203800 | ty;
        if (xo5 == 28 || xo5 == 30) { E(0x1E214000 | ty | B << 5 | 16); Y = 16; }        // fmsub, fnmsub: y = -b
        int lo = sgl ? 32 : 64;
        E(0x1F000000 | ty | X << 16 | Y << 10 | A << 5 | 3); fma_inf(sgl, 3, din, 3, &inf);  // r1 = a*c + y
        E(0x1E200800 | ty | X << 16 | A << 5 | 17); fexp(sgl, 17, 0);                     // u1 = a*c
        E(0x7100001F | (uint32_t)lo << 10 | T1 << 5); uint32_t *big = cp; E(0x54000000 | CS);   // u1 tiny: only an exact zero
        if (sgl) E(0x53000000 | 31u << 16 | 30u << 10 | T2 << 5 | T2); else E(0xD37FF800 | T2 << 5 | T2);
        fslow((sgl ? 0x35000000u : 0xB5000000u) | T2);
        E(0x1E202008 | ty | A << 5); uint32_t *az = cp; E(0x54000000 | EQ); E(0x1E202008 | ty | X << 5); fslow(0x54000000 | NE); fix(az, cp);
        movw(T1, 0); uint32_t *zok = cp; E(0x14000000);                                 // a*c = +-0 exactly: r1 = 0 + y exactly, FI = 0
        fix(big, cp); E(0x1E202008 | ty | 3u << 5); uint32_t *nz = cp; E(0x54000000 | NE);   // r1 = 0 with u1 not tiny:
        movw(T1, 0); uint32_t *zero = cp; E(0x14000000); fix(nz, cp);                    // exact cancellation, FI = 0 (LU)
        fexp(sgl, 3, lo);                                                                // otherwise r1 is not below lo
        E(0x1F208000 | ty | X << 16 | 17u << 10 | A << 5 | 18);                          // u2 = a*c - u1
        E(FADD | 18u << 16 | Y << 5 | 19); E(FSUB | Y << 16 | 19u << 5 | 21); E(FSUB | 21u << 16 | 19u << 5 | 22);   // TwoSum(y, u2)
        E(FSUB | 21u << 16 | 18u << 5 | 23); E(FSUB | 22u << 16 | Y << 5 | 22); E(FADD | 23u << 16 | 22u << 5 | 20);  //   -> a1 v19, z v20
        E(FADD | 19u << 16 | 17u << 5 | 21); E(FSUB | 17u << 16 | 21u << 5 | 22); E(FSUB | 22u << 16 | 21u << 5 | 23); // TwoSum(u1, a1)
        E(FSUB | 22u << 16 | 19u << 5 | 18); E(FSUB | 23u << 16 | 17u << 5 | 23); E(FADD | 18u << 16 | 23u << 5 | 22); //   -> b1 v21, b2 v22
        E(FSUB | 3u << 16 | 21u << 5 | 23); E(FADD | 22u << 16 | 23u << 5 | 23); E(FADD | 20u << 16 | 23u << 5 | 24);  // g; r2 = g + z
        E(0x1E202008 | ty | 24u << 5); cset(T1, NE); ubfm(T1, T1, 28, 27);              // FI = (r2 != 0) -> bit 4
        fr_sign(sgl, 24, 3, -1);                                                         // exact - r1 = r2 + r3, |r3| < |r2|
        b_at(zok, cp); b_at(zero, cp); b_at(inf, cp);
    }
    if (sgl) {
        E(0x1E22C000 | 3u << 5 | 3); over = cp; E(0x14000000);                       // fcvt d3, s3
        for (int r = 0; r < 3; r++) fix(nd[r], cp);                                  // an input is not a single: the FPSR path
        fpsr_zero();
        if (xo5 == 29 || xo5 == 31) E(0x1F400000 | 1u << 16 | 2u << 10 | 3); else E(0x1F608000 | 1u << 16 | 2u << 10 | 3);
        fpsr_ok(); fslow(0x37000000 | 4u << 19 | T1); fpsr_zero(); fcvt_s3(); fpsr_ok(); fr_conv(6); E(0x1E22C000 | 3u << 5 | 3);
        b_at(over, cp);
    }
    if (xo5 >= 30) E(0x1E614000 | 3u << 5 | 3);                                       // fnm*: sign after rounding
    fst(3, d); fp_done(pc, 1, sgl, 3);
    return 0;
}
static int fp_owe(uint32_t in, uint32_t pc, int sgl);
static int fp_arith(uint32_t in, uint32_t pc, int sgl) {        // A-form: fadd fsub fmul fdiv fmadd fmsub fnmadd fnmsub (and the single forms)
    int xo5 = in >> 1 & 31, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, cc = in >> 6 & 31;
    if (in & 1 || !(xo5 == 18 || xo5 == 20 || xo5 == 21 || xo5 == 25 || xo5 >= 28)) return -1;
    if (fp_lazy_active && !fp_old && (xo5 < 28 || fma_fast)) return fp_owe(in, pc, sgl);
    if (!fp_old && xo5 < 28) return fp_fast(in, pc, sgl);
    if (!fp_old && fma_fast) return fp_fma_fast(in, pc, sgl);
    fp_check(pc); nfsl = 0; fp_guard();
    fld(0, a); fld(1, xo5 == 25 || xo5 >= 28 ? cc : b); E(0x1E602000 | 1u << 16 | 0u << 5); fslow(0x54000000 | VS);   // NaN: interpreter
    if (xo5 >= 28) { fld(2, b); E(0x1E602000 | 2u << 16 | 2u << 5); fslow(0x54000000 | VS); }
    uint32_t *nd[3], *over = NULL; int nn = 0;
    if (sgl) {                                       // inputs exactly representable as single: one rounding, directly in single (as the interpreter)
        for (uint32_t r = 0; r < (xo5 >= 28 ? 3u : 2u); r++) {                        // fcvt s4+r, dr; fcvt d7, s4+r; fcmp d7, dr
            E(0x1E624000 | r << 5 | (4 + r)); E(0x1E22C000 | (4 + r) << 5 | 7); E(0x1E602000 | r << 16 | 7u << 5); nd[nn++] = cp; E(0x54000000 | NE);
        }
        fpsr_zero();
        switch (xo5) {
        case 18: E(0x1E201800 | 5u << 16 | 4u << 5 | 3); break;  case 20: E(0x1E203800 | 5u << 16 | 4u << 5 | 3); break;
        case 21: E(0x1E202800 | 5u << 16 | 4u << 5 | 3); break;  case 25: E(0x1E200800 | 5u << 16 | 4u << 5 | 3); break;
        case 29: case 31: E(0x1F000000 | 5u << 16 | 6u << 10 | 4u << 5 | 3); break;       // s3 = s4 * s5 + s6
        default: E(0x1F208000 | 5u << 16 | 6u << 10 | 4u << 5 | 3); break;               // s3 = s4 * s5 - s6
        }
        fpsr_ok(); fr_inexact_slow(); E(0x1E22C000 | 3u << 5 | 3); over = cp; E(0x14000000);
        for (int i = 0; i < nn; i++) fix(nd[i], cp);                                    // otherwise through double
    }
    fpsr_zero();
    switch (xo5) {
    case 18: E(0x1E601800 | 1u << 16 | 3); break;  case 20: E(0x1E603800 | 1u << 16 | 3); break;
    case 21: E(0x1E602800 | 1u << 16 | 3); break;  case 25: E(0x1E600800 | 1u << 16 | 3); break;
    case 29: case 31: E(0x1F400000 | 1u << 16 | 2u << 10 | 3); break;                  // d3 = d0 * d1 + d2
    default: E(0x1F608000 | 1u << 16 | 2u << 10 | 3); break;                           // d3 = d0 * d1 - d2
    }
    fpsr_ok();
    if (sgl) { fslow(0x37000000 | 4u << 19 | T1); fpsr_zero(); fcvt_s3(); fpsr_ok(); fr_conv(6); E(0x1E22C000 | 3u << 5 | 3); b_at(over, cp); }   // exact double -> float
    else fr_inexact_slow();
    if (xo5 >= 30) E(0x1E614000 | 3u << 5 | 3);                                        // fnmadd/fnmsub: sign after rounding
    fst(3, d); fp_done(pc, 1, sgl, 3);
    return 0;
}
/* PT_FP_LAZY: lazy FPSCR status. fadd fsub fmul fdiv and the FMA family (single and double) with XX
 * already set, RN = NI = enables = 0, ordinary inputs and a result that cannot raise OX or UX change only FR, FI and
 * FPRF: the JIT stores the result and records the operation (CPU.fs_kind, fs_x; ppc.h) instead of computing them.
 * Every FPSCR reader settles first: ppc_fp_settle in the interpreter; in the JIT mffs, mtfsf, mtfsfi and fctiw(z) call
 * the settle routine (settle_gen); an eager operation's store supersedes the record (fp_done_kind); fcmpu marks the
 * FPCC it wrote. The record lives in CPU, so it survives links, exits, faults and interrupts: no exit needs a recovery.
 * Single operations on single inputs are one host single operation (rounded once, as the G4). Anything else runs the
 * operation's eager form, emitted inline after the fast path. Geekbench 2 kernels: Dot Product 105 -> ~60, LU
 * 101 -> ~50 A64 instructions per iteration, and no FPSCR store-to-load chain between operations. */
_Static_assert(offsetof(CPU, fs_x) / 8 < 64 && offsetof(CPU, fs_kind) < 4096, "fs_x: stp imm7, fs_kind: ldrb/strh imm12");
static int fp_xx;   /* fp_owe in this block has seen XX = 1 (sticky; only mtfsf/mtfsfi in a block clear it: fp_xx = 0) */
/* An operation followed in its block by another one skips its record and leaves it here; the next operation
 * stores it only on its way out of line (special results, the eager form, an exit to the interpreter), so the CPU state
 * at every exit is still the state after the previous operation. Geekbench Mandelbrot: 9 operations in a row, 4 stores
 * each. The registers are FPR cache registers the operation did not write; the next operation that writes one of them
 * keeps its value (save in fp_owe). */
static struct { int on, uB, uC; uint32_t a, b, c, kind, sf; } pend;
static void fp_record(uint32_t a, uint32_t b, uint32_t c, int uB, int uC, uint32_t kind, uint32_t sf) {   // sf: a b c single format
    uint32_t xs = OFF(fs_x) / 8;
    if (sf & 1) { E(0x1E22C000 | a << 5 | 16); a = 16; }                  // fcvt d16, sA
    if (sf & 2) { E(0x1E22C000 | b << 5 | 17); b = 17; }
    if (sf & 4) { E(0x1E22C000 | c << 5 | 18); c = 18; }
    if (uB) E(0x6D000000 | xs << 15 | b << 10 | ENV << 5 | a);              // a..c may be the destination's cache register
    else E(0xFD000000 | xs << 10 | ENV << 5 | a);
    if (uC) E(0xFD000000 | (xs + 2) << 10 | ENV << 5 | c);
    movw(T1, kind); E(0x79000000u | (OFF(fs_kind) / 2) << 10 | ENV << 5 | T1);   // strh, FPCC byte 0
}
static int fp_owe(uint32_t in, uint32_t pc, int sgl) {
    static const uint32_t OP[32] = { [18] = 0x1E201800, [20] = 0x1E203800, [21] = 0x1E202800, [25] = 0x1E200800 };
    int xo5 = in >> 1 & 31, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, cc = in >> 6 & 31;
    int fma = xo5 >= 28, uB = xo5 != 25, uC = xo5 == 25 || fma, addsub = xo5 == 20 || xo5 == 21;
    uint32_t ty = sgl ? 0 : 1u << 22, *eager[12], *xx0 = NULL, *special[2]; int ne = 0, ns = 0;
    fp_check(pc);
    fsgl = sgl; uint32_t hA = (uint32_t)fget(0, a), hB = uB ? (uint32_t)fget(1, b) : 0, hC = uC ? (uint32_t)fget(2, cc) : 0; fsgl = 0;
    uint32_t valid = fvalid;                                                // the eager form runs after these loads
    uint32_t sfm = sgl && fcache_active ? (fsf >> a & 1) | (uB ? (fsf >> b & 1) << 1 : 0) | (uC ? (fsf >> cc & 1) << 2 : 0) : 0;   // single-format operands
    int sfd = sgl && fcache_active && fsf >> d & 1;
    __typeof__(pend) prev = pend; pend.on = 0;
    if (!fp_hoist || !fp_guarded || !fp_xx) {                               // once per block: RN NI enables 0, XX = 1
        ldrw(T0, ENV, OFF(fpscr));
        if (!fp_hoist || !fp_guarded) { uint32_t f; bm32(0xff, &f); E(ANDSi | f | T0 << 5 | ZR); eager[ne++] = cp; E(0x54000000 | NE); fp_guarded = 1; }
        if (!fp_hoist || !fp_xx) { xx0 = cp; E(0x36000000 | 25u << 19 | T0); fp_xx = 1; }   // tbz w16, #25: XX = 0
    }
    /* No NaN test of the inputs: a NaN input makes a NaN result, which the result test below sends to the
     * eager form (and a single input check refuses a NaN as not exactly single). */
    uint32_t x = hA, y = hB, z = hC;
    if (sgl) {                                                              // inputs exactly single
        const uint32_t h[3] = {hA, hB, hC}, use[3] = {1, (uint32_t)uB, (uint32_t)uC}; uint32_t *xyz[3] = {&x, &y, &z};
        for (uint32_t r = 0; r < 3; r++) if (use[r] && !(sfm >> r & 1)) {
            E(0x1E624000 | h[r] << 5 | (4 + r)); E(0x1E22C000 | (4 + r) << 5 | 7); E(0x1E602000 | h[r] << 16 | 7u << 5);
            eager[ne++] = cp; E(0x54000000 | NE); *xyz[r] = 4 + r;
        }
    }
    /* A double result goes straight into the destination's cache register (an fmov on every dependency
     * step otherwise: Mandelbrot's chain is five operations). When that register is an operand or a pending record's,
     * its value is kept in d21 first (off the chain) and recorded from there; out of line the result goes to d3 and the
     * register gets its value back, so the checks and the eager form see the operands. */
    int cr = fcache_active && fmap[d] >= 0 ? fmap[d] : -1;
    uint32_t R = cr >= 0 && (sfd || (!sgl && xo5 < 30)) ? (uint32_t)cr : 3u;
    int save = R != 3 && (R == hA || (uB && R == hB) || (uC && R == hC) || (prev.on && (R == prev.a || (prev.uB && R == prev.b) || (prev.uC && R == prev.c))));
    if (save) E(0x1E604000 | R << 5 | 21);                                  // fmov d21, dR
    if (!fma) E(OP[xo5] | ty | (xo5 == 25 ? z : y) << 16 | x << 5 | R);              // a op b, a * c
    else E((xo5 & 1 ? 0x1F000000u : 0x1F208000u) | ty | z << 16 | y << 10 | x << 5 | R);   // a*c + b, a*c - b
    /* Ordinary results here; Inf/NaN, and for mul, div and FMA also below 2^-959 (double, as the eager forms) or
     * 2^-125 (single), are checked out of line. Sums and differences of tiny values are exact. */
    if (sgl) { E(0x1E260000 | R << 5 | T2); ubfm(T1, T2, 23, 30); }
    else { E(0x9E660000 | R << 5 | T2); E(0xD3400000 | 52u << 16 | 62u << 10 | T2 << 5 | T1); }
    uint32_t max = sgl ? 0xff : 0x7ff, lo = addsub ? 0 : sgl ? 2 : 64;
    if (lo) { E(0x51000000 | lo << 10 | T1 << 5 | T1); E(0x7100001F | (max - 1 - lo) << 10 | T1 << 5); special[ns++] = cp; E(0x54000000 | HI); }
    else { E(0x7100001F | max << 10 | T1 << 5); special[ns++] = cp; E(0x54000000 | EQ); }
    uint32_t *accept = cp;
    uint32_t nx = !check && fp_hoist && fp_guarded && fp_xx && cur_n + 1 < maxblk && has_next(pc, 1) && pc + 4 < 0xFFF00000u &&
                  cp - blk_start < 45056 ? nexti(pc, 1) : 0, nxo = nx >> 1 & 31;
    int defer = (nx >> 26 == 59 || nx >> 26 == 63) && !(nx & 1) && (nxo == 18 || nxo == 20 || nxo == 21 || nxo == 25 || (nxo >= 28 && fma_fast)) &&
                hA >= 25 && (!uB || hB >= 25) && (!uC || hC >= 25) && (cr < 0 || ((uint32_t)cr != hA && (!uB || (uint32_t)cr != hB) && (!uC || (uint32_t)cr != hC)));
    uint32_t kind = (sgl ? 0x20u : 0) | (uint32_t)xo5;
    if (defer) { pend.on = 1; pend.a = hA; pend.b = hB; pend.c = hC; pend.uB = uB; pend.uC = uC; pend.kind = kind; pend.sf = sfm; }
    else fp_record(hA == R ? 21 : hA, hB == R ? 21 : hB, hC == R ? 21 : hC, uB, uC, kind, sfm);   // the record first
    /* A conversion or negation still to come writes the destination's cache register directly (no fmov
     * after it: Dot Product's sum is a loop-carried single chain). */
    uint32_t r = (sgl || xo5 >= 30) && cr >= 0 ? (uint32_t)cr : R;
    if (sfd) {                                                              // the single in its register; the file gets the double
        if (xo5 >= 30) E(0x1E214000 | R << 5 | R);                          // fneg sR
        E(0x1E22C000 | R << 5 | 3); E(0xFD000000 | (fpr_at / 8 + (uint32_t)d) << 10 | ENV << 5 | 3); fvalid |= 1u << d;
    } else {
        if (sgl) E(0x1E22C000 | 3u << 5 | r);                               // fcvt dR, s3
        if (xo5 >= 30) E(0x1E614000 | (sgl ? r : 3u) << 5 | r);             // fnmadd/fnmsub: sign after rounding
        if (r == 3) { fsgl = sgl; fst(3, d); fsgl = 0; }
        else { E(0xFD000000 | (fpr_at / 8 + (uint32_t)d) << 10 | ENV << 5 | r); fvalid |= 1u << d; }   // write-through, as fst
    }
    uint32_t done = fvalid, *over = cp; E(0x14000000);
    /* Out of line, exact and status-only: Inf from an Inf input (no OX), a product or FMA with a zero factor (the result
     * is the addend, exactly), a zero dividend. Xbench FP Basic's array is +-Inf and 0 after ~10 of its 3680 passes.
     * The eager forms settle these too (fma_inf, zero factors). */
    for (int k = 0; k < ns; k++) fix(special[k], cp);
    if (R != 3) E(0x1E604000 | R << 5 | 3);                                 // fmov d3, dR: the out-of-line code's result
    uint32_t *back = accept;                                                // the result accepted after all
    if (save) { E(0x1E604000 | 21u << 5 | R); uint32_t *j = cp; E(0x14000000); back = cp; E(0x1E604000 | 3u << 5 | R); b_to(accept); b_at(j, cp); }
    if (prev.on) fp_record(prev.a, prev.b, prev.c, prev.uB, prev.uC, prev.kind, prev.sf);   // out of line: the previous record first
    uint32_t r3 = sgl ? 7u : 3u;
    if (sgl) E(0x1E22C000 | 3u << 5 | 7);                                   // fcvt d7, s3
    E(0x1E602000 | r3 << 16 | r3 << 5); eager[ne++] = cp; E(0x54000000 | VS);   // NaN result: invalid operation
    E(0xD2EFFE00 | T1); E(0x9E670000 | T1 << 5 | 22); E(0x1E60C000 | r3 << 5 | 23); E(0x1E602000 | 22u << 16 | 23u << 5);   // |r| == +Inf?
    uint32_t *fin = cp; E(0x54000000 | NE);
    const uint32_t h[3] = {hA, hB, hC}, use[3] = {1, (uint32_t)uB, (uint32_t)uC};
    for (uint32_t r = 0; r < 3; r++) if (use[r]) {
        if (sfm >> r & 1) E(0x1E22C000 | h[r] << 5 | 23);                   // fcvt d23, s: a single-format operand
        E(0x1E60C000 | (sfm >> r & 1 ? 23u : h[r]) << 5 | 23); E(0x1E602000 | 22u << 16 | 23u << 5); E(0x54000000 | EQ | ((uint32_t)(back - cp) & 0x7ffff) << 5);
    }
    uint32_t *ox = cp; E(0x14000000);                                       // Inf from finite inputs: OX
    fix(fin, cp);
    if (!addsub) {                                                          // a zero (or tiny) product, quotient or FMA
        E((sfm & 1 ? 0x1E202008 : 0x1E602008) | hA << 5); E(0x54000000 | EQ | ((uint32_t)(back - cp) & 0x7ffff) << 5);   // fcmp hA, #0.0
        if (uC) { E((sfm & 4 ? 0x1E202008 : 0x1E602008) | hC << 5); E(0x54000000 | EQ | ((uint32_t)(back - cp) & 0x7ffff) << 5); }
    }
    for (int k = 0; k < ne; k++) fix(eager[k], cp);
    b_at(ox, cp);
    if (prev.on) fp_record(prev.a, prev.b, prev.c, prev.uB, prev.uC, prev.kind, prev.sf);
    fvalid = valid; int checked = fp_checked, guarded = fp_guarded; fp_checked = fp_guarded = 0; fool = 1;
    if (xo5 < 28) fp_fast(in, pc, sgl); else fp_fma_fast(in, pc, sgl);       // complete status (or the interpreter)
    uint32_t eager_valid = fvalid;
    if (xx0) {                                                              // XX = 0: the block's later operations assume XX = 1
        uint32_t *skip = cp; E(0x14000000); fix(xx0, cp);
        fvalid = valid; fp_checked = fp_guarded = 0;
        if (xo5 < 28) fp_fast(in, pc, sgl); else fp_fma_fast(in, pc, sgl);
        exit_to(pc + 4); b_at(skip, cp);
    }
    fvalid = done & eager_valid; fp_checked = checked; fp_guarded = guarded; fool = 0;
    b_at(over, cp);
    return 0;
}
/* Before an inline FPSCR read or partial write (mffs mtfsf mtfsfi fctiw): the owed status, by the settle routine (the
 * VM cannot call C). Its failure, or no routine (fixtures), leaves the instruction to the interpreter, which settles. */
static void fp_settle_site(uint32_t pc) {
    if (!fp_lazy_enabled) return;
    E(0x39400000u | OFF(fs_kind) << 10 | ENV << 5 | T0); uint32_t *none = cp; E(0x34000000 | T0);   // ldrb w16; cbz
    if (settle_fn) { uint32_t *bl = cp; E(0x94000000u | ((uint32_t)(settle_fn - bl) & 0x3ffffff)); uint32_t *ok = cp; E(0x34000000 | T0);
                     exit_why(pc, X_INTERP); fix(ok, cp); }
    else exit_why(pc, X_INTERP);
    fix(none, cp);
}
/* The SR-switch routine, once per code buffer before the blocks (code0), bl from an rfi or mtmsr into DR=1 whose SRs
 * changed in real mode (srgen != srgen_ok): XNU reloads all 16 SRs on every exception from user mode and back, and
 * without it each such return leaves the VM (up to 165 000 lookup exits a second). It looks for a slot the
 * environment published (vs_ok, vs_sr: exactly these 16 SRs, ASIDs current) and installs it as the environment's SR
 * switch does: TTBR0 values for DR=1 (ttbr[1], ttbr[2]), vs_cur, srgen_ok = srgen, icbi_lea = 0. w16 = 1 installed,
 * 0 not (the caller leaves for the environment). Uses x16 x17 x30 (x17 and x30 restored) and v0, v16-v24 (scratch at
 * every site); never the guest or cache registers, whose values the failure path stores again. */
static uint32_t *srsw_fn;
static void srsw_gen(void) {
    srsw_fn = cp;
    E(0x9E670000 | T1 << 5 | 24); E(0x9E670000 | T2 << 5 | 0);              // fmov d24, x17; fmov d0, x30
    E(0x91000000 | OFF(sr) << 10 | ENV << 5 | T0); E(0x4C402800 | T0 << 5 | 16);   // add x16, x27, #sr; ld1 {v16-v19.4s}, [x16]
    ldrw(T1, ENV, OFF(vs_ok)); E(0x91000000 | OFF(vs_sr) << 10 | ENV << 5 | T0);
    uint32_t *loop = cp; uint32_t *fail = cp; E(0x34000000 | T1);           // cbz w17, fail
    uint32_t *skip = cp; E(0x36000000 | T1);                                // tbz w17, #0, next
    E(0x4C402800 | T0 << 5 | 20);                                           // ld1 {v20-v23.4s}, [x16]
    for (int i = 0; i < 4; i++) E(0x6EA08C00 | (16u + i) << 16 | (20u + i) << 5 | (20u + i));   // cmeq v20+i, v20+i, v16+i
    E(0x4E201C00 | 21u << 16 | 20u << 5 | 20); E(0x4E201C00 | 23u << 16 | 22u << 5 | 22); E(0x4E201C00 | 22u << 16 | 20u << 5 | 20);
    E(0x6EB1A800 | 20u << 5 | 20); E(0x1E260000 | 20u << 5 | T2);          // uminv s20, v20.4s; fmov w30, s20
    uint32_t *found = cp; E(0x35000000 | T2);                               // cbnz w30, found
    fix(skip, cp); ubfm(T1, T1, 1, 31); E(0x91000000 | 64u << 10 | T0 << 5 | T0); b_to(loop);   // next slot
    fix(fail, cp); movw(T0, 0); uint32_t *out = cp; E(0);
    fix(found, cp);
    E(0xCB000000 | ENV << 16 | T0 << 5 | T2); E(0xD1000000 | OFF(vs_sr) << 10 | T2 << 5 | T2); E(0xD3400000 | 6u << 16 | 63u << 10 | T2 << 5 | T2);   // w30 = slot
    strw(T2, ENV, OFF(vs_cur));
    E(0x91000000 | OFF(vs_ttbr) << 10 | ENV << 5 | T0); E(0x8B000000 | T2 << 16 | 4u << 10 | T0 << 5 | T0);
    E(0xA9400000 | T1 << 10 | T0 << 5 | T0);                                // ldp x16, x17, [x16]
    strx(T0, ENV, OFF(ttbr) + 8); strx(T1, ENV, OFF(ttbr) + 16);
    ldrw(T0, ENV, OFF(srgen)); strw(T0, ENV, OFF(srgen_ok)); strw(ZR, ENV, OFF(icbi_lea)); movw(T0, 1);
    b_at(out, cp);
    E(0x9E660000 | 24u << 5 | T1); E(0x9E660000 | 0u << 5 | T2); E(0xD65F03C0);   // fmov x17, d24; fmov x30, d0; ret
    __builtin___clear_cache((char *)srsw_fn, (char *)cp);
    code0 = cp;
}
/* After "cmp srgen, srgen_ok" at an rfi/mtmsr into DR=1: the branch to patch to the environment exit, taken when the SRs
 * set in real mode are not installed and no published slot has them. */
static uint32_t *sr_site(void) {
    if (!srsw_fn) { uint32_t *s = cp; E(0x54000000 | NE); return s; }
    uint32_t *eq = cp; E(0x54000000 | EQ);
    uint32_t *bl = cp; E(0x94000000u | ((uint32_t)(srsw_fn - bl) & 0x3ffffff));
    uint32_t *s = cp; E(0x34000000 | T0);
    fix(eq, cp); return s;
}
/* The settle routine, once per code buffer before the blocks (code0): bl from fp_settle_site, w16 = 0 settled, 1 not
 * (the interpreter settles). It dispatches on the recorded kind to that operation's eager form, emitted over CPU.fs_x
 * as its FPR file (A, B, C, D = 0..3) and without the hoisted checks the record already passed; the eager form stores
 * FR, FI, FPRF and clears the record. FPCC written by fcmpu after the operation is put back. Clobbers x16 x17 x30 and
 * v0-v24 (scratch at every site), never the guest or cache registers. */
static void settle_gen(void) {
    static const uint8_t XO[8] = {18, 20, 21, 25, 28, 29, 30, 31};
    uint32_t at = fpr_at; int hoist = fp_hoist, checked = fp_checked, guarded = fp_guarded, cache = fcache_active, lazy = fp_lazy_active;
    fpr_at = OFF(fs_x); fp_hoist = fp_checked = fp_guarded = 1; fcache_active = 0; fp_lazy_active = 0; owe_gen = 1;
    settle_fn = cp;
    strx(T2, ENV, OFF(fs_lr));                                              // the return address: the eager forms use x30
    E(0x39400000u | (OFF(fs_kind) + 1) << 10 | ENV << 5 | T1); uint32_t *nov = cp; E(0x34000000 | T1);   // FPCC since?
    ldrw(T1, ENV, OFF(fpscr)); logi(ANDi, ANDw, T1, T1, 0xF000); logi(ORRi, ORRw, T1, T1, 1);
    fix(nov, cp); strw(T1, ENV, OFF(fs_save));                              // 0, or that FPCC | 1
    E(0x39400000u | OFF(fs_kind) << 10 | ENV << 5 | T0);                    // ldrb w16: the kind
    E(0x10000000u | 3u << 5 | T2); E(0x8B000000 | T0 << 16 | 2u << 10 | T2 << 5 | T2); E(0xD61F0000 | T2 << 5);   // adr x30, table; add; br
    uint32_t *table = cp; cp += 64;
    settle_fail = cp; movw(T0, 1); ldrx(T2, ENV, OFF(fs_lr)); E(0xD65F03C0);
    uint32_t *ok = cp;
    ldrw(T0, ENV, OFF(fs_save)); uint32_t *plain = cp; E(0x34000000 | T0);
    ldrw(T1, ENV, OFF(fpscr)); logi(ANDi, ANDw, T0, T0, 0xF000); logi(ANDi, ANDw, T1, T1, ~0xF000u); op3(ORRw, T1, T1, T0); strw(T1, ENV, OFF(fpscr));
    fix(plain, cp); movw(T0, 0); ldrx(T2, ENV, OFF(fs_lr)); E(0xD65F03C0);
    for (unsigned k = 0; k < 64; k++) b_at(table + k, settle_fail);
    for (unsigned sgl = 0; sgl < 2; sgl++) for (unsigned i = 0; i < 8; i++) {
        if (XO[i] >= 28 && !fma_fast) continue;                             // never recorded (fp_arith)
        b_at(table + (sgl << 5 | XO[i]), cp);
        uint32_t in = (sgl ? 59u : 63u) << 26 | 3u << 21 | 0u << 16 | 1u << 11 | 2u << 6 | (uint32_t)XO[i] << 1;
        if (XO[i] < 28) fp_fast(in, 0, (int)sgl); else fp_fma_fast(in, 0, (int)sgl);
        b_to(ok);
    }
    __builtin___clear_cache((char *)settle_fn, (char *)cp);
    code0 = cp;
    fpr_at = at; fp_hoist = hoist; fp_checked = checked; fp_guarded = guarded; fcache_active = cache; fp_lazy_active = lazy; owe_gen = 0;
}
static int fp_xform(uint32_t in, uint32_t pc) {                // op 63: fcmpu fcmpo frsp fctiw fctiwz fsel
    int xo = in >> 1 & 0x3ff, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31;
    if ((in >> 1 & 31) == 23) {                                                        // fsel: no FPSCR
        if (in & 1) return -1;
        fp_check(pc); fld(0, a); fld(1, in >> 6 & 31); fld(2, b);
        E(0x1E602008); E(0x1E600C00 | 2u << 16 | (uint32_t)GE << 12 | 1u << 5 | 3); fst(3, d); return 0;   // fcmp d0, #0; fcsel d3, d1, d2, ge
    }
    if (xo == 0 || xo == 32) {                                                         // fcmpu/fcmpo without NaN: CR and FPCC
        fp_check(pc); nfsl = 0; fld(0, a); fld(1, b); E(0x1E602000 | 1u << 16 | 0u << 5); fslow(0x54000000 | VS);
        cset(T0, MI); cset(T1, GT); cset(T2, EQ);
        E(ORRw | T0 << 16 | 1u << 10 | T1 << 5 | T0); E(ORRw | T0 << 16 | 1u << 10 | T2 << 5 | T0); ubfm(T0, T0, 31, 30);
        ldrw(T1, ENV, OFF(fpscr)); bfm(T1, T0, 20, 3); strw(T1, ENV, OFF(fpscr));
        if (fp_lazy_enabled) E(0x39000000u | (OFF(fs_kind) + 1) << 10 | ENV << 5 | T0);   // strb: this FPCC outlives an owed status
        int f = d >> 2; E(0x8B000000 | T0 << 16 | 3u << 10 | ENV << 5 | T2); ldrx(f ? T1 : H_CR0, T2, OFF(crtab)); if (f) strx(T1, ENV, OCR(f));
        uint32_t *over = cp; E(0x14000000); fix(fsl[0], cp); exit_why(pc, X_INTERP); b_at(over, cp);
        return 0;
    }
    if (in & 1 || (xo != 12 && xo != 14 && xo != 15)) return -1;
    fp_check(pc); nfsl = 0;
    if (xo != 12) fp_settle_site(pc);                          // fctiw(z) leaves FPRF: the owed status first
    if (!fp_old && fp_round_fast && xo != 12) {
        /* Explicit FRINT modes keep host FPCR=0 for VMX and error-free
         * transforms, while accepting all PPC RN modes for fctiw(z). */
        uint32_t f; ldrw(T0, ENV, OFF(fpscr)); bm32(0xfc, &f);
        E(ANDSi | f | T0 << 5 | ZR); fslow(0x54000000 | NE);
        fld(0, b);
        if (xo == 15) E(0x1e65c001u); /* frintz d1,d0 */
        else {
            E(0x1e644001u); ubfm(T1, T0, 0, 1); /* nearest, or select RN=1/2/3 */
            uint32_t *rn0 = cp; E(0x34000000u | T1);
            uint32_t *rn1 = cp; E(0x36000000u | 1u << 19 | T1);
            uint32_t *rn2 = cp; E(0x36000000u | T1);
            E(0x1e654001u); uint32_t *rnm = cp; E(0x14000000u);
            fix(rn2, cp); E(0x1e64c001u); uint32_t *rnp = cp; E(0x14000000u);
            fix(rn1, cp); E(0x1e65c001u); fix(rn0, cp); b_at(rnm, cp); b_at(rnp, cp);
        }
        E(0xd2e83c00u | T1); E(0x9e670000u | T1 << 5 | 2); /* d2 = 2^31 */
        E(0x1e602000u | 2u << 16 | 1u << 5); fslow(0x54000000u | GE);
        E(0x1e614000u | 2u << 5 | 2);
        E(0x1e602000u | 2u << 16 | 1u << 5); fslow(0x54000000u | LT); /* includes NaN */
        E(0x1e78003eu); /* fcvtzs w30,d1; range already checked */
        E(0x1e602000u | 1u << 16); cset(T1, NE); ubfm(T1, T1, 28, 27);
        if (fp_fr_on && xo == 14) {   /* FR = |rounded| > |b|; w30 holds the integer: a branch, not cset */
            E(0x1E60C000 | 1u << 5 | 6); E(0x1E60C000 | 0u << 5 | 7); E(0x1E602000 | 7u << 16 | 6u << 5);
            uint32_t *le = cp; E(0x54000000 | LE); logi(ORRi, ORRw, T1, T1, 0x20); fix(le, cp);
        }
        E(0xB24D3000u | T2 << 5 | T2); fbits_store(d, T2);
        fp_done(pc, 0, 0, 0); return 0;
    }
    fp_guard(); fld(0, b);
    if (xo == 12 && !fp_old && fp_round_fast) {
        /* frsp: one rounding and a value comparison, no serial FPSR access.
         * Tiny nonzero inputs/overflow/NaN retain the exception-aware path. */
        E(0x1e602000u); fslow(0x54000000u | VS);
        E(0x1e60c001u); E(0x1e602028u); uint32_t *zero = cp; E(0x54000000u | EQ);
        E(0xd2e70200u | T1); E(0x9e670000u | T1 << 5 | 2); /* 2^-126 */
        E(0x1e602000u | 2u << 16 | 1u << 5); fslow(0x54000000u | CC);
        fix(zero, cp); E(0x1e624003u); fexp(1, 3, 0); E(0x1e22c063u);
        E(0x1e602000u | 3u << 16); cset(T1, NE); ubfm(T1, T1, 28, 27);
        if (fp_fr_on) { E(0x1E60C000 | 3u << 5 | 6); E(0x1E60C000 | 0u << 5 | 7); E(0x1E602000 | 7u << 16 | 6u << 5);   // |d3| > |d0|
                        cset(T2, GT); E(ORRw | T2 << 16 | 5u << 10 | T1 << 5 | T1); }
        fst(3, d); fp_done_kind(pc, 1, 1, 3, 2); return 0;
    }
    if (xo == 12) { E(0x1E602000 | 0u << 16 | 0u << 5); fslow(0x54000000 | VS); fpsr_zero(); E(0x1E624000 | 0u << 5 | 3); fpsr_ok();   // frsp
                    fr_conv(0); E(0x1E22C000 | 3u << 5 | 3); fst(3, d); fp_done(pc, 1, 1, 3); return 0; }
    fpsr_zero(); E((xo == 15 ? 0x1E780000u : 0x1E600000u) | 0u << 5 | T2); fpsr_ok();             // fcvtzs / fcvtns w30, d0
    if (xo == 14) fr_inexact_slow();                                                               // fctiwz never rounds up
    E(0xB24D3000 | T2 << 5 | T2); fbits_store(d, T2); fp_done(pc, 0, 0, 0);      // | 0xFFF8...; FPRF unchanged
    return 0;
}
/* frsqrte exactly as the interpreter computes it (1/sqrt(b) rounded twice, finer than the G4 estimate): b > 0
 * (normal, denormal, +Inf) stays here with the FPSR flags, so FI/XX/FX/FPRF match; zero, negative, NaN (ZX, VXSQRT,
 * NaN rules) and a guarded FPSCR go to the interpreter untouched. stfiwx (in tr, op 31): the low FPR word.
 * Doom 3 timedemo: ~105k frsqrte and ~9k stfiwx interpreter exits per 5 s. Enabled by PT_FP_MORE=1. */
static int fp_more_enabled;
static int fp_rsqrte(uint32_t in, uint32_t pc) {
    if (in & 1) return -1;                                                     // frsqrte.: CR1 in the interpreter
    fp_check(pc); nfsl = 0; fp_guard(); fld(0, in >> 11 & 31);
    E(0x1E602008); fslow(0x54000000 | LE);                                     // fcmp d0, #0.0: <= 0 or NaN
    fpsr_zero(); E(0x1E61C001); E(0x1E6E1002); E(0x1E611843); fpsr_ok();       // fsqrt d1, d0; fmov d2, #1.0; fdiv d3, d2, d1
    fst(3, in >> 21 & 31); fp_done(pc, 1, 0, 3);
    return 0;
}
static int plain_spr(int spr) {                // the SPR is a plain word in CPU (read and write have no side effects)
    return spr == 18 || spr == 19 || spr == 26 || spr == 27 || (spr >= 272 && spr <= 275)    // DSISR DAR SRR0 SRR1 SPRG0-3
        || ((spr == 256 || spr == 1014) && vm_vec);   // VRSAVE (G4, user); MSSCR0 (G4: its idle loop made 26k+26k exits per 5 s)
}
static int plain_rd(int spr) {                 // SPRs whose read is a plain word in CPU (a write has side effects)
    return plain_spr(spr) || spr == 25 || spr == 287 || spr == 1008 || spr == 1009 || spr == 1017 || (spr >= 528 && spr <= 543);
}
/* VMX scratch v0-v24 does not overlap the write-through FPR cache v25-v31. */
static int vmx_neon_enabled, vmx_mem_enabled, vmx_vscr_enabled, vmx_int2_enabled, vmx_mhadd_enabled;
static void vmx_check(uint32_t pc) {
    if (jsys) { ldrw(T1, ENV, OFF(msr)); uint32_t *br = cp;
        E(0x37000000u | 25u << 19 | T1); exit_why(pc, X_INTERP); fix(br, cp); }
}
static int vmx_sat(uint32_t in, uint32_t pc) {
    if (!vmx_int2_enabled) return -1;
    unsigned xo = in & 2047, lane = xo >> 6 & 3, family = xo & ~0xc0u;
    if (lane > 2 || (family != 0x200 && family != 0x300 && family != 0x600 && family != 0x700)) return -1;
    unsigned d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31;
    vmx_check(pc);
    E(0x3dc00000u | (OFF(vr) / 16 + a) << 10 | ENV << 5);
    E(0x3dc00001u | (OFF(vr) / 16 + b) << 10 | ENV << 5);
    uint32_t reverse = lane == 1 ? 0x4e201800u : lane == 2 ? 0x6e200800u : 0;
    if (reverse) { E(reverse); E(reverse | 1u << 5 | 1); }
    E((family & 0x400 ? 0x6e218402u : 0x4e218402u) | lane << 22); /* wrapping result */
    uint32_t op = family & 0x100 ? 0x4e210c00u : 0x6e210c00u;
    E(op | (family & 0x400 ? 0x2000u : 0) | lane << 22);
    /* QC is not consulted: SAT is sticky iff clamped differs from wrapping.
     * Scalar FP's FPSR paths clear FPSR before use. */
    E(0x6e228c02u); E(0x6e205842u); E(0x6e30a842u); E(0x0e013c50u);
    E(0x7100001fu | T0 << 5); cset(T0, NE);
    ldrw(T1, ENV, OFF(vscr)); op3(ORRw, T1, T1, T0); strw(T1, ENV, OFF(vscr));
    if (reverse) E(reverse);
    E(0x3d800000u | (OFF(vr) / 16 + d) << 10 | ENV << 5);
    return 0;
}
/* vmhaddshs/vmhraddshs (VA 32/33), as the interpreter: sat16(((a*b [+0x4000]) >> 15) + c) per halfword, VSCR.SAT
 * when any lane clamps. Exact 32-bit products (smull) -> no double saturation. Tiger's Dock magnification ran ~360k
 * interpreter exits/s on these. v0-v5 scratch. Enabled by PT_VMX_MHADD=1. */
static int vmx_mhadd(uint32_t in, uint32_t pc) {
    unsigned d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, c = in >> 6 & 31, q = OFF(vr) / 16;
    uint32_t shr = in & 1 ? 0x4f312400u : 0x4f310400u;                     /* srshr / sshr .4s, #15 */
    vmx_check(pc);
    E(0x3dc00000u | (q + a) << 10 | ENV << 5); E(0x3dc00001u | (q + b) << 10 | ENV << 5); E(0x3dc00002u | (q + c) << 10 | ENV << 5);
    E(0x4e201800u); E(0x4e201821u); E(0x4e201842u);                          /* rev16: big-endian halfwords -> host */
    E(0x0e61c003u); E(0x4e61c004u);                                          /* smull v3.4s, v0.4h, v1.4h; smull2 v4.4s, v0.8h, v1.8h */
    E(shr | 3u << 5 | 3); E(shr | 4u << 5 | 4);
    E(0x0e621063u); E(0x4e621084u);                                          /* saddw v3.4s, v3.4s, v2.4h; saddw2 v4.4s, v4.4s, v2.8h */
    E(0x0e614860u); E(0x4e614880u);                                          /* sqxtn v0.4h, v3.4s; sqxtn2 v0.8h, v4.4s */
    E(0x0e612865u); E(0x4e612885u);                                          /* xtn v5.4h, v3.4s; xtn2 v5.8h, v4.4s */
    E(0x6e208ca5u); E(0x6e2058a5u); E(0x6e30a8a5u); E(0x0e013cb0u);         /* cmeq, not, umaxv b5, umov w16, v5.b[0] */
    E(0x7100001fu | T0 << 5); cset(T0, NE);
    ldrw(T1, ENV, OFF(vscr)); op3(ORRw, T1, T1, T0); strw(T1, ENV, OFF(vscr));
    E(0x4e201800u); E(0x3d800000u | (q + d) << 10 | ENV << 5);
    return 0;
}
/* vmladduhm (VA 34: a*b + c per halfword, modulo 2^16) and vsl/vsr (VX 0x1c4/0x2c4: the whole register shifted by
 * vB[15] & 7 bits), as the interpreter. Tiger's Quartz path, desktop idle with Xbench open: 1.7M vmladduhm in the
 * interpreter, which also took the translatable VMX next to them (interpreter batch). v0-v6 scratch. */
static int vmx_mladd_shift(uint32_t in, uint32_t pc) {
    unsigned d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, c = in >> 6 & 31, q = OFF(vr) / 16;
    vmx_check(pc);
    E(0x3dc00000u | (q + a) << 10 | ENV << 5); E(0x3dc00001u | (q + b) << 10 | ENV << 5);         /* ldr q0, VR[a]; ldr q1, VR[b] */
    if ((in & 63) == 34) {
        E(0x3dc00002u | (q + c) << 10 | ENV << 5);                                                /* ldr q2, VR[c] */
        E(0x4e201800u); E(0x4e201821u); E(0x4e201842u);                                           /* rev16: big-endian halfwords -> host */
        E(0x4e619402u); E(0x4e201842u);                                                           /* mla v2.8h, v0.8h, v1.8h; rev16 */
        E(0x3d800002u | (q + d) << 10 | ENV << 5); return 0;
    }
    int left = (in & 2047) == 0x1c4;
    E(0x0e1f3c30u); logi(ANDi, ANDw, T0, T0, 7);                                                  /* w16 = vB byte 15 & 7 */
    if (left) E(0x51002000u | T0 << 5 | T1);                                                      /* w17 = sh - 8 */
    else { movw(T1, 8); E(0x4b000000u | T0 << 16 | T1 << 5 | T1); E(0x4b0003e0u | T0 << 16 | T0); }   /* w17 = 8 - sh; w16 = -sh */
    E(0x4e010c02u | T0 << 5); E(0x4e010c06u | T1 << 5); E(0x6f00e404u);                          /* dup v2, w16; dup v6, w17; v4 = 0 */
    E(left ? 0x6e040803u : 0x6e007883u);                                                          /* ext: v3[i] = a[i+1] / a[i-1] */
    E(0x6e224405u); E(0x6e264463u); E(0x4ea31ca5u);                                               /* ushl v5, v0, v2; ushl v3, v3, v6; orr */
    E(0x3d800005u | (q + d) << 10 | ENV << 5);
    return 0;
}
/* Saturating packs vpkuhus vpkuwus vpkshus vpkswus vpkshss vpkswss (VX 0x08e-0x1ce, PT_VMX_INT2), as the
 * interpreter: narrow a into the low half and b into the high half with saturation (uqxtn/sqxtun/sqxtn); VSCR.SAT
 * when widening the result back changes any element. Dock magnification hits vpkshus after vmhraddshs. */
static int vmx_pack(uint32_t in, uint32_t pc) {
    unsigned xo = in & 2047, w = xo >> 6 & 1, mode = xo & ~0x40u, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, q = OFF(vr) / 16;
    uint32_t rev = w ? 0x6e200800u : 0x4e201800u;                                       /* rev32 / rev16 */
    uint32_t nar = (mode == 0x08e ? 0x2e214800u : mode == 0x10e ? 0x2e212800u : 0x0e214800u) | w << 22;
    uint32_t wid = (mode == 0x18e ? 0x0f00a400u : 0x2f00a400u) | (w ? 0x00100000u : 0x00080000u);   /* sshll / ushll #0 */
    uint32_t eq = 0x6e208c00u | (w ? 0x00800000u : 0x00400000u);                        /* cmeq .4s / .8h */
    vmx_check(pc);
    E(0x3dc00000u | (q + a) << 10 | ENV << 5); E(0x3dc00001u | (q + b) << 10 | ENV << 5);
    E(rev); E(rev | 1u << 5 | 1);
    E(nar | 2); E(nar | 0x40000000u | 1u << 5 | 2);
    E(wid | 2u << 5 | 3); E(wid | 0x40000000u | 2u << 5 | 4);
    E(eq | 3u << 5 | 3); E(eq | 1u << 16 | 4u << 5 | 4);
    E(0x4e241c63u); E(0x6e205863u); E(0x6e30a863u); E(0x0e013c70u);                     /* and, not, umaxv b3, umov w16 */
    E(0x7100001fu | T0 << 5); cset(T0, NE);
    ldrw(T1, ENV, OFF(vscr)); op3(ORRw, T1, T1, T0); strw(T1, ENV, OFF(vscr));
    if (w) E(0x4e201842u);                                                                /* halfword results to big-endian */
    E(0x3d800002u | (q + d) << 10 | ENV << 5);
    return 0;
}
static int vmx_neon(uint32_t in, uint32_t pc) {
    if (!vmx_neon_enabled || !(jmsr & MSR_VEC)) return -1;
    unsigned original = in & 2047, family = original & ~0xc0u;
    if (!vmx_int2_enabled && (
        (original & 63) == 14 || (original & 63) == 8 ||
        family == 0x402 || family == 0x502 ||
        ((original & 63) == 6 && family != 0x006))) return -1;
    if (!vmx_sat(in, pc)) return 0;
    uint32_t words[PT_VMX_NEON_WORDS];
    unsigned n = pt_vmx_neon_emit(words, in, OFF(vr), ENV);
    if (!n) return -1;
    vmx_check(pc);
    for (unsigned i = 0; i < n; i++) E(words[i]);
    unsigned xo = in & 2047, cmp = xo & ~0x400u, cb = cmp & ~0xc0u;
    if ((xo & 0x400) && (cmp & 63) == 6 && ((cmp >> 6) & 3) < 3 && (cb == 6 || cb == 0x206 || cb == 0x306)) {
        E(0x6e31a801u); E(0x6e30a800u); /* min/max over canonical 0/ff byte masks */
        E(0x0e013c31u); E(0x0e013c10u);
        E(0x7100001fu | T1 << 5); cset(T1, NE); /* all true */
        E(0x7100001fu | T0 << 5); cset(T0, EQ); /* all false */
        E(ORRw | T1 << 16 | 2u << 10 | T0 << 5 | T0); /* all*4 + none */
        ubfm(T0, T0, 31, 30); /* CR6 = all*8 + none*2, GT=SO=0 */
        E(0x8b000000u | T0 << 16 | 3u << 10 | ENV << 5 | T2);
        ldrx(T0, T2, OFF(crtab)); strx(T0, ENV, OCR(6));
    }
    return 0;
}
_Static_assert(!(offsetof(CPU, vr) & 15) && offsetof(CPU, vr) + sizeof(((CPU *)0)->vr) <= 65536,
               "VMX state must fit scaled LDR/STR Q offsets");
/* vaddfp vsubfp vmaxfp vminfp vmaddfp vnmsubfp: one NEON op for 4 lanes. FPCR = 0 (round to nearest, no flush) and
 * AltiVec FP always rounds to nearest (FMA fused). Every lane runs in the JIT, as the interpreter computes it
 * (altivec.c vec_float): with VSCR.NJ, denormal inputs and results become signed zeros (mask: exponent != 0, or the
 * sign); a NaN input gives the first NaN of A, B, C (B for the add), quieted; a NaN from the operation gives 0x7fc00000.
 * Sending NaN or denormal lanes to the interpreter cost Tiger's OpenGL vertex path in Xbench's Spinning Squares 2.6M
 * exits per 5 s, taking its neighbours along. v16-v23 are temporaries. PT_VMX_FP=0 -> interpreter. */
static int vmx_fp_enabled;
static int vmx_fp(uint32_t in, uint32_t pc) {
    if (!vmx_neon_enabled || !vmx_fp_enabled || !(jmsr & MSR_VEC)) return -1;
    unsigned d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, c = in >> 6 & 31, q = OFF(vr) / 16, xo = in & 2047;
    unsigned fma = (in & 62) == 46, neg = (in & 63) == 47, n = fma ? 3 : 2;
    uint32_t op = fma ? 0x4e20cc00u : xo == 0x00a ? 0x4e20d400u : xo == 0x04a ? 0x4ea0d400u : xo == 0x40a ? 0x4e20f400u : 0x4ea0f400u;
    vmx_check(pc);
    ldrw(T1, ENV, OFF(vscr)); ubfm(T1, T1, 16, 16); E(0x4b1103f1u); ubfm(T1, T1, 1, 31); E(0x2a3103f1u);   // w17 = NJ ? 0x80000000 : ~0
    E(0x4e040c00u | T1 << 5 | 23); movw(T0, 0x7f800000); E(0x4e040c00u | T0 << 5 | 22);                  // v23 = keep, v22 = exponent
    const unsigned g[3] = {a, b, c};
    for (unsigned i = 0; i < n; i++) {
        unsigned x = 16 + i;
        E(0x3dc00000u | (q + g[i]) << 10 | ENV << 5 | x); E(0x6e200800u | x << 5 | x);                      // ldr qX, VR; rev32
        E(0x4ea08c00u | 22u << 16 | x << 5 | 19); E(0x4ea01c00u | 23u << 16 | 19u << 5 | 19);              // cmtst, orr keep
        E(0x4e201c00u | 19u << 16 | x << 5 | x);                                                         // and: NJ flush
    }
    if (!fma) E(op | 17u << 16 | 16u << 5 | 20);                                                         // v20 = a op b
    else {                                                                                               // vnmsubfp = -(a*c + -b)
        if (neg) E(0x6ea0f800u | 17u << 5 | 20); else E(0x4ea01c00u | 17u << 16 | 17u << 5 | 20);         // v20 = -b / b
        E(op | 18u << 16 | 16u << 5 | 20);                                                               // fmla v20, v16, v18
        if (neg) E(0x6ea0f800u | 20u << 5 | 20);
    }
    E(0x4ea08c00u | 22u << 16 | 20u << 5 | 19); E(0x4ea01c00u | 23u << 16 | 19u << 5 | 19); E(0x4e201c00u | 19u << 16 | 20u << 5 | 20);   // NJ flush
    movw(T0, 0x7fc00000); E(0x4e040c00u | T0 << 5 | 21);                                                 // v21 = default NaN
    E(0x4e20e400u | 20u << 16 | 20u << 5 | 19); E(0x6ee01c00u | 19u << 16 | 21u << 5 | 20);              // NaN result -> 0x7fc00000
    for (int i = (int)n - 1; i >= 0; i--) {                                                              // C, B, A: the first NaN wins
        unsigned x = 16u + (unsigned)i;                                                                  // A B C = v16 v17 v18
        E(0x4e20e400u | x << 16 | x << 5 | 19); E(0x4f025400u | x); E(0x6ee01c00u | 19u << 16 | x << 5 | 20);   // fcmeq; orr #0x400000; bif
    }
    E(0x6e200800u | 20u << 5 | 20); E(0x3d800000u | (q + d) << 10 | ENV << 5 | 20);                       // rev32; str q20, VR[D]
    return 0;
}
/* vcfux vcfsx (0x30a 0x34a): vB's words converted and divided by 2^UIMM, rounded once (ucvtf/scvtf with UIMM
 * fraction bits), as the interpreter's (float)x scaled exactly; never denormal. Tiger's OpenGL vertex path,
 * Spinning Squares: 330k interpreter exits per 5 s. */
static int vmx_cvt(uint32_t in, uint32_t pc) {
    if (!vmx_neon_enabled || !vmx_fp_enabled || !(jmsr & MSR_VEC)) return -1;
    unsigned d = in >> 21 & 31, k = in >> 16 & 31, b = in >> 11 & 31, q = OFF(vr) / 16, sgn = (in & 2047) == 0x34a;
    vmx_check(pc);
    E(0x3dc00000u | (q + b) << 10 | ENV << 5 | 17); E(0x6e200800u | 17u << 5 | 17);                      // ldr q17, VR[b]; rev32
    if (k) E((sgn ? 0x4f00e400u : 0x6f00e400u) | (64 - k) << 16 | 17u << 5 | 17);                        // [us]cvtf v17.4s, #k
    else E((sgn ? 0x4e21d800u : 0x6e21d800u) | 17u << 5 | 17);
    E(0x6e200800u | 17u << 5 | 17); E(0x3d800000u | (q + d) << 10 | ENV << 5 | 17);
    return 0;
}
/* vcmpeqfp vcmpgefp vcmpgtfp (and the Rc forms: CR6) and vrsqrtefp as the interpreter computes them (1/sqrtf
 * rounded twice, finer than the G4 estimate). A denormal input lane goes to the interpreter (VSCR.NJ decides its value
 * there); otherwise NEON compares and fsqrt/fdiv give the interpreter's bits for zero, normal, infinite and NaN lanes
 * (FPCR = 0). Doom 3 (idSIMD AltiVec) timedemo: ~200k interpreter exits per 5 s. Enabled by PT_VMX_FP2=1.
 * Also vrefp (1.0f/b, as the interpreter: finer than the G4 estimate) and vrfin/vrfiz/vrfip/vrfim (frintn/z/p/m:
 * exact; a NaN keeps its payload, quieted, as the interpreter). A denormal result lane of vrefp (|b| > 2^126) also
 * goes to the interpreter (VSCR.NJ). Doom 3: vrefp 74k, vrfim 32k exits per 5 s. */
static int vmx_fp2_enabled;
static int vmx_fp2(uint32_t in, uint32_t pc) {
    unsigned xo = in & 2047, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, q = OFF(vr) / 16, one = (xo & 63) == 10;
    if (!vmx_neon_enabled || !vmx_fp2_enabled || !(jmsr & MSR_VEC)) return -1;
    vmx_check(pc);
    E(0x3dc00000u | (q + b) << 10 | ENV << 5 | 17); E(0x6e200800u | 17u << 5 | 17);              // ldr q17, VR[b]; rev32
    E(0x4f044413u);                                                                            // movi v19.4s: 2^-126
    E(0x6ea0ec00u | 17u << 16 | 19u << 5 | 20); E(0x4ea0d800u | 17u << 5 | 21);                // |b| < 2^-126, b == 0
    E(0x4e601c00u | 21u << 16 | 20u << 5 | 20);                                                // v20 = denormal lanes of b
    if (!one) {
        E(0x3dc00000u | (q + a) << 10 | ENV << 5 | 16); E(0x6e200800u | 16u << 5 | 16);          // ldr q16, VR[a]; rev32
        E(0x6ea0ec00u | 16u << 16 | 19u << 5 | 22); E(0x4ea0d800u | 16u << 5 | 21);
        E(0x4e601c00u | 21u << 16 | 22u << 5 | 22); E(0x4ea01c00u | 22u << 16 | 20u << 5 | 20); // | denormal lanes of a
        E(((xo & 0x3ff) == 0x0c6 ? 0x4e20e400u : (xo & 0x3ff) == 0x1c6 ? 0x6e20e400u : 0x6ea0e400u) | 17u << 16 | 16u << 5);   // fcmeq/fcmge/fcmgt v0, a, b
    } else if (xo == 0x14a) { E(0x6ea1f800u | 17u << 5 | 18); E(0x4f03f600u | 22); E(0x6e20fc00u | 18u << 16 | 22u << 5); }   // v0 = 1/sqrt(b)
    else if (xo == 0x10a) {                                                                    // vrefp: v0 = 1/b
        E(0x4f03f600u | 22); E(0x6e20fc00u | 17u << 16 | 22u << 5);
        E(0x6ea0ec00u | 0u << 16 | 19u << 5 | 21); E(0x4ea0d800u | 0u << 5 | 22);                // |v0| < 2^-126, v0 == 0
        E(0x4e601c00u | 22u << 16 | 21u << 5 | 21); E(0x4ea01c00u | 21u << 16 | 20u << 5 | 20); // | denormal result lanes
    } else E((xo == 0x20a ? 0x4e218800u : xo == 0x24a ? 0x4ea19800u : xo == 0x28a ? 0x4ea18800u : 0x4e219800u) | 17u << 5);   // frintn/z/p/m v0, b
    E(0x6eb0a800u | 20u << 5 | 20); E(0x1e260000u | 20u << 5 | T1);                           // umaxv s20; fmov w17, s20
    uint32_t *ok = cp; E(0x34000000u | T1); exit_why(pc, X_INTERP); fix(ok, cp);               // a denormal lane: interpreter
    if (one) E(0x6e200800u);                                                                   // rev32 v0: BE lanes
    E(0x3d800000u | (q + d) << 10 | ENV << 5);                                                 // str q0, VR[d]
    if (xo & 0x400) {                                                                          // Rc: CR6 = all*8 + none*2, as the integer compares
        E(0x6e31a801u); E(0x6e30a800u); E(0x0e013c31u); E(0x0e013c10u);
        E(0x7100001fu | T1 << 5); cset(T1, NE); E(0x7100001fu | T0 << 5); cset(T0, EQ);
        E(ORRw | T1 << 16 | 2u << 10 | T0 << 5 | T0); ubfm(T0, T0, 31, 30);
        E(0x8b000000u | T0 << 16 | 3u << 10 | ENV << 5 | T2); ldrx(T0, T2, OFF(crtab)); strx(T0, ENV, OCR(6));
    }
    return 0;
}
static int tr(uint32_t in, uint32_t pc) {
    int op = in >> 26, D = in >> 21 & 31, A = in >> 16 & 31, B = in >> 11 & 31, rc = in & 1;
    int32_t simm = (int16_t)in; uint32_t uimm = in & 0xffff;
    static const uint8_t LKIND[16] = {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 0, 0};
    switch (op) {
    case 4:
        if (((in & 2047) == 0x604 || (in & 2047) == 0x644) && jmsr & MSR_VEC && vmx_vscr_enabled) {   // mfvscr/mtvscr (G4: the kernel and threads save VSCR)
            if (jsys) { ldrw(T1, ENV, OFF(msr)); uint32_t *br = cp; E(0x37000000u | 25u << 19 | T1); exit_why(pc, X_INTERP); fix(br, cp); }
            if ((in & 2047) == 0x604) { uint32_t vo = OFF(vr) + 16u * D;              // VR[D] = {0, 0, 0, VSCR}, BE
                strx(ZR, ENV, vo); strw(ZR, ENV, vo + 8); ldrw(T1, ENV, OFF(vscr)); E(REVw | T1 << 5 | T1); strw(T1, ENV, vo + 12); }
            else { ldrw(T0, ENV, OFF(vr) + 16u * B + 12); E(REVw | T0 << 5 | T0); logi(ANDi, ANDw, T0, T0, 0x10001); strw(T0, ENV, OFF(vscr)); }
            return 0;
        }
        if ((in & 62) == 46 || (in & 2047 & ~0x440u) == 0x00a) return vmx_fp(in, pc);   // vmaddfp vnmsubfp vaddfp vsubfp vmaxfp vminfp
        if ((in & 62) == 32) return vmx_neon_enabled && vmx_mhadd_enabled && jmsr & MSR_VEC ? vmx_mhadd(in, pc) : -1;   // vmhaddshs vmhraddshs
        if ((in & 63) == 34 || (in & 2047) == 0x1c4 || (in & 2047) == 0x2c4)                    // vmladduhm vsl vsr
            return vmx_neon_enabled && jmsr & MSR_VEC ? vmx_mladd_shift(in, pc) : -1;
        if (((in & 2047) & ~0x1c0u) == 0x00e && (in & 2047) >= 0x08e)                  // saturating packs
            return vmx_neon_enabled && vmx_int2_enabled && jmsr & MSR_VEC ? vmx_pack(in, pc) : -1;
        if ((in & 2047) == 0x30a || (in & 2047) == 0x34a) return vmx_cvt(in, pc);              // vcfux vcfsx
        if ((in & 0x3ff) == 0x0c6 || (in & 0x3ff) == 0x1c6 || (in & 0x3ff) == 0x2c6 || (in & 2047) == 0x14a || (in & 2047) == 0x10a ||
            ((in & 2047) >= 0x20a && (in & 2047) <= 0x2ca && (in & 63) == 10)) return vmx_fp2(in, pc);   // vcmp*fp (.) vrsqrtefp vrefp vrfi*
        return vmx_neon(in, pc);
    case 14: { int d = dst(D); if (!A) movw(d, (uint32_t)simm); else addi32(d, src(A, T0), simm); put(D, d); return 0; }
    case 15: { int d = dst(D); if (!A) movw(d, uimm << 16); else addi32(d, src(A, T0), (int32_t)(uimm << 16)); put(D, d); return 0; }
    case 7:  { int d = dst(D), a = src(A, T0); movw(T1, (uint32_t)simm); op3(MULw, d, a, T1); put(D, d); return 0; }
    case 8:  { int d = dst(D), a = src(A, T0); movw(T1, (uint32_t)simm); op3(SUBSw, d, T1, a); set_ca(); put(D, d); return 0; }
    case 12: case 13: { int d = dst(D), a = src(A, T0); movw(T1, (uint32_t)simm); op3(ADDSw, d, a, T1); set_ca();
                        if (op == 13) rec(d); put(D, d); return 0; }
    case 11: case 10: {                                                         // cmpi / cmpli
        if (D & 1) return -1;
        int f = D >> 2, v = f ? T0 : H_CR0, a = src(A, T0);
        if (try_fuse(f, op == 11, a, -1, op == 11 ? simm : (int32_t)uimm, pc)) return 0;
        if (next_kills(pc, f)) return 0;
        if (op == 10) { subimm64(v, a, uimm, 0); if (f) strx(v, ENV, OCR(f)); return 0; }
        if (op == 11) { sxtw(v, a); if (!simm) ; else if (simm > 0 && simm < 4096) E(0xD1000000 | (uint32_t)simm << 10 | v << 5 | v);
                        else if (simm < 0 && simm > -4096) E(0x91000000 | (uint32_t)-simm << 10 | v << 5 | v);
                        else { movw(T1, (uint32_t)simm); E(0xCB20C000 | T1 << 16 | v << 5 | v); } }
        else { mov32(v, a); if (!uimm) ; else if (uimm < 4096) E(0xD1000000 | uimm << 10 | v << 5 | v); else { movw(T1, uimm); E(0xCB000000 | T1 << 16 | v << 5 | v); } }
        if (f) strx(v, ENV, OCR(f));
        return 0;
    }
    case 18: { uint32_t t = (in & 2 ? 0 : pc) + (uint32_t)((int32_t)(in << 6) >> 6 & ~3);
               if (t == pc && !(in & 1)) return -1;                                 // `b .`: bare metal stops in the interpreter
               if (in & 1) movw(H_LR, pc + 4); exit_to(t); return 1; }
    case 16: {
        int bo = D, bi = A; uint32_t t = (in & 2 ? 0 : pc) + (uint32_t)(int32_t)(int16_t)(in & 0xfffc);
        if (rc) movw(H_LR, pc + 4);
        if ((bo & 0x14) == 0x14) { exit_to(t); return 1; }
        uint32_t *br;
        if (!(bo & 4) && !(bo & 16)) {                                          // bdnzt/bdnzf...: CTR and a CR bit
            E(0x51000400 | H_CTR << 5 | H_CTR); uint32_t *nt = cp; E((bo & 2 ? 0x35000000u : 0x34000000u) | H_CTR);   // CTR condition fails: not taken
            br = br_crbit(bi, bo >> 3 & 1); fix(nt, cp);
        }
        else if (!(bo & 4)) { E(0x51000400 | H_CTR << 5 | H_CTR); br = cp; E((bo & 2 ? 0x34000000u : 0x35000000u) | H_CTR); }
        else br = br_crbit(bi, bo >> 3 & 1);
        if (t == blk_pc0 && !check && !nolink) { fix(br, loop_start); return 0; }   // loop back edge
        if (nside == 128 || check) { exit_to(pc + 4); fix(br, cp); exit_to(t); return 1; }
        side[nside].br = br; side[nside].target = t; side[nside++].mf = -1;  // superblock: continue along the not-taken path
        return 0;
    }
    case 19: {
        int xo = in >> 1 & 0x3ff, bo = D, bi = A;
        if (xo == 150) {                                                        // isync after icbi (entries pending): to the interpreter
            ldrw(T0, ENV, OFF(icbi_n)); uint32_t *br = cp; E(0x34000000 | T0);  // cbz w16, continue
            exit_why(pc, X_INTERP); fix(br, cp); return 0;
        }
        if (xo == 50 && (jsys == 2 || (jsys && vm_rfi == 2)) && jit_vm_buf && !kpr && vm_rfi) {                     // rfi in the VM: MSR <- SRR1, jump to SRR0 with the mode's TTBR
            ldrw(T0, ENV, OFF(icbi_n)); uint32_t *s1 = cp; E(0x35000000 | T0);                   // icbi pending: interpreter
            ldrw(T1, ENV, OFF(spr) + 4u * SPR_SRR1); movw(T2, jmsr & ~(uint32_t)MSR_POW); op3(ANDw, T1, T1, T2);
            ldrw(T2, ENV, OFF(msr)); op3(EORw, T2, T2, T1);
            movw(T0, ~(uint32_t)(MSR_EE | MSR_FP | MSR_ME | MSR_FE0 | MSR_FE1 | MSR_RI | MSR_IP | MSR_PMM | MSR_IR | MSR_DR | MSR_PR | vm_vec));
            E(0x6A00001F | T0 << 16 | T2 << 5); uint32_t *s2 = cp; E(0x54000000 | NE);            // other bits (SE, LE...): interpreter
            op3(ANDw, T2, T2, T1); uint32_t *ok = cp; E(0x36000000 | 15u << 19 | T2);           // EE not turned on: continue
            ldrw(T0, ENV, OFF(pend_masked)); uint32_t *s3 = cp; E(0x35000000 | T0);              // an interrupt is waiting: interpreter
            fix(ok, cp);
            uint32_t *s5 = NULL; if (vm_rfi >= 3) { s5 = cp; E((vm_rfi == 3 ? 0x37000000u : 0x36000000u) | 14u << 19 | T1); }   // PR=1 / PR=0: interpreter
            uint32_t *rm = cp; E(0x36000000 | 4u << 19 | T1);                                    // DR=1: are SRs changed in real mode
            ldrw(T0, ENV, OFF(srgen)); ldrw(T2, ENV, OFF(srgen_ok)); E(0x6B00001F | T2 << 16 | T0 << 5);   // not yet in the tables? (sr_site, s4 below)
            if (srsw_fn) flush_cache_regs();                                    // the routine leaves them alone; the exit stores them again
            uint32_t *s4 = sr_site(); fix(rm, cp);
            strw(T1, ENV, OFF(msr)); ldrw(T0, ENV, OFF(spr) + 4u * SPR_SRR0); logi(ANDi, ANDw, T0, T0, ~3u);
            flush_cache_regs(); jump_dyn();
            /* s4 (the target's SRs were set in real mode and are not yet in the ARM tables; in Doom 3 these were 2.2M
             * of 2.2M rfi exits) finishes the rfi here and leaves for a lookup at SRR0: the host switches the segment
             * tables at the top of its loop and enters the target's block, without the interpreter batch. */
            fix(s4, cp); strw(T1, ENV, OFF(msr)); flush_cache_regs(); ldrw(T0, ENV, OFF(spr) + 4u * SPR_SRR0);
            logi(ANDi, ANDw, T0, T0, ~3u); strw(T0, ENV, OFF(pc)); movw(T1, X_LOOKUP); strw(T1, ENV, OFF(jit_exit)); b_to(exit_common);
            fix(s1, cp); fix(s2, cp); fix(s3, cp); if (s5) fix(s5, cp); exit_why(pc, X_INTERP);
            return 1;
        }
        if (xo == 0) {                                                          // mcrf
            int fs = A >> 2, fd = D >> 2, h = fs ? (ldrx(T0, ENV, OCR(fs)), T0) : H_CR0;
            if (fd) strx(h, ENV, OCR(fd)); else E(0xAA0003E0 | h << 16 | H_CR0);
            return 0;
        }
        if (xo == 257 || xo == 449 || xo == 193 || xo == 225 || xo == 33 || xo == 289 || xo == 129 || xo == 417) {   // CR logic
            crbit_to(T1, A); crbit_to(T2, B);
            switch (xo) {
            case 257: case 225: op3(ANDw, T1, T1, T2); break;  case 449: case 33: op3(ORRw, T1, T1, T2); break;
            case 193: case 289: op3(EORw, T1, T1, T2); break;  case 129: op3(BICw, T1, T1, T2); break;
            case 417: op3(ORNw, T1, T1, T2); E(0x12000000 | T1 << 5 | T1); break;                      // and #1
            }
            if (xo == 225 || xo == 33 || xo == 289) E(0x52000000 | T1 << 5 | T1);                       // eor #1
            strw(T1, ENV, OFF(jit_tmp));
            int fd = D >> 2, h = fd ? (ldrx(T0, ENV, OCR(fd)), T0) : H_CR0;                             // nibble of field D
            E(0x52800000 | T1);
            E(0xD37EFC00 | h << 5 | T2); bfm(T1, T2, 29, 0);
            E(0xF100001F | h << 5); cset(T2, GT); bfm(T1, T2, 30, 0);
            E(0x7100001F | h << 5); cset(T2, EQ); bfm(T1, T2, 31, 0);
            E(0xCA000000 | h << 16 | 2u << 10 | h << 5 | T2); E(0xD37FFC00 | T2 << 5 | T2); bfm(T1, T2, 0, 0);
            ldrw(T2, ENV, OFF(jit_tmp)); bfm(T1, T2, (32 - (3 - (D & 3))) & 31, 0);                  // the new bit
            E(0x8B000000 | T1 << 16 | 3u << 10 | ENV << 5 | T2);                                        // x30 = ENV + 8 * nibble
            ldrx(fd ? T0 : H_CR0, T2, OFF(crtab)); if (fd) strx(T0, ENV, OCR(fd));
            return 0;
        }
        if (xo != 16 && xo != 528) return -1;
        if (rc && !(bo & 16)) return -1;                                        // conditional bclrl: LR is written even when not taken
        if (!(bo & 4) && (!(bo & 16) || xo == 528)) return -1;                 // CTR together with CR; bcctr with CTR is invalid
        uint32_t *br;
        if (!(bo & 4)) { E(0x51000400 | H_CTR << 5 | H_CTR); br = cp; E((bo & 2 ? 0x35000000u : 0x34000000u) | H_CTR); }   // bdnzlr/bdzlr
        else br = (bo & 16) ? NULL : br_crbit(bi, !(bo >> 3 & 1));
        logi(ANDi, ANDw, T0, xo == 16 ? H_LR : H_CTR, ~3u);
        if (rc) movw(H_LR, pc + 4);
        exit_ind();
        if (br) { fix(br, cp); exit_to(pc + 4); }
        return 1;
    }
    case 17: {                                                                  // sc: on the machine, the 0xC00 exception directly in the VM
        if (!(jsys == 2 || (jsys && vm_sc == 2)) || !jit_vm_buf || !vm_sc || (vm_sc == 3 && kpr) || (vm_sc == 4 && !kpr)) { exit_why(pc + 4, X_SC); return 1; }
        ldrw(T0, ENV, OFF(icbi_n)); uint32_t *s1 = cp; E(0x35000000 | T0);
        ldrw(T1, ENV, OFF(msr)); uint32_t *s2 = cp; E(0x37000000 | 16u << 19 | T1);          // ILE: environment
        movw(T0, pc + 4); strw(T0, ENV, OFF(spr) + 4u * SPR_SRR0);
        movw(T2, 0xFF77 | (jmsr & MSR_VEC)); op3(ANDw, T2, T1, T2); strw(T2, ENV, OFF(spr) + 4u * SPR_SRR1);
        movw(T2, MSR_ME | MSR_IP); op3(ANDw, T1, T1, T2); strw(T1, ENV, OFF(msr));
        movw(T0, 0xC00); uint32_t *b = cp; E(0x36000000 | 6u << 19 | T1); movw(T0, 0xFFF00C00); fix(b, cp);   // IP: vector at 0xFFF00000
        flush_cache_regs(); jump_dyn();
        fix(s1, cp); fix(s2, cp); exit_why(pc + 4, X_SC);
        return 1;
    }
    case 21: {                                                                  // rlwinm
        int sh = B, mb = in >> 6 & 31, me = in >> 1 & 31, s = src(D, T0), d = dst(A); uint32_t m = ppc_mask(mb, me);
        int w = me - mb + 1, lsb = 31 - me;
        if (mb == 0 && me == 31 - sh) ubfm(d, s, (32 - sh) & 31, 31 - sh);      // slwi
        else if (mb <= me && me == 31 && sh && 32 - sh + w <= 32) ubfm(d, s, 32 - sh, 31 - sh + w);  // ubfx (srwi and field extract)
        else if (mb <= me && lsb == sh && sh) ubfm(d, s, (32 - lsb) & 31, w - 1);                   // ubfiz
        else if (!sh) { if (m == ~0u) mov32(d, s); else logi(ANDi, ANDw, d, s, m); }
        else { rorw(d, s, 32 - sh); if (m != ~0u) logi(ANDi, ANDw, d, d, m); }
        if (rc) rec(d); put(A, d); return 0;
    }
    case 20: {                                                                  // rlwimi
        int sh = B, mb = in >> 6 & 31, me = in >> 1 & 31, s = src(D, T0); uint32_t m = ppc_mask(mb, me);
        int hA = hreg(A), ra = hA >= 0 ? hA : (ldrw(T1, ENV, OGPR(A)), T1);
        if (hA >= 0 && GMAP[A] < 0) cwr |= 1u << A;
        if (m == ~0u) rorw(ra, s, 32 - sh);
        else if (mb <= me) { int lsb = 31 - me, w = me - mb + 1; rorw(T0, s, lsb - sh); bfm(ra, T0, (32 - lsb) & 31, w - 1); }
        else { rorw(T0, s, 32 - sh); uint32_t f1 = 0, f2 = 0; bm32(m, &f1); bm32(~m, &f2);
               E(ANDi | f1 | T0 << 5 | T0); E(ANDi | f2 | ra << 5 | ra); op3(ORRw, ra, ra, T0); }
        if (rc) rec(ra); if (ra == T1) strw(T1, ENV, OGPR(A)); return 0;
    }
    case 23: { int s = src(D, T0), b = src(B, T1), d = dst(A); uint32_t m = ppc_mask(in >> 6 & 31, in >> 1 & 31);   // rlwnm
               op3(SUBw, T1, ZR, b); op3(RORVw, d, s, T1); if (m != ~0u) logi(ANDi, ANDw, d, d, m);
               if (rc) rec(d); put(A, d); return 0; }
    case 24: case 25: case 26: case 27: case 28: case 29: {
        int s = src(D, T0), d = dst(A); uint32_t imm = op & 1 ? uimm << 16 : uimm;
        if (op >= 28) logi(ANDi, ANDw, d, s, imm);
        else if (!imm) mov32(d, s);
        else logi(op < 26 ? ORRi : EORi, op < 26 ? ORRw : EORw, d, s, imm);
        if (op >= 28) rec(d); put(A, d); return 0;
    }
    case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: {     // lfs lfd stfs stfd (and update forms)
        fp_check(pc); int e = ea_d(A, simm);
        if (op < 52) fload(op < 50, D, e); else fstore(op < 54, D, e, pc);
        if (op & 1) upd(A, e);
        return 0;
    }
    case 59: return fp_arith(in, pc, 1);                                        // single precision
    case 63: {                                                                  // fmr fneg fabs fnabs (without Rc they leave FPSCR alone)
        int xo = in >> 1 & 0x3ff;
        if (!rc && (xo == 583 || xo == 711 || xo == 134)) {                    // mffs mtfsf mtfsfi: as in the interpreter
            fp_check(pc);
            if (xo == 711 && (in >> 17 & 0x18) == 0x18) { if (fp_lazy_enabled) E(0x79000000u | (OFF(fs_kind) / 2) << 10 | ENV << 5 | ZR); }   // fields 3, 4: FR FI FPRF rewritten
            else fp_settle_site(pc);
            ldrw(T0, ENV, OFF(fpscr));
            if (xo == 583) { E(0xB24D3000 | T0 << 5 | T0); fbits_store(D, T0); return 0; }   // | 0xFFF8…
            fp_guarded = fp_xx = 0; /* mtfsf/mtfsfi can change RN, NI, exception enables and XX. */
            uint32_t m = 0, v = 0, f;
            if (xo == 711) { for (int i = 0; i < 8; i++) if (in >> 17 & 0x80 >> i) m |= 0xFu << (28 - 4 * i); }
            else { m = 0xFu << (28 - 4 * (D >> 2)); v = (B >> 1 & 15) << (28 - 4 * (D >> 2)); }
            m &= ~0x60000000u; movw(T1, m); op3(BICw, T0, T0, T1);                                          // FEX and VX are summary bits
            if (xo == 711) { ldrw(T2, ENV, OFF(fpr) + 8u * B); op3(ANDw, T2, T2, T1); op3(ORRw, T0, T0, T2); }
            else if (v & m) { movw(T2, v & m); op3(ORRw, T0, T0, T2); }
            logi(ANDi, ANDw, T0, T0, ~0x60000000u);                                                        // fp_summ:
            movw(T1, 0x01F80700); E(0x6A00001F | T1 << 16 | T0 << 5); cset(T1, NE); E(ORRw | T1 << 16 | 29u << 10 | T0 << 5 | T0);   // VX = |VX*
            ubfm(T1, T0, 22, 31); op3(ANDw, T1, T1, T0); bm32(0xF8, &f); E(ANDSi | f | T1 << 5 | ZR);      // FEX = |(exception & enable)
            cset(T1, NE); E(ORRw | T1 << 16 | 30u << 10 | T0 << 5 | T0); strw(T0, ENV, OFF(fpscr)); return 0;
        }
        if ((xo & 31) == 26 && fp_more_enabled) return fp_rsqrte(in, pc);             // frsqrte (PT_FP_MORE)
        if ((xo & 31) >= 18) return (xo & 31) == 23 ? fp_xform(in, pc) : fp_arith(in, pc, 0);   // A-form (fsel: no FPSCR)
        if (rc || (xo != 72 && xo != 40 && xo != 264 && xo != 136)) return fp_xform(in, pc);
        fp_check(pc); fsgl = 1; fbits_load(T1, B); fsgl = 0;                  // the file is exact: B may be single format
        if (xo == 40) E(0xD2410000 | T1 << 5 | T1); else if (xo == 264) E(0x9240F800 | T1 << 5 | T1); else if (xo == 136) E(0xB2410000 | T1 << 5 | T1);
        fbits_store(D, T1); return 0;
    }
    case 32: case 33: case 34: case 35: case 40: case 41: case 42: case 43: {
        static const uint8_t SZ[16] = {4, 4, 1, 1, 4, 4, 1, 1, 2, 2, 2, 2, 2, 2, 4, 4};
        Addr ad = addr_d(A, simm, SZ[op - 32], op & 1); load(LKIND[op - 32], D, ad); if (op & 1) upd(A, ad.base); return 0; }
    case 36: case 37: case 38: case 39: case 44: case 45: {
        static const uint8_t SZ[16] = {4, 4, 1, 1, 4, 4, 1, 1, 2, 2, 2, 2, 2, 2, 4, 4};
        Addr ad = addr_d(A, simm, SZ[op - 32], op & 1); store(LKIND[op - 32], D, ad); if (op & 1) upd(A, ad.base); return 0; }
    case 46: case 47: {                                                         // lmw/stmw
        int e = ea_d(A, simm); mov32(T0, e);
        for (int r = D; r < 32; r++) {
            Addr ad = zbase && (r - D) * 4 < 16384 ? (Addr){T0, (r - D) * 4} : (Addr){T0, 0};
            if (op == 46) load(0, r, ad); else store(0, r, ad);
            if (!zbase && r < 31) E(0x11001000 | T0 << 5 | T0);
        }
        return 0;
    }
    case 31: {
        int xo = in >> 1 & 0x3ff, xo9 = xo & 0x1ff;
        switch (xo9) {                                                          // XO-form arithmetic (no OE)
        case 266: case 40: case 104: case 235: case 75: case 11: case 491: case 459:
        case 10: case 8: case 138: case 136: case 202: case 200: case 234: case 232: {
            if (in >> 10 & 1) return -1;
            int needb = !(xo9 == 104 || xo9 == 202 || xo9 == 200 || xo9 == 234 || xo9 == 232);
            int a = src(A, T0), b = needb ? src(B, T1) : ZR, d = dst(D);
            int withca = xo9 == 138 || xo9 == 136 || xo9 == 202 || xo9 == 200 || xo9 == 234 || xo9 == 232;
            if (withca) { ldrw(T2, ENV, OFF(ca)); E(0x7100041F | T2 << 5); }       // C = CA
            if (xo9 == 234 || xo9 == 232) movw(T1, ~0u);
            switch (xo9) {
            case 266: op3(ADDw, d, a, b); break;            case 40:  op3(SUBw, d, b, a); break;
            case 104: op3(SUBw, d, ZR, a); break;           case 235: op3(MULw, d, a, b); break;
            case 75:  op3(SMULL, d, a, b); E(0xD360FC00 | d << 5 | d); break;
            case 11:  op3(UMULL, d, a, b); E(0xD360FC00 | d << 5 | d); break;
            case 491: op3(SDIVw, d, a, b); break;           case 459: op3(UDIVw, d, a, b); break;
            case 10:  op3(ADDSw, d, a, b); break;           case 8:   op3(SUBSw, d, b, a); break;
            case 138: op3(ADCSw, d, a, b); break;           case 136: op3(SBCSw, d, b, a); break;
            case 202: op3(ADCSw, d, a, ZR); break;          case 200: op3(SBCSw, d, ZR, a); break;
            case 234: op3(ADCSw, d, a, T1); break;          case 232: op3(SBCSw, d, T1, a); break;
            }
            if (withca || xo9 == 10 || xo9 == 8) set_ca();
            if (rc) rec(d); put(D, d); return 0;
        }
        }
        switch (xo) {
        case 0: case 32: {                                                      // cmp / cmpl
            if (D & 1) return -1;
            int f = D >> 2, v = f ? T0 : H_CR0, a = src(A, T0), b = src(B, T1);
            if (try_fuse(f, xo == 0, a, b, 0, pc)) return 0;
            if (next_kills(pc, f)) return 0;
            if (xo == 0) { sxtw(v, a); E(0xCB20C000 | b << 16 | v << 5 | v); }
            else E(0xCB000000 | b << 16 | a << 5 | v);
            if (f) strx(v, ENV, OCR(f));
            return 0;
        }
        case 28: case 60: case 444: case 412: case 316: case 476: case 124: case 284: case 24: case 536: {
            int s = src(D, T0), b = src(B, T1), d = dst(A);
            switch (xo) {
            case 28: op3(ANDw, d, s, b); break;   case 60: op3(BICw, d, s, b); break;
            case 444: if (D == B) mov32(d, s); else op3(ORRw, d, s, b); break;
            case 412: op3(ORNw, d, s, b); break;  case 316: op3(EORw, d, s, b); break;
            case 476: op3(ANDw, d, s, b); E(0x2A2003E0 | d << 16 | d); break;
            case 124: op3(ORRw, d, s, b); E(0x2A2003E0 | d << 16 | d); break;
            case 284: op3(EONw, d, s, b); break;
            case 24: case 536: logi(ANDi, ANDw, T1, b, 63); op3(xo == 24 ? LSLVx : LSRVx, T1, s, T1); mov32(d, T1); break;
            }
            if (rc) rec(d); put(A, d); return 0;
        }
        case 792: {                                                             // sraw: CA = (rS < 0) and lost bits
            int s = src(D, T0), b = src(B, T1);
            logi(ANDi, ANDw, T2, b, 63); sxtw(T0, s);
            E(0x9AC02800 | T2 << 16 | T0 << 5 | T1);                            // asrv x17, x16, x30
            E(0x9AC02000 | T2 << 16 | T1 << 5 | T2);                            // lslv x30, x17, x30
            E(0xEB00001F | T0 << 16 | T2 << 5); cset(T2, NE);                   // shifted back != original: bits were lost
            E(0x0A400000 | T0 << 16 | 31u << 10 | T2 << 5 | T2); strw(T2, ENV, OFF(ca));   // and w30, w30, w16, lsr #31
            if (rc) rec(T1);
            put(A, T1); return 0;
        }
        case 146: {                                                             // mtmsr: no exit unless the mode changes
            if (kpr) return -1;
            fp_checked = 0; /* MSR.FP can change without ending this block. */
            int s = src(D, T0);
            logi(ANDi, ANDw, T1, s, jmsr); ldrw(T2, ENV, OFF(msr)); op3(EORw, T2, T2, T1);
            movw(T0, ~(uint32_t)(MSR_EE | MSR_FP | MSR_ME | MSR_FE0 | MSR_FE1 | MSR_RI | MSR_IP | MSR_PMM | vm_vec));
            E(0x6A00001F | T0 << 16 | T2 << 5); uint32_t *b1 = cp; E(0x54000000 | NE);   // IR/DR/PR/POW/SE... changed
            strw(T1, ENV, OFF(msr));
            op3(ANDw, T2, T2, T1); uint32_t *b2 = cp; E(0x36000000 | 15u << 19 | T2);    // EE not turned on: continue
            uint32_t *b3 = NULL;                                                      // on the machine (sys=2) irq is raised for every interrupt:
            if (jsys == 2) { ldrw(T0, ENV, OFF(pend_masked)); b3 = cp; E(0x34000000 | T0); }   // does one wait for EE? The stand always exits
            movw(T1, 1); strw(T1, ENV, OFF(irq)); exit_why(pc + 4, X_MODE);
            fix(b1, cp);
            if ((jsys == 2 || (jsys && vm_msr == 2)) && jit_vm_buf && vm_msr) {                           // IR/DR/PR change in the VM: like rfi, to pc + 4 with the mode's TTBR
                movw(T0, ~(uint32_t)(MSR_EE | MSR_FP | MSR_ME | MSR_FE0 | MSR_FE1 | MSR_RI | MSR_IP | MSR_PMM | MSR_IR | MSR_DR | MSR_PR | vm_vec));
                E(0x6A00001F | T0 << 16 | T2 << 5); uint32_t *s1 = cp; E(0x54000000 | NE);   // POW, SE, LE...: environment
                ldrw(T0, ENV, OFF(icbi_n)); uint32_t *s2 = cp; E(0x35000000 | T0);
                op3(ANDw, T0, T2, T1); uint32_t *e1 = cp; E(0x36000000 | 15u << 19 | T0);
                ldrw(T0, ENV, OFF(pend_masked)); uint32_t *s3 = cp; E(0x35000000 | T0); fix(e1, cp);
                uint32_t *d1 = cp; E(0x36000000 | 4u << 19 | T1);                   // DR=1: SRs from real mode must be in the tables
                ldrw(T0, ENV, OFF(srgen)); ldrw(T2, ENV, OFF(srgen_ok)); E(0x6B00001F | T2 << 16 | T0 << 5);
                if (srsw_fn) flush_cache_regs();
                uint32_t *s4 = sr_site();
                fix(d1, cp); strw(T1, ENV, OFF(msr)); movw(T0, pc + 4); flush_cache_regs(); jump_dyn();
                fix(s1, cp); fix(s2, cp); fix(s3, cp); fix(s4, cp);
            }
            strw(T1, ENV, OFF(msr)); movw(T1, 1); strw(T1, ENV, OFF(irq)); exit_why(pc + 4, X_MODE);
            fix(b2, cp); if (b3) fix(b3, cp); return 0;
        }
        case 982: {                                                             // icbi: the previous icbi's page: nothing; otherwise the interpreter
            int e = ea_x(A, B); logi(ANDi, ANDw, T1, e, ~0xFFFu);
            ldrw(T2, ENV, OFF(msr)); ubfm(T2, T2, 4, 4); E(ORRw | T2 << 16 | 1u << 10 | T1 << 5 | T1); logi(ORRi, ORRw, T1, T1, 1);
            ldrw(T2, ENV, OFF(icbi_lea)); E(0x6B00001F | T2 << 16 | T1 << 5); uint32_t *ok = cp; E(0x54000000 | EQ);
            exit_why(pc, X_INTERP); fix(ok, cp); return 0;
        }
        case 597: case 725: {                                                   // lswi/stswi: n bytes at EA (rA|0), zbase only
            if (!zbase) return -1;
            int n = B ? B : 32;
            if (A) mov32(T2, src(A, T0)); else movw(T2, 0);
            for (int i = 0; i < n; i += 4) {
                int r = (D + i / 4) & 31, k = n - i < 4 ? n - i : 4;
                if (xo == 597) {
                    if (k == 4) { E(0xB9400000 | (uint32_t)(i / 4) << 10 | T2 << 5 | T1); E(REVw | T1 << 5 | T1); }
                    else { movw(T1, 0); for (int j = 0; j < k; j++) { E(0x39400000 | (uint32_t)(i + j) << 10 | T2 << 5 | T0);
                                                                     E(ORRw | T0 << 16 | (uint32_t)(24 - 8 * j) << 10 | T1 << 5 | T1); } }
                    put(r, T1);
                } else {
                    int s = src(r, T1);
                    if (k == 4) { E(REVw | s << 5 | T0); E(0xB9000000 | (uint32_t)(i / 4) << 10 | T2 << 5 | T0); }
                    else for (int j = 0; j < k; j++) { E(0x53000000 | (uint32_t)(24 - 8 * j) << 16 | 31u << 10 | s << 5 | T0);
                                                       E(0x39000000 | (uint32_t)(i + j) << 10 | T2 << 5 | T0); }
                }
            }
            return 0;
        }
        case 533: case 661: {                                                   // lswx/stswx: n = XER[25-31] bytes, registers through ENV
            if (!zbase) return -1;                                              // a device would see extra reads (and writes) of the end bytes
            int e = ea_x(A, B); mov32(T2, e); uint32_t f3, f31; bm32(3, &f3); bm32(31, &f31);
            ldrw(T0, ENV, OFF(xer_bc)); logi(ANDi, ANDw, T0, T0, 0x7f); uint32_t *z = cp; E(0x34000000 | T0);   // n = 0: nothing
            E(0x8B000000 | T0 << 16 | T2 << 5 | T1); E(0xD1000400 | T1 << 5 | T1);                         // x17 = EA + n - 1
            if (xo == 533) { E(0x39400000 | T1 << 5 | T1); E(0x39400000 | T2 << 5 | T1); }                   // end bytes first: a DSI comes before any change
            else { E(0x39400000 | T1 << 5 | T0); E(0x39000000 | T1 << 5 | T0); E(0x39400000 | T2 << 5 | T0); E(0x39000000 | T2 << 5 | T0);
                   ldrw(T0, ENV, OFF(xer_bc)); logi(ANDi, ANDw, T0, T0, 0x7f); }
            flush_cache_regs(); for (int g = 0; g < 32; g++) if (GMAP[g] >= 0) strw(GMAP[g], ENV, OGPR(g));
            movw(0, D); movw(1, 0); movw(2, 0); uint32_t *lp = cp;                                         // w0 register, w1 word, w2 i
            if (xo == 533) {
                E(LDRB_r | 2u << 16 | T2 << 5 | 3); E(ORRw | 1u << 16 | 8u << 10 | 3u << 5 | 1); E(0x11000400 | 2u << 5 | 2);
                E(ANDSi | f3 | 2u << 5 | ZR); uint32_t *nf = cp; E(0x54000000 | NE);
                E(0x8B000000 | 0u << 16 | 2u << 10 | ENV << 5 | 4); strw(1, 4, OGPR(0)); E(0x11000400 | 0u << 5 | 0); E(ANDi | f31 | 0u << 5 | 0); movw(1, 0);
                fix(nf, cp); E(0x6B00001F | T0 << 16 | 2u << 5); E(0x54000000 | ((uint32_t)(lp - cp) & 0x7ffff) << 5 | CC);
                E(ANDSi | f3 | 2u << 5 | 3); uint32_t *full = cp; E(0x54000000 | EQ);                       // partial last word: bytes on the left, zeros on the right
                ubfm(3, 3, 29, 28); movw(4, 32); op3(SUBw, 4, 4, 3); E(0x1AC02000 | 4u << 16 | 1u << 5 | 1);
                E(0x8B000000 | 0u << 16 | 2u << 10 | ENV << 5 | 4); strw(1, 4, OGPR(0)); fix(full, cp);
                for (int g = 0; g < 32; g++) if (GMAP[g] >= 0 || cmap[g] >= 0) ldrw(GMAP[g] >= 0 ? GMAP[g] : cmap[g], ENV, OGPR(g));
            } else {
                E(ANDSi | f3 | 2u << 5 | ZR); uint32_t *nl = cp; E(0x54000000 | NE);
                E(0x8B000000 | 0u << 16 | 2u << 10 | ENV << 5 | 4); ldrw(1, 4, OGPR(0)); E(0x11000400 | 0u << 5 | 0); E(ANDi | f31 | 0u << 5 | 0);
                fix(nl, cp); ubfm(3, 1, 24, 31); E(STRB_r | 2u << 16 | T2 << 5 | 3); ubfm(1, 1, 24, 23); E(0x11000400 | 2u << 5 | 2);
                E(0x6B00001F | T0 << 16 | 2u << 5); E(0x54000000 | ((uint32_t)(lp - cp) & 0x7ffff) << 5 | CC);
                for (int g = 0; g <= 4; g++) ldrw(g, ENV, OGPR(g));
            }
            fix(z, cp); return 0;
        }
        case 954: case 922: case 26: case 824: {
            int s = src(D, T0), d = dst(A);
            if (xo == 954) sbfm(d, s, 0, 7); else if (xo == 922) sbfm(d, s, 0, 15);
            else if (xo == 26) E(0x5AC01000 | s << 5 | d);
            else if (!B) { mov32(d, s); strw(ZR, ENV, OFF(ca)); }
            else { uint32_t f = 0; bm32((1u << B) - 1, &f); E(ANDSi | f | s << 5 | ZR); cset(T1, NE);
                   E(ANDw | 1u << 22 | s << 16 | 31u << 10 | T1 << 5 | T1); strw(T1, ENV, OFF(ca)); sbfm(d, s, B, 31); }
            if (rc) rec(d); put(A, d); return 0;
        }
        case 23: case 55: case 87: case 119: case 279: case 311: case 343: case 375: case 534: case 790: {
            int e = ea_x(A, B), k = xo == 534 ? 4 : xo == 790 ? 5 : (xo >> 6) == 0 ? 0 : xo < 128 ? 1 : xo < 320 ? 2 : 3;
            load(k, D, (Addr){e, 0}); if (xo & 32 && xo < 400) upd(A, e); return 0; }
        case 151: case 183: case 215: case 247: case 407: case 439: case 662: case 918: {
            int e = ea_x(A, B), k = xo == 662 ? 4 : xo == 918 ? 5 : xo < 200 ? 0 : xo < 300 ? 1 : 2;
            store(k, D, (Addr){e, 0}); if (xo & 32 && xo < 500) upd(A, e); return 0; }
        case 6: case 38: case 7: case 39: case 71: case 135: case 167: case 199: {
            if (!(jmsr & MSR_VEC) || !vmx_mem_enabled || !vmx_int2_enabled) return -1;
            vmx_check(pc);int e=ea_x(A,B);mov32(T0,e);
            uint32_t vo=OFF(vr)+16u*D;
            if (xo==6 || xo==38) { /* lvsl/lvsr: no access to the guest EA */
                logi(ANDi,ANDw,T0,T0,15);
                if(xo==38){movw(T1,16);op3(SUBw,T0,T1,T0);}
                E(0x10000071u);E(0x3dc00220u); /* adr x17, local bytes; ldr q0,[x17] */
                uint32_t *over=cp;E(0x14000000u);
                E(0x03020100u);E(0x07060504u);E(0x0b0a0908u);E(0x0f0e0d0cu);b_at(over,cp);
                E(0x4e010e01u);E(0x4e218400u); /* dup v1.16b,w16; add byte indices */
                E(0x3d800000u | (vo/16)<<10 | ENV<<5);return 0;
            }
            unsigned n=xo==7||xo==135?1:xo==39||xo==167?2:4;
            logi(ANDi,ANDw,T0,T0,~(n-1));ubfm(T2,T0,0,3);
            E(0x8b000000u | T2<<16 | ENV<<5 | T2);
            if(vo>>12)E(0x91400000u | (vo>>12)<<10 | T2<<5 | T2);
            static const uint32_t loads[3]={0x39400000u,0x79400000u,0xb9400000u};
            unsigned k=n==1?0:n==2?1:2;int st=xo>=135;
            uint32_t op=loads[k] | ((vo&4095)/n)<<10 | T2<<5 | T1;
            if(st){E(op);mem(0,n,T1,(Addr){T0,0});}
            else {mem(1,n,T1,(Addr){T0,0});E(op&~0x00400000u);}
            return 0;
        }
        case 103: case 359: case 231: case 487: {                               // lvx/lvxl/stvx/stvxl: 16 bytes at EA & ~15 <-> VR as is
            if (!(jmsr & MSR_VEC) || !vmx_mem_enabled) return -1;                // (VR is in memory order; two 64-bit accesses: ISV for MMIO,
            if (jsys) { ldrw(T1, ENV, OFF(msr)); uint32_t *br = cp;              //  one page, so no partial store)
                        E(0x37000000u | 25u << 19 | T1); exit_why(pc, X_INTERP); fix(br, cp); }   // VEC=0: the exception in the interpreter
            int e = ea_x(A, B); uint32_t vo = OFF(vr) + 16u * D;
            logi(ANDi, ANDw, T0, e, ~15u);
            if (!zbase) E(0x8B204000 | T0 << 16 | MEM << 5 | T0);               // add x16, x28, w16, uxtw
            if (xo == 103 || xo == 359) { E(0xF9400000 | T0 << 5 | T1); E(0xF9400400 | T0 << 5 | T2); strx(T1, ENV, vo); strx(T2, ENV, vo + 8); }
            else { ldrx(T1, ENV, vo); ldrx(T2, ENV, vo + 8); E(0xF9000000 | T0 << 5 | T1); E(0xF9000400 | T0 << 5 | T2); }
            return 0;
        }
        case 371: case 339: case 467: {                                         // mftb/mfspr/mtspr: LR, CTR, TB in the VM, SPRs without side effects
            int spr = A | B << 5;
            if (xo != 467 && (spr == 268 || spr == 269) && jsys == 2 && jit_vm_buf) {   // TB from the VM counter: tb_a + (CNTVCT * tb_m) >> 32
                E(0xD53BE040 | T0); ldrx(T1, ENV, OFF(tb_m));
                E(0x9B007C00 | T1 << 16 | T0 << 5 | T2); E(0x9BC07C00 | T1 << 16 | T0 << 5 | T0);   // mul x30; umulh x16
                E(0x93C00000 | T2 << 16 | 32u << 10 | T0 << 5 | T0);                                // extr x16, x16, x30, #32
                ldrx(T1, ENV, OFF(tb_a)); E(0x8B000000 | T1 << 16 | T0 << 5 | T0);
                if (spr == 269) E(0xD360FC00 | T0 << 5 | T0);
                put(D, T0); return 0;
            }
            if (xo == 371) return -1;
            if (spr == 8 || spr == 9) { if (xo == 339) put(D, spr == 8 ? H_LR : H_CTR); else mov32(spr == 8 ? H_LR : H_CTR, src(D, T0)); return 0; }
            if (spr == 1 && xo == 339) {                                        // mfxer (SO is always 0 in the JIT)
                ldrw(T1, ENV, OFF(xer_bc)); logi(ANDi, ANDw, T1, T1, 0x7f);
                ldrw(T2, ENV, OFF(ca)); E(ORRw | T2 << 16 | 29u << 10 | T1 << 5 | T1);
                ldrw(T2, ENV, OFF(ov)); E(ORRw | T2 << 16 | 30u << 10 | T1 << 5 | T1);
                put(D, T1); return 0;
            }
            if (spr == 1) {                                                     // mtxer: SO=1 goes to the interpreter (the JIT turns off)
                int s = src(D, T0); uint32_t *b = cp; E(0x36000000 | 31u << 19 | s);   // tbz ws, #31, continue
                exit_why(pc, X_INTERP); fix(b, cp);
                ubfm(T2, s, 30, 30); strw(T2, ENV, OFF(ov)); ubfm(T2, s, 29, 29); strw(T2, ENV, OFF(ca));
                logi(ANDi, ANDw, T2, s, 0x7f); strw(T2, ENV, OFF(xer_bc)); return 0;
            }
            if (xo == 339 && spr == 22 && !kpr && jsys == 2 && jit_vm_buf) {   // mfdec: dec_w - (TB - dec_tbw), TB from the VM counter
                E(0xD53BE040 | T0); ldrx(T1, ENV, OFF(tb_m));
                E(0x9B007C00 | T1 << 16 | T0 << 5 | T2); E(0x9BC07C00 | T1 << 16 | T0 << 5 | T0);
                E(0x93C00000 | T2 << 16 | 32u << 10 | T0 << 5 | T0);
                ldrx(T1, ENV, OFF(tb_a)); E(0x8B000000 | T1 << 16 | T0 << 5 | T0);
                ldrx(T1, ENV, OFF(dec_tbw)); op3(SUBw, T0, T0, T1); ldrw(T1, ENV, OFF(dec_w)); op3(SUBw, T0, T1, T0);
                put(D, T0); return 0;
            }
            if (xo == 467 && spr == 1008 && !kpr && vm_vec) {                    // mtspr HID0 (G4; its idle loop made 52k exits per 5 s):
                int v = src(D, T0); ldrw(T1, ENV, OFF(spr) + 4u * 1008);          // a plain store unless HIGH_BAT/XBSEN/STEN change or XBSEN
                op3(EORw, T1, T1, v); logi(ANDi, ANDw, T2, v, HID0_XBSEN); op3(ORRw, T1, T1, T2);   // is written (xgen, xbsen: interpreter)
                movw(T2, HID0_HIGH_BAT | HID0_XBSEN | HID0_STEN); E(0x6A00001Fu | T2 << 16 | T1 << 5);   // tst
                uint32_t *ok = cp; E(0x54000000 | EQ); exit_why(pc, X_INTERP); fix(ok, cp);
                strw(v, ENV, OFF(spr) + 4u * 1008); return 0;
            }
            if (!(xo == 339 ? plain_rd(spr) : plain_spr(spr)) || (kpr && spr & 16)) return -1;   // the rest, and privileged SPRs with PR=1: interpreter
            if (xo == 339) { int d = dst(D); ldrw(d, ENV, OFF(spr) + 4u * spr); put(D, d); } else strw(src(D, T0), ENV, OFF(spr) + 4u * spr);
            return 0;
        }
        case 83: { if (kpr) return -1; int d = dst(D); ldrw(d, ENV, OFF(msr)); put(D, d); return 0; }   // mfmsr
        case 595: { if (kpr) return -1; int d = dst(D); ldrw(d, ENV, OFF(sr) + 4u * (A & 15)); put(D, d); return 0; }   // mfsr
        case 659: { if (kpr) return -1; int b = src(B, T1); ubfm(T2, b, 28, 31); E(0x8B000000 | T2 << 16 | 2u << 10 | ENV << 5 | T2);   // mfsrin
                    int d = dst(D); ldrw(d, T2, OFF(sr)); put(D, d); return 0; }
        case 983: {                                                             // stfiwx: the low FPR word as an integer (PT_FP_MORE)
            if (!fp_more_enabled) return -1;
            fp_check(pc); int e = ea_x(A, B);
            fbits_load(T1, D); E(REVw | T1 << 5 | T1); mem(0, 4, T1, (Addr){e, 0});
            return 0;
        }
        case 535: case 567: case 599: case 631: case 663: case 695: case 727: case 759: {     // lfsx lfdx stfsx stfdx (and update forms)
            fp_check(pc); int e = ea_x(A, B), sgl = xo == 535 || xo == 567 || xo == 663 || xo == 695;
            if (xo < 650) fload(sgl, D, e); else fstore(sgl, D, e, pc);
            if (xo & 32) upd(A, e);
            return 0;
        }
        case 210: case 242: {                                                   // mtsr/mtsrin: SR in CPU without an exit; with DR=1 an exit
            if (kpr) return -1;                                                 // (the environment installs segment mappings) unless a published slot fits
            int s = src(D, T0);
            if (xo == 210) movw(T2, (A & 15) * 4u); else { int b = src(B, T1); ubfm(T2, b, 28, 31); E(0x531E7400 | T2 << 5 | T2); }
            E(0x8B000000 | T2 << 16 | ENV << 5 | T2);                           // x30 = ENV + 4 * SR number
            ldrw(T1, T2, OFF(sr)); E(0x6B00001F | s << 16 | T1 << 5); uint32_t *b1 = cp; E(0x54000000 | EQ);   // unchanged: continue
            strw(s, T2, OFF(sr)); ldrw(T1, ENV, OFF(srgen)); E(0x11000400 | T1 << 5 | T1); strw(T1, ENV, OFF(srgen));
            ldrw(T1, ENV, OFF(msr)); uint32_t *b2 = cp; E(0x36000000 | 4u << 19 | T1);                   // DR=0: continue
            /* With DR=1 (XNU's kernel sets SRs so) a published slot with the new SRs is installed here
             * (ttbr[1]: DR=1 PR=0) and the block goes on; not the segment this code runs from, whose code may change. */
            uint32_t *b3 = NULL, *b4 = NULL, *b5 = NULL;
            if (srsw_fn && !(xo == 210 && (A & 15) == pc >> 28)) {
                if (xo == 242) { int b = src(B, T1); ubfm(T1, b, 28, 31); E(0x7100001F | (pc >> 28) << 10 | T1 << 5); b3 = cp; E(0x54000000 | EQ); }   // cmp w17, #seg
                uint32_t *bl = cp; E(0x94000000u | ((uint32_t)(srsw_fn - bl) & 0x3ffffff));
                b4 = cp; E(0x34000000 | T0);                                    // cbz w16: not published — the environment
                ldrx(T0, ENV, OFF(ttbr) + 8); E(0xD5182000 | T0); E(0xD5033FDF);   // msr ttbr0_el1, x16; isb
                b5 = cp; E(0);
                fix(b4, cp); if (b3) fix(b3, cp);
            }
            exit_why(pc + 4, X_MODE);
            fix(b1, cp); fix(b2, cp); if (b5) b_at(b5, cp); return 0;
        }
        case 20: {                                                              // lwarx: load, then reserve
            int e = ea_x(A, B); mov32(T2, e); load(0, D, (Addr){T2, 0});
            movw(T1, 1); strw(T1, ENV, OFF(reserve)); strw(T2, ENV, OFF(reserve_addr)); return 0;
        }
        case 150: {                                                             // stwcx.: store if the reservation is for this EA
            if (!rc) return -1;
            int e = ea_x(A, B); mov32(T2, e);
            ldrw(T1, ENV, OFF(reserve)); uint32_t *b1 = cp; E(0x34000000 | T1);                  // cbz w17, failure
            ldrw(T1, ENV, OFF(reserve_addr)); E(0x6B00001F | T2 << 16 | T1 << 5); uint32_t *b2 = cp; E(0x54000000 | NE);
            store(0, D, (Addr){T2, 0});
            strw(ZR, ENV, OFF(reserve)); E(0xD2F40000 | H_CR0); uint32_t *b3 = cp; E(0x14000000);  // cr0 = EQ (0xA000…0)
            fix(b1, cp); fix(b2, cp);
            strw(ZR, ENV, OFF(reserve)); E(0xD2F40000 | H_CR0); E(0xB2400000 | H_CR0 << 5 | H_CR0);   // cr0 = 0 (…01)
            b_at(b3, cp); return 0;
        }
        case 1014: {                                                            // dcbz: 32 zero bytes
            int e = ea_x(A, B); logi(ANDi, ANDw, T2, e, ~31u);
            for (int k = 0; k < 4; k++) {
                if (zbase) E(0xF9000000 | (uint32_t)k << 10 | T2 << 5 | ZR);                         // str xzr, [x30, #8k]
                else { E(0xF8204800 | T2 << 16 | MEM << 5 | ZR); if (k < 3) E(0x11002000 | T2 << 5 | T2); }   // str xzr, [x28, w30, uxtw]; +8
            }
            return 0;
        }
        case 19: {                                                              // mfcr: assemble the 8 fields
            uint32_t mask = ~0u, next = 0;
            int sh_out = 0;
            /* GCC's mfcr; rlwinm rD,rD,... reads only a few CR bits. Both
             * instructions are nonfaulting; fuse only an in-place overwrite. */
            if (cr_fuse && !check && gm && pc + 4 < pg_end && cur_n + 1 < maxblk) {
                next = be32(gm + pc + 4);
                if (next >> 26 == 21 && (int)(next >> 21 & 31) == D && (int)(next >> 16 & 31) == D) {
                    sh_out = next >> 11 & 31;
                    uint32_t m = ppc_mask(next >> 6 & 31, next >> 1 & 31);
                    mask = sh_out ? m >> sh_out | m << (32 - sh_out) : m;
                    skip_next = 1;
                }
            }
            if (skip_next) movw(T1, 0);
            for (int f = 0; f < 8; f++) {
                if (!(mask & (0xf0000000u >> (4 * f)))) continue;
                int h = f ? (ldrx(T0, ENV, OCR(f)), T0) : H_CR0, sh = 31 - 4 * f;
                if (mask & (1u << sh)) { E(0xD37EFC00 | h << 5 | T2); bfm(T1, T2, (32 - sh) & 31, 0); }
                if (mask & (1u << (sh - 1))) { E(0xF100001F | h << 5); cset(T2, GT); bfm(T1, T2, (33 - sh) & 31, 0); }
                if (mask & (1u << (sh - 2))) { E(0x7100001F | h << 5); cset(T2, EQ); bfm(T1, T2, (34 - sh) & 31, 0); }
                if (mask & (1u << (sh - 3))) { E(0xCA000000 | h << 16 | 2u << 10 | h << 5 | T2); E(0xD37FFC00 | T2 << 5 | T2); bfm(T1, T2, (35 - sh) & 31, 0); }
            }
            if (skip_next) { rorw(T1, T1, 32 - sh_out); if (next & 1) sxtw(H_CR0, T1); }
            put(D, T1); return 0;
        }
        case 144: {                                                             // mtcrf: nibble table -> V
            int crm = in >> 12 & 0xff, s = src(D, T1);
            for (int f = 0; f < 8; f++) if (crm & (0x80 >> f)) {
                ubfm(T2, s, 28 - 4 * f, 31 - 4 * f);                            // ubfx w30, ws, #(28-4f), #4
                E(0x8B000000 | T2 << 16 | 3u << 10 | ENV << 5 | T2);            // add x30, x27, x30, lsl #3
                ldrx(f ? T0 : H_CR0, T2, OFF(crtab));
                if (f) strx(T0, ENV, OCR(f));
            }
            return 0;
        }
        case 86: case 54: case 278: case 246: case 598: case 854: return 0;    // cache, barriers (icbi: case 982)
        case 566: case 470: return kpr ? -1 : 0;                                // tlbsync, dcbi: supervisor no-ops (user: program exception)
        case 306: {                                                             /* tlbie in the VM when no ARM table maps the page */
            if (kpr || !jit_pggen || !jit_mapped_off) return -1;
            /* XNU invalidates pages no table holds (88 000 tlbie exits in 5 s of Xbench Quartz): then the only
             * effects are the code generation of the page (jump cache) and the icbi shortcut. A page a table may map, or
             * a valid entry of the 7447A software TLB (tlbld/tlbli) in its set, leaves for the interpreter. */
            int e = src(B, T1); strw(e, ENV, OFF(jit_tmp));
            ubfm(T0, e, 12, 17); E(0x8B000000 | T0 << 16 | 5u << 10 | ENV << 5 | T0);   // x16 = ENV + 32 * set
            ldrw(T1, T0, OFF(tlb) + 12); ldrw(T2, T0, OFF(tlb) + 28); op3(ORRw, T1, T1, T2);
            ldrw(T2, T0, OFF(tlb) + 2048 + 12); op3(ORRw, T1, T1, T2); ldrw(T2, T0, OFF(tlb) + 2048 + 28); op3(ORRw, T1, T1, T2);
            uint32_t *s1 = cp; E(0x35000000 | T1);                                  // cbnz w17: software TLB
            ldrw(T1, ENV, OFF(jit_tmp)); ubfm(T0, T1, 12, 27);                      // w16 = page within the segment
            E(0x91400000 | (jit_mapped_off >> 12) << 10 | ENV << 5 | T2); ubfm(T1, T0, 6, 15);
            E(0x8B000000 | T1 << 16 | 3u << 10 | T2 << 5 | T2); ldrx(T2, T2, 0); E(0x9AC02400 | T0 << 16 | T2 << 5 | T2);   // x30 = word >> (page & 63)
            uint32_t *s2 = cp; E(0x37000000 | T2);                                  // tbnz w30, #0: a table may map it
            E(0x91400000 | (jit_pggen_off >> 12) << 10 | ENV << 5 | T2); E(0x8B000000 | T0 << 16 | 2u << 10 | T2 << 5 | T2);
            ldrw(T1, T2, 0); E(0x11000400 | T1 << 5 | T1); strw(T1, T2, 0);        // its code generation (jump cache)
            strw(ZR, ENV, OFF(icbi_lea));
            uint32_t *ok = cp; E(0);
            fix(s1, cp); fix(s2, cp); exit_why(pc, X_INTERP);
            b_at(ok, cp); return 0;
        }
        case 342: case 374: case 822: case 758: return jmsr & MSR_VEC ? 0 : -1; // dst/dstst/dss/dcba: hints (altivec.c), Xbench Stream
        }
        return -1;
    }
    }
    return -1;
}

static uint32_t body(CPU *c, uint32_t pc0) {                     // block body; returns the PC of the last instruction
    uint32_t pc = pc0;
    fvalid = fpro; fp_checked = fp_guarded = fp_xx = 0; pend.on = 0;
    for (int n = 0;; n++, pc += 4) {
        if (n == maxblk || (n && !(pc & 0xfff)) || cp - blk_start > 49152) { exit_to(pc); return pc - 4; }
        // a block does not cross a PPC page; instruction offsets fit the uint16_t imap
        uint32_t in = be32(gm + pc);
        uint32_t *mark = cp; int saved = nstub, saved_side = nside;
        cur_n = n; skip_next = 0; kill_cr0 = next_kills(pc, 0);
        if (!dry) imap[nim + n] = (uint16_t)(cp - blk_start);
        if (nhot && pc >= 0xFFF00000u && is_hot(pc)) { exit_why(pc, X_INTERP); return pc; }   // a device access from firmware: interpreter (not marked in untr)
        int r = tr(in, pc);
        if (skip_next && !dry) imap[nim + n + 1] = imap[nim + n];
        if (skip_next) { n++; pc += 4; }
        if (r < 0) nside = saved_side;
        if (r < 0) { uint32_t in = be32(gm + pc), xo = in >> 1 & 0x3ff;
                     if (in >> 26 == 31 && (xo == 339 || xo == 467)) untr_spr[xo == 339][(in >> 16 & 31) | (in >> 6 & 0x3e0)] = 1; else untr[(in >> 26) << 10 | xo] = 1; cp = mark; nstub = saved; exit_why(pc, X_INTERP); return pc; }
        if (r) return pc;
    }
}
static uint32_t *translate(CPU *c, uint32_t pc0, uint32_t ra0) {   // ra0: physical address of pc0 (IR=1)
    if (bufend - cp < (fp_lazy_enabled ? 65536 : 16384) || nblk == MAXB || nim + 256 > NIM) flush_all();
    blk_pc0 = pc0; blk_pg = ra0 >> 12; blk_start = loop_start = cp; gm = pmem(c, ra0 & ~0xfffu, 0x1000) - (pc0 & ~0xfffu); pg_end = (pc0 & ~0xfffu) + 0x1000;
    memset(cmap, -1, sizeof cmap); memset(cnt, 0, sizeof cnt); cwr = 0;
    fcache_active = 0; memset(fcnt, 0, sizeof fcnt); memset(fmap, -1, sizeof fmap);
    fp_hoist = !check && fp_hoist_enabled;
    fp_lazy_active = !check && !fp_old && fp_lazy_enabled; fpro = 0; fsf = fdbl = fwr = frbw = fsw = 0;
    if (!check) {                                                                // pass 1: count register uses
        dry = 1; nstub = 0; nside = 0; body(c, pc0); dry = 0; cp = blk_start;
        for (int k = 0; k < NPOOL; k++) {                                        // the most used go into the cache
            int best = -1; for (int g = 0; g < 32; g++) if (cnt[g] && cmap[g] < 0 && (best < 0 || cnt[g] > cnt[best])) best = g;
            if (best < 0) break; cmap[best] = POOL[k];
        }
        for (int g = 0; g < 32; g++) if (cmap[g] >= 0) ldrw(cmap[g], ENV, OGPR(g));   // prologue
        if (fcache_enabled) {
            for (int k = 0; k < 7; k++) {
                int best = -1;
                for (int g = 0; g < 32; g++) if (fcnt[g] > 1 && fmap[g] < 0 && (best < 0 || fcnt[g] > fcnt[best])) best = g;
                if (best < 0) break;
                fmap[best] = (int8_t)(25 + k);
            }
            /* Loaded here, before loop_start, so a self-loop keeps them in registers: a loop-carried FPR
             * (Geekbench Dot Product's sum) does not go through a store and a reload per iteration. */
            uint32_t *bad[8]; int nbad = 0;
            for (int g = 0; g < 32; g++) if (fmap[g] >= 0) {
                uint32_t h = (uint32_t)fmap[g], at = OFF(fpr) / 8 + (uint32_t)g;
                if (fsf_enabled && fp_lazy_active && !(fdbl >> g & 1) && fsw >> g & 1 && !fsf_off(pc0)) {   // single format (fsf above)
                    fsf |= 1u << g; E(0xFD400000 | at << 10 | ENV << 5); E(0x1E624000 | h);   // ldr d0; fcvt sH, d0
                    if (frbw >> g & 1) { E(0x1E22C001 | h << 5); E(0x1E602000 | 1u << 5); bad[nbad++] = cp; E(0x54000000 | NE); }   // exact?
                } else E(0xFD400000 | at << 10 | ENV << 5 | h);
                fpro |= 1u << g;
            }
            if (fsf) st_fsf_blocks++;
            if (nbad) { uint32_t *ok = cp; E(0x14000000); for (int i = 0; i < nbad; i++) fix(bad[i], cp); cwr = 0; exit_why(pc0, X_FSF); b_at(ok, cp); }   // translated again without
            fcache_active = 1;
        }
        loop_start = cp; cwr = 0;
    }
    ldrw(T0, ENV, OFF(irq)); uint32_t *irq_br = cp; E(0x35000000 | T0);           // entry and backward branch: is an interrupt requested?
    nstub = 0; nside = 0;
    uint32_t pc = body(c, pc0);
    for (int i = 0; i < nside; i++) {
        fix(side[i].br, cp);
        if (side[i].mf >= 0) {
            int v = side[i].mf ? T2 : H_CR0, a = side[i].ma, b = side[i].mb;
            if (side[i].msign) { sxtw(v, a); if (b >= 0) E(0xCB20C000 | b << 16 | v << 5 | v); else subimm64(v, v, side[i].mimm, 1); }
            else if (b >= 0) E(0xCB000000 | b << 16 | a << 5 | v);
            else subimm64(v, a, (uint32_t)side[i].mimm, 0);
            if (side[i].mf) strx(T2, ENV, OCR(side[i].mf));
        }
        exit_to(side[i].target);       // cwr is every write of the block here: in a loop, earlier iterations also made the writes after this exit
    }
    for (int i = 0; i < nstub; i++) {                                           // link stubs
        b_at(stub[i].site, cp);
        movw(T0, stub[i].target); strw(T0, ENV, OFF(pc));
        int32_t off = (int32_t)((stub[i].site - cp) * 4);
        E(0x10000000 | ((uint32_t)off & 3) << 29 | (((uint32_t)off >> 2) & 0x7ffff) << 5 | T1);   // adr x17, site
        strx(T1, ENV, OFF(jit_link)); movw(T1, X_LINK); strw(T1, ENV, OFF(jit_exit)); b_to(exit_common);   // stubs only within the page
    }
    fix(irq_br, cp);                                                            // interrupt: block cache to ENV, exit at pc0
    for (int g = 0; g < 32; g++) if (cmap[g] >= 0) strw(cmap[g], ENV, OGPR(g));
    movw(T0, pc0); strw(T0, ENV, OFF(pc)); movw(T1, X_IRQ); strw(T1, ENV, OFF(jit_exit)); b_to(exit_common);
    uint32_t *dead = cp;                                                        // exit before the prologue: the block's first instruction becomes a b here
    movw(T0, pc0); strw(T0, ENV, OFF(pc)); movw(T1, X_LOOKUP); strw(T1, ENV, OFF(jit_exit)); b_to(exit_common);
    __builtin___clear_cache((char *)blk_start, (char *)cp);
    insert(pc0 | KF, blk_pg, blk_start); st_blocks++;
    int32_t d = dead_ix[hix(pc0 | KF, blk_pg) & 0xffff] - 1;                   // the previous translation was dropped: old links to it come here
    if (d >= 0 && d < nblk && blk[d].isdead && blk[d].key == (pc0 | KF) && blk[d].ppg == blk_pg) {
        b_at(blk[d].code, blk_start); __builtin___clear_cache((char *)blk[d].code, (char *)(blk[d].code + 1));
    }
    uint32_t q = ra0 >> 12;
    if (pp_gen[q] != gen) { pp_gen[q] = gen; pp_head[q] = -1; }
    blk[nblk].key = pc0 | KF; blk[nblk].dead = dead; blk[nblk].isdead = 0; blk[nblk].ppg = q;
    blk[nblk].ppnext = pp_head[q]; pp_head[q] = nblk;
    blk[nblk].code = blk_start; blk[nblk].end = cp; blk[nblk].pc = pc0; blk[nblk].n = (pc - pc0) / 4 + 1;
    blk[nblk].im = nim; memcpy(blk[nblk].cmap, cmap, 32); nim += blk[nblk++].n;
    fcache_active = fp_hoist = fp_lazy_active = 0;
    return blk_start;
}

static void init(CPU *c) {
    jmsr=ppc_msr_mask(c);
    /* Keep new paths opt-in until their differential + application A/B acceptance. */
    fcache_enabled = getenv("PT_FPR_CACHE") && atoi(getenv("PT_FPR_CACHE"));
    fp_hoist_enabled = getenv("PT_FP_HOIST") && atoi(getenv("PT_FP_HOIST"));
    fp_lazy_enabled = getenv("PT_FP_LAZY") && atoi(getenv("PT_FP_LAZY"));
    fp_round_fast = getenv("PT_FP_ROUND_FAST") && atoi(getenv("PT_FP_ROUND_FAST"));
    fma_single_direct = getenv("PT_FMA_DIRECT_S") && atoi(getenv("PT_FMA_DIRECT_S"));
    cr_fuse = getenv("PT_CR_FUSE") && atoi(getenv("PT_CR_FUSE"));
    vmx_int2_enabled = getenv("PT_VMX_INT2") && atoi(getenv("PT_VMX_INT2"));
    vmx_mhadd_enabled = getenv("PT_VMX_MHADD") && atoi(getenv("PT_VMX_MHADD"));
    const char *neon = getenv("PT_VMX_NEON");
    vmx_neon_enabled = neon && !strcmp(neon, "1");
    vmx_mem_enabled = !getenv("PT_VMX_MEM") || atoi(getenv("PT_VMX_MEM"));
    vmx_vscr_enabled = !getenv("PT_VMX_VSCR") || atoi(getenv("PT_VMX_VSCR"));
    vmx_fp_enabled = !getenv("PT_VMX_FP") || atoi(getenv("PT_VMX_FP"));
    vmx_fp2_enabled = getenv("PT_VMX_FP2") && atoi(getenv("PT_VMX_FP2"));
    fp_more_enabled = getenv("PT_FP_MORE") && atoi(getenv("PT_FP_MORE"));
    fsf_enabled = !getenv("PT_FP_SGL") || atoi(getenv("PT_FP_SGL"));
    if (jit_vm_buf) { buf = jit_vm_buf; bufend = buf + jit_vm_size / 4; jc = jit_vm_jc; }
    else {
        buf = mmap(0, 64 << 20, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (buf == MAP_FAILED) { perror("jit mmap"); exit(1); }
        bufend = buf + (64 << 20) / 4;
    }
    cp = buf;
    if (jit_vm_buf) {   /* the fault-resume stub: the queued invalidations, then back to the access (ELR_EL1/SPSR_EL1 hold it) */
        jit_tlbi_eret = cp; strx(T0, ENV, OFF(tlbi_save)); strx(T2, ENV, OFF(tlbi_save) + 8); tlbi_run();
        ldrx(T0, ENV, OFF(tlbi_save)); ldrx(T2, ENV, OFF(tlbi_save) + 8); E(0xD69F03E0);   // eret
    }
    uint32_t *t = cp; jit_tramp = t;
    if (jit_vm_buf) {                                                           // VM: no host stack; the exit is brk #0
        // Not hvc: on macOS 27.0.1 (M4), for some x0 (e.g. 0xc1a86500) HVF handled hvc itself and returned
        // x0 = 0xffffffff to the guest without exiting to the host; 0x80000000 and 0xffffffff gave a normal HVC exit.
        // The mechanism is unknown (input matrix: tests/epicycle/hvc_matrix.c). brk with trapped debug exceptions is documented.
        E(0xAA0003E0 | 0u << 16 | ENV); E(0xAA0003E0 | 1u << 16 | T1);
        tlbi_run(); load_guest(); E(0xD61F0000 | T1 << 5);
        exit_common = cp; store_guest(); E(0xD4200000); b_to(exit_common);
        enter = jit_vm_enter;
        goto done;
    }
    E(0xA9800000 | (uint32_t)(-12 & 0x7f) << 15 | 30u << 10 | 31u << 5 | 29);   // stp x29,x30,[sp,#-96]!
    for (int i = 0; i < 5; i++) E(0xA9000000 | (uint32_t)(2 + 2 * i) << 15 | (uint32_t)(20 + 2 * i) << 10 | 31u << 5 | (uint32_t)(19 + 2 * i));
    E(0xAA0003E0 | 0u << 16 | ENV); E(0xAA0003E0 | 1u << 16 | T1);             // mov x27,x0; mov x17,x1
    ldrx(MEM, ENV, OFF(vbase)); load_guest(); E(0xD61F0000 | T1 << 5);         // br x17
    exit_common = cp; jit_fault_exit = cp;
    store_guest(); ldrw(0, ENV, OFF(jit_exit));
    for (int i = 0; i < 5; i++) E(0xA9400000 | (uint32_t)(2 + 2 * i) << 15 | (uint32_t)(20 + 2 * i) << 10 | 31u << 5 | (uint32_t)(19 + 2 * i));
    E(0xA8C00000 | 12u << 15 | 30u << 10 | 31u << 5 | 29); E(0xD65F03C0);     // ldp x29,x30,[sp],#96; ret
    enter = (int (*)(CPU *, void *))(void *)t;
done:
    for (int i = 0; i < 1 << 16; i++) jc[i] = (JC){~0ull, 0, 0, 0, 0};     // an empty slot must not match PPC pc=0
    c->jit_jc = jit_vm_buf ? (void *)jit_vm_jc_va : (void *)jc;
    if (!c->vbase) c->vbase = c->mem;
    zbase = jit_zbase >= 0 ? jit_zbase : (!c->mem && !getenv("PT_NOZB"));
    if (zbase) POOL[NPOOL++] = MEM;
    for (int n = 0; n < 16; n++) c->crtab[n] = cr_make(n >> 3 & 1, n >> 2 & 1, n >> 1 & 1, n & 1);
    flush_all();
    if (getenv("PT_MAXBLK")) maxblk = atoi(getenv("PT_MAXBLK"));
    if (maxblk < 1) maxblk = 1;
    if (maxblk > 256) maxblk = 256;                          /* imap reservation and cold-link capacity */
    if (getenv("PT_CHECK")) { if (sscanf(getenv("PT_CHECK"), "%x-%x", &chk_lo, &chk_hi) == 2) chk_arm = 1; else { check = 1; maxblk = 1; } }
    /* Explicit zero must disable a switch; A/B settings use the same numeric convention as the other FP flags. */
    nolink = getenv("PT_NOLINK") && atoi(getenv("PT_NOLINK"));
    nojc = getenv("PT_NOJC") && atoi(getenv("PT_NOJC"));
    if (getenv("PT_JC_WAYS") && atoi(getenv("PT_JC_WAYS")) == 1) jc_ways = 1;
    fp_old = getenv("PT_FP_OLD") && atoi(getenv("PT_FP_OLD"));
    fma_fast = getenv("PT_FMA_FAST") && atoi(getenv("PT_FMA_FAST"));
    fp_mixed = getenv("PT_FP_MIXED") && atoi(getenv("PT_FP_MIXED"));
    fp_fr_on = !getenv("PT_FP_FR") || atoi(getenv("PT_FP_FR"));        /* =0 only to measure FR's cost */
    /* The values themselves, not a 0/1 flag: =2 (rfi/sc/mtmsr in the VM also on the bare-metal stand, -s) and =3/4
     * (see vm_* above) need them. */
    vm_rfi = getenv("PT_VMRFI") ? atoi(getenv("PT_VMRFI")) : 1; vm_sc = getenv("PT_VMSC") ? atoi(getenv("PT_VMSC")) : 1;
    vm_msr = getenv("PT_VMMSR") ? atoi(getenv("PT_VMMSR")) : 1;
    vm_vec = (!getenv("PT_VMVEC") || atoi(getenv("PT_VMVEC"))) ? jmsr & MSR_VEC : 0;   // G4: the kernel changes VEC on every return to an AltiVec thread
    jsys = c->sys; interp_batch = c->sys == 2 ? 256 : 1;                       // machine: interpreter batches of up to 256 while the next instructions are untranslatable too
    const char *batch = getenv("PT_INTERP_BATCH");
    if (batch) {
        char *end; long n = strtol(batch, &end, 10);
        if (!*batch || *end || n < 1 || n > 4096) { fprintf(stderr, "PT_INTERP_BATCH: requires an integer 1..4096\n"); exit(2); }
        interp_batch = (int)n;
    }
    if (check || chk_arm || maxblk == 1) interp_batch = 1;        // diagnostics must not skip instruction boundaries
    if (!check && ((fp_lazy_enabled && !fp_old) || (jit_vm_buf && jsys))) {   // the settle and SR-switch routines, then blocks
        cp = exit_common + 64;
        if (fp_lazy_enabled && !fp_old) settle_gen();
        if (jit_vm_buf && jsys && (!getenv("PT_VM_SRSW") || atoi(getenv("PT_VM_SRSW")))) srsw_gen();   /* =0: off, for A/B measurements */
        flush_all();
    }
}

int jit_stats_on; uint64_t jit_tm[8];       // PT_STATS: time (ns): 0 VM, 1 translation, 2 interpreter, 3 tlbie, 4 SR switch; 5, 6 (hvf.c): DR=1 fill, devices
uint64_t jit_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec; }
static uint64_t st_fp_fpscr[256];   // PT_STATS: FPSCR & 0xff (RN, NI, enables) at FP exits to the interpreter
static uint64_t st_rfi[5];                     // PT_STATS: rfi exits by reason
static uint64_t st_interp_op[64 * 1024], st_exit_op[64 * 1024 + 2048];   // PT_STATS: instructions in interpreter batches; exit reasons (+ the SPR for mfspr/mtspr)
static uint32_t st_jcm[1 << 16]; static uint64_t st_jck[1 << 16][3];   /* PT_STATS: misses per set; the last wanted, evicted and kept keys */
static uint64_t st_jc[4];   /* PT_STATS: lookup exits by the target's jump-cache cell: empty, another target, stale page generation, current (left for another reason) */
static uint8_t *chk_ptr(CPU *c, uint32_t ea, uint32_t n, int acc, uint32_t mmu) {  // host address for the comparison; NULL: not available
    uint32_t ra = ea; int wr;
    if (c->msr & mmu && ((ea & 0xfff) + n > 0x1000 || pt_xlate(c, ea, acc, !!(c->msr & MSR_PR), &ra, &wr))) return NULL;
    return pmem(c, ra, n);
}
static int diff(CPU *j, CPU *r, uint32_t in, uint32_t pc) {      // 1 = mismatch
    int bad = j->pc != r->pc || j->msr != r->msr || j->lr != r->lr || j->ctr != r->ctr || memcmp(j->gpr, r->gpr, sizeof j->gpr)
           || xer_get(j) != xer_get(r) || j->fpscr != r->fpscr || memcmp(j->fpr, r->fpr, sizeof j->fpr)
           || j->vscr != r->vscr || memcmp(j->vr, r->vr, sizeof j->vr)
           || j->reserve != r->reserve || (j->reserve && j->reserve_addr != r->reserve_addr);
    for (int f = 0; f < 8; f++) bad |= cr_field(j, f) != cr_field(r, f);
    if (!bad) return 0;
    fprintf(stderr, "MISMATCH @%08x insn %08x (op %u/%u)\n", pc, in, in >> 26, in >> 1 & 0x3ff);
    for (int g = 0; g < 32; g++) if (j->gpr[g] != r->gpr[g]) fprintf(stderr, "  r%d jit %08x interp %08x\n", g, j->gpr[g], r->gpr[g]);
    for (int f = 0; f < 8; f++) if (cr_field(j, f) != cr_field(r, f)) fprintf(stderr, "  cr%d jit %x interp %x\n", f, cr_field(j, f), cr_field(r, f));
    if (j->pc != r->pc) fprintf(stderr, "  pc jit %08x interp %08x\n", j->pc, r->pc);
    if (j->msr != r->msr) fprintf(stderr, "  msr jit %08x interp %08x\n", j->msr, r->msr);
    if (j->vscr != r->vscr) fprintf(stderr, "  vscr jit %08x interp %08x\n", j->vscr, r->vscr);
    for (int v = 0; v < 32; v++) if (memcmp(j->vr[v], r->vr[v], sizeof j->vr[v])) fprintf(stderr, "  v%d differs\n", v);
    if (j->lr != r->lr || j->ctr != r->ctr || xer_get(j) != xer_get(r)) fprintf(stderr, "  lr %08x/%08x ctr %08x/%08x xer %08x/%08x\n", j->lr, r->lr, j->ctr, r->ctr, xer_get(j), xer_get(r));
    if (j->fpscr != r->fpscr) fprintf(stderr, "  fpscr jit %08x interp %08x\n", j->fpscr, r->fpscr);
    for (int f = 0; f < 32; f++) if (memcmp(&j->fpr[f], &r->fpr[f], 8)) fprintf(stderr, "  f%d differs\n", f);
    if (j->reserve != r->reserve) fprintf(stderr, "  reservation jit %u interp %u\n", j->reserve, r->reserve);
    return 1;
}

static void kill_block(int32_t i) {         // a stale translation: its entry becomes an exit, and it leaves the block table and the jump cache
    if (blk[i].isdead) return;
    blk[i].isdead = 1; dead_ix[hix(blk[i].key, blk[i].ppg) & 0xffff] = i + 1;
    b_at(blk[i].code, blk[i].dead); __builtin___clear_cache((char *)blk[i].code, (char *)(blk[i].code + 1));   // jump-cache cells and links
    for (uint32_t h = hix(blk[i].key, blk[i].ppg); ht[h].epoch == gen; h = (h + 1) & (HT - 1))                  // to it now reach the "dead" exit
        if (ht[h].code == blk[i].code) { ht[h].pc = ht[h].pg = ~0u; break; }
    st_pkill++;
}
static void kill_phys(uint32_t q) {         // icbi: the code on physical page q changed
    if (pp_gen[q] != gen) return;
    for (int32_t i = pp_head[q]; i >= 0; i = blk[i].ppnext) kill_block(i);
    pp_head[q] = -1;
}
/* A block's prologue found a value it keeps in single format not exactly single (X_FSF, nothing executed). The
 * first three times at a pc the caller runs one interpreter step (a loop's first entry with a stale double, Geekbench
 * Dot Product); the fourth drops its blocks, translated again without the single-format cache (Lua's blocks that only
 * stored doubles failed at every entry). Returns 1 when dropped. */
int jit_fsf_fail(CPU *c) {
    uint32_t n = 4;
    for (uint32_t i = c->pc >> 2 & 1023, k = 0; k < 16; k++, i = (i + 1) & 1023) if (!nofsf[i] || nofsf[i] == c->pc + 1) { nofsf[i] = c->pc + 1; n = ++nofsf_n[i]; break; }
    if (n < 4) return 0;
    for (int i = 0; i < nblk; i++) if (blk[i].pc == c->pc && !blk[i].isdead) kill_block(i);
    return 1;
}
static uint32_t *block(CPU *c) {           // the block for c->pc; NULL: not translatable, the interpreter ran one step (and delivered any ISI)
    uint32_t ra = c->pc; int wr;
    if (kir && pt_xlate(c, c->pc, 2, !!(c->msr & MSR_PR), &ra, &wr)) { interp_step(c); return NULL; }
    uint32_t *code = lookup(c->pc | KF, ra >> 12);
    if (code) return code;
    if (!pmem(c, ra & ~0xfffu, 0x1000)) { interp_step(c); return NULL; }
    if (!jit_stats_on) return translate(c, c->pc, ra);
    uint64_t t0 = jit_now(); uint32_t *r = translate(c, c->pc, ra); jit_tm[1] += jit_now() - t0; return r;
}
uint32_t jit_mem_ea(uint32_t in, const uint64_t *x, int *n, int *st) {   // in: an ldr/str emitted by mem()
    *n = 1 << (in >> 30); *st = !(in >> 22 & 1);
    uint64_t base = x[in >> 5 & 31];
    if (in & 0x01000000) return (uint32_t)(base + ((in >> 10 & 0xFFFu) << (in >> 30)));    // [xN, #imm] (base 0)
    if (in & 0x00200000) return (uint32_t)x[in >> 16 & 31];                                // [x28, wM, uxtw]: EA = wM
    return (uint32_t)(base + (int64_t)((int32_t)(in << 11) >> 23));                        // ldur/stur [xN, #simm9]
}
static void prof_snap(void);
void jit_snap(void) {                      // PT_STATS: JIT-code exits and frequent interpreter instructions since the last snapshot
    static uint64_t e0[8], s0, t0, f0;
    static uint64_t k0;
    static uint64_t x0;
    fprintf(stderr, "  SR switches %llu, tlbie %llu, code flushes %llu (including IBAT/SDR1/machine %llu), invalidated blocks %llu\n", (unsigned long long)(st_srsw - s0),
        (unsigned long long)(st_tlbie - t0), (unsigned long long)(st_cflush - f0), (unsigned long long)(st_xflush - x0), (unsigned long long)(st_pkill - k0));
    s0 = st_srsw; t0 = st_tlbie; f0 = st_cflush; k0 = st_pkill; x0 = st_xflush;
    fprintf(stderr, "  time, ms: HVF %llu (DR=1 fill %llu, devices %llu), translation %llu, interpreter %llu, tlbie %llu, SR switch %llu\n",
        (unsigned long long)(jit_tm[0] / 1000000), (unsigned long long)(jit_tm[5] / 1000000), (unsigned long long)(jit_tm[6] / 1000000), (unsigned long long)(jit_tm[1] / 1000000),
        (unsigned long long)(jit_tm[2] / 1000000), (unsigned long long)(jit_tm[3] / 1000000), (unsigned long long)(jit_tm[4] / 1000000));
    memset(jit_tm, 0, sizeof jit_tm);
    fprintf(stderr, "  exits: link %llu lookup %llu sc %llu interp %llu dsi %llu irq %llu; to interpreter (op/xo×n):",
        (unsigned long long)(st_exit[1] - e0[1]), (unsigned long long)(st_exit[2] - e0[2]), (unsigned long long)(st_exit[3] - e0[3]),
        (unsigned long long)(st_exit[4] - e0[4]), (unsigned long long)(st_exit[5] - e0[5]), (unsigned long long)(st_exit[6] - e0[6]));
    fprintf(stderr, "  exits mtmsr/mtsr %llu", (unsigned long long)(st_exit[7] - e0[7]));
    fprintf(stderr, " [lookups: jump-cache cell empty %llu, other target %llu, stale %llu, current %llu]", (unsigned long long)st_jc[0], (unsigned long long)st_jc[1],
            (unsigned long long)st_jc[2], (unsigned long long)st_jc[3]); memset(st_jc, 0, sizeof st_jc);
    for (int k = 0; k < 4; k++) {                                       // the sets that miss most: wanted, evicted, kept as SR:pc/flags
        int b = -1; for (int i = 0; i < 1 << 16; i++) if (st_jcm[i] && (b < 0 || st_jcm[i] > st_jcm[b])) b = i;
        if (b < 0) break;
        fprintf(stderr, " [cell %d ×%u:", b, st_jcm[b]);
        for (int q = 0; q < 3; q++) fprintf(stderr, " %08x:%08x/%u", (unsigned)(st_jck[b][q] >> 32), (unsigned)st_jck[b][q] & ~3u, (unsigned)st_jck[b][q] & 3);
        fprintf(stderr, "]"); st_jcm[b] = 0;
    }
    memset(st_jcm, 0, sizeof st_jcm);
    for (int k = 0; k < 8; k++) {
        int b = -1; for (int i = 0; i < 64 * 1024; i++) if (st_interp_op[i] && (b < 0 || st_interp_op[i] > st_interp_op[b])) b = i;
        if (b < 0) break;
        fprintf(stderr, " %d/%d×%llu", b >> 10, b & 1023, (unsigned long long)st_interp_op[b]); st_interp_op[b] = 0;
    }
    fprintf(stderr, "\n  interpreter exits (op/xo or mfspr/mtspr N):");
    for (int k = 0; k < 10; k++) {
        int b = -1; for (int i = 0; i < 64 * 1024 + 2048; i++) if (st_exit_op[i] && (b < 0 || st_exit_op[i] > st_exit_op[b])) b = i;
        if (b < 0) break;
        if (b < 65536) fprintf(stderr, " %d/%d×%llu", b >> 10, b & 1023, (unsigned long long)st_exit_op[b]);
        else fprintf(stderr, " %s %d×%llu", b - 65536 < 1024 ? "mtspr" : "mfspr", (b - 65536) & 1023, (unsigned long long)st_exit_op[b]);
        st_exit_op[b] = 0;
    }
    fprintf(stderr, "\n  rfi exits: other %llu icbi %llu msr-bits %llu irq %llu sr-tables %llu",
            (unsigned long long)st_rfi[0], (unsigned long long)st_rfi[1], (unsigned long long)st_rfi[2], (unsigned long long)st_rfi[3], (unsigned long long)st_rfi[4]);
    memset(st_rfi, 0, sizeof st_rfi);
    fprintf(stderr, "\n  FP exits by FPSCR&0xff:");
    for (int i = 0; i < 256; i++) if (st_fp_fpscr[i]) fprintf(stderr, " %02x×%llu", i, (unsigned long long)st_fp_fpscr[i]);
    memset(st_fp_fpscr, 0, sizeof st_fp_fpscr);
    fprintf(stderr, "\n  XER.SO=1 total: episodes %llu, interpreter instructions %llu\n", (unsigned long long)st_so, (unsigned long long)st_so_ins);
    prof_snap();
    memset(st_interp_op, 0, sizeof st_interp_op); memset(st_exit_op, 0, sizeof st_exit_op); memcpy(e0, st_exit, sizeof e0);
}
static int find_blk(uint64_t pc, uint32_t *ins) {             // ARM address in the VM -> block and the PPC instruction index in it; -1: not block code
    uint32_t *p = (uint32_t *)(pc - jit_delta);
    for (int lo = 0, hi = nblk - 1; lo <= hi;) {
        int m = (lo + hi) / 2;
        if (p < blk[m].code) { hi = m - 1; continue; }
        if (p >= blk[m].end) { lo = m + 1; continue; }
        uint32_t off = (uint32_t)(p - blk[m].code), i = 0;
        while (i + 1 < blk[m].n && imap[blk[m].im + i + 1] <= off) i++;
        *ins = i; return m;
    }
    return -1;
}
uint32_t jit_ppc_pc(uint64_t pc) { uint32_t i; int m = find_blk(pc, &i); return m < 0 ? 0 : blk[m].pc + 4 * i; }   /* PT_STATS (hvf.c) */
// PT_PROF on the stand: per-PPC-instruction histogram of guest-code samples (hvf.c), printed and reset with PT_STATS.
static struct { uint32_t pc, word; int32_t blk; uint64_t n; } st_ps[4096]; static uint64_t st_ps_n, st_ps_out;
void jit_prof_sample(CPU *c, uint64_t pc) {
    uint32_t i; int m = find_blk(pc, &i); st_ps_n++;
    if (m < 0) { st_ps_out++; return; }
    uint32_t a = blk[m].pc + 4 * i, h = a >> 2 & 4095;
    for (int k = 0; k < 4096; k++, h = (h + 1) & 4095) {
        if (st_ps[h].n && (st_ps[h].pc != a || st_ps[h].blk != m)) continue;
        if (!st_ps[h].n) { uint8_t *p = chk_ptr(c, a, 4, 2, MSR_IR); st_ps[h].pc = a; st_ps[h].word = p ? be32(p) : 0; st_ps[h].blk = m; }
        st_ps[h].n++; return;
    }
}
static void prof_snap(void) {
    if (!st_ps_n) return;
    fprintf(stderr, "  JIT PC samples %llu, outside JIT blocks %.1f%%; hottest PPC (pc word op/xo %% | block pc, PPC n, ARM words):\n",
            (unsigned long long)st_ps_n, 100.0 * st_ps_out / st_ps_n);
    for (int k = 0; k < 16; k++) {
        int b = -1; for (int i = 0; i < 4096; i++) if (st_ps[i].n && (b < 0 || st_ps[i].n > st_ps[b].n)) b = i;
        if (b < 0) break;
        uint32_t w = st_ps[b].word; int m = st_ps[b].blk;
        fprintf(stderr, "    %08x %08x %2u/%-4u %5.1f%% | %08x %3u %4u\n", st_ps[b].pc, w, w >> 26, w >> 1 & 0x3ff, 100.0 * st_ps[b].n / st_ps_n,
                blk[m].pc, blk[m].n, (unsigned)(blk[m].end - blk[m].code));
        st_ps[b].n = 0;
    }
    memset(st_ps, 0, sizeof st_ps); st_ps_n = st_ps_out = 0;
}
void jit_mmio_note(uint64_t pc) {                // a JIT-code device access (pc: the ARM instruction in the VM); only firmware code is counted
    uint32_t i; int m = find_blk(pc, &i);
    if (m < 0 || blk[m].isdead || blk[m].pc < 0xFFF00000u) return;
    uint32_t ppc = blk[m].pc + 4 * i;
    for (uint32_t h = ppc >> 2 & 1023, k = 0; k < 8; k++, h = (h + 1) & 1023) {
        if (!hot[h].pc1) { hot[h].pc1 = ppc + 1; nhot++; }
        if (hot[h].pc1 != ppc + 1) continue;
        if (++hot[h].n == 256) kill_block(m);                                   // translate again: this instruction goes to the interpreter
        return;
    }
}
int jit_restore(CPU *c, const uint64_t *x, uint64_t pc) {
    uint32_t i; int m = find_blk(pc, &i);
    if (m < 0) return 0;
    for (int g = 0; g < 32; g++) if (GMAP[g] >= 0) c->gpr[g] = (uint32_t)x[GMAP[g]]; else if (blk[m].cmap[g] >= 0) c->gpr[g] = (uint32_t)x[blk[m].cmap[g]];
    c->ctr = (uint32_t)x[H_CTR]; c->lr = (uint32_t)x[H_LR]; c->cr[0] = x[H_CR0]; c->pc = blk[m].pc + 4 * i;
    return 1;
}

void jit_run(CPU *c) {                     // until c->halted; a repeated call (after returning to the environment) continues with the existing code
    static int ready; static uint32_t seen_gen, seen_mode, seen_cgen, seen_srgen;
    if (!ready) { init(c); ready = 1; seen_gen = c->xgen; seen_srgen = c->srgen_ok = c->srgen; c->tlbie_n = 0; seen_mode = c->msr & (MSR_DR | MSR_PR); seen_cgen = c->cgen; if (jit_dmap) jit_dmap(c, 1); }
    int prof = !!getenv("PT_PROF"), stats = jit_stats_on = !!getenv("PT_STATS");
#ifdef __linux__
    if (prof) { struct sigaction sa = {0}; sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigaction(SIGPROF, &sa, 0);
                struct itimerval it = {{0, 100}, {0, 100}}; setitimer(ITIMER_PROF, &it, 0); }
#endif
    uint32_t *code = NULL; int fill_jc = 0;
    while (!c->halted) {
        if (c->jit_off) { if (c->so) { st_so_ins++; interp_step(c); continue; } c->jit_off = 0; code = NULL; fill_jc = 0; }   // SO cleared (mtxer/mcrxr, another thread): back to the JIT
        if (c->irq) { ppc_irq(c); code = NULL; if (c->halted) break; }
        if (c->cgen != seen_cgen) {                                     // icbi ... isync: retranslate the code of these pages
            uint32_t n = c->icbi_n; c->icbi_n = 0; c->icbi_lea = 0; seen_cgen = c->cgen; code = NULL;
            if (n > 64) flush_all(); else for (uint32_t i = 0; i < n; i++) kill_phys(c->icbi_ea[i] >> 12);
        }
        if (c->xgen != seen_gen) { flush_all(); code = NULL; c->icbi_lea = 0; seen_gen = c->xgen; st_xflush++; if (jit_dmap) jit_dmap(c, 1); }   // full flush (IBAT, SDR1, machine state)
        if (c->srgen != seen_srgen) {                                   // SR: switch segments; without an environment hook, flush everything
            seen_srgen = c->srgen; code = NULL; st_srsw++; c->icbi_lea = 0; uint64_t ts = stats ? jit_now() : 0;
            if (jit_dsr) jit_dsr(c); else { flush_all(); if (jit_dmap) jit_dmap(c, 1); }
            if (stats) jit_tm[4] += jit_now() - ts;
            c->srgen_ok = seen_srgen;
        }
        if (c->tlbie_n) {                                               // tlbie: remove the pages; jump-cache cells into them go stale
            uint32_t n = c->tlbie_n; c->tlbie_n = 0; code = NULL; st_tlbie += n; c->icbi_lea = 0; uint64_t tt = stats ? jit_now() : 0;
            if (n > 512 || !jit_dtlbie) { flush_all(); if (jit_dmap) jit_dmap(c, 1); }
            else {                                                      // bit 0: data only (DBAT); block bodies stay (they are keyed by physical page)
                for (uint32_t i = 0; i < n; i++) if (!(c->tlbie_ea[i] & 1)) { if (jit_pggen) jit_pggen[c->tlbie_ea[i] >> 12 & 0xffff]++; else jc_reset(); }   // every segment's alias: tlbie ignores the segment
                jit_dtlbie(c, c->tlbie_ea, (int)n);
            }
            if (stats) jit_tm[3] += jit_now() - tt;
        }
        if ((c->msr & (MSR_DR | MSR_PR)) != seen_mode) { seen_mode = c->msr & (MSR_DR | MSR_PR); if (jit_dmap) jit_dmap(c, 0); }
        else if (jit_vm_buf && jit_dmap && c->ttbr_k != (c->msr & MSR_DR ? 1u + !!(c->msr & MSR_PR) : 0u)) jit_dmap(c, 0);   // JIT code changed the mode (rfi/sc)
        kir = !!(c->msr & MSR_IR); kpr = !!(c->msr & MSR_PR);
        if (chk_arm && c->pc - chk_lo < chk_hi - chk_lo) {
            chk_arm = 0; check = 1; maxblk = 1; flush_all(); code = NULL; ppc_fp_settle(c);   // compare from a settled FPSCR
            fprintf(stderr, "ptolemy: compare JIT with interpreter from pc %08x\n", c->pc);
        }
        if (!code && !(code = block(c))) { fill_jc = 0; continue; }
        if (fill_jc) {                                                          // indirect branch: remember the target in the cache
            uint32_t s = kir ? c->sr[c->pc >> 28] : 0, h = (c->pc ^ s) * 0x9E3779B1u, i = jc_ways == 2 ? h >> 17 << 1 : h >> 16; fill_jc = 0;   // as jc_index()
            uint64_t key = (uint64_t)s << 32 | c->pc | KF; uint32_t g = jit_pggen ? jit_pggen[c->pc >> 12 & 0xffff] : 0; JC *w = &jc[i];
            if (stats) { int k = w[0].pc == key ? 0 : jc_ways == 2 && w[1].pc == key ? 1 : -1;
                         st_jc[!w[0].code ? 0 : k < 0 ? 1 : jit_pggen && w[k].gen != g ? 2 : 3]++;
                         if (w[0].code && k < 0) { st_jcm[i]++; st_jck[i][0] = key; st_jck[i][1] = w[jc_ways - 1].pc; st_jck[i][2] = jc_ways == 2 ? w[0].pc : 0; } }
            if (!w[0].code && njc < 1 << 16) jc_used[njc++] = i;
            if (jc_ways == 2 && w[0].pc != key) w[1] = w[0];                     // the newest in way 0; no key twice
            w[0].pc = key; w[0].code = (uint64_t)code + jit_delta; w[0].gen = g;
        }
        CPU before; if (check) before = *c;
        uint32_t *ran = code, wea = 0; uint8_t win0[128], winj[128];
        uint8_t *wp = NULL, *ip = check ? chk_ptr(c, c->pc, 4, 2, MSR_IR) : NULL;   // the instruction and its store window (also with the MMU on)
        int wlen = ip ? ppc_store_window(c, be32(ip), &wea) : 0;
        if (wlen && !(wp = chk_ptr(c, wea, wlen, 1, MSR_DR))) wlen = 0;
        unsigned long mmio0 = jit_mmio_n;
        if (wlen) memcpy(win0, wp, wlen);
        uint64_t tv = stats ? jit_now() : 0;
        int why = enter(c, code); code = NULL;
        if (stats) jit_tm[0] += jit_now() - tv;
        st_exit[why & 7]++;
        if (check && (why == X_LINK || why == X_LOOKUP)) {
            if (jit_mmio_n != mmio0) continue;                                  // a device cannot be accessed a second time
            uint32_t in = ip ? be32(ip) : 0; CPU ref = before; ref.irq = 0;
            if (wlen) { memcpy(winj, wp, wlen); memcpy(wp, win0, wlen); }       // the interpreter stores from a clean slate
            interp_step(&ref);
            if (wlen && memcmp(winj, wp, wlen)) {
                fprintf(stderr, "MEMORY MISMATCH @%08x insn %08x: write to [%08x, +%d) differs\n", before.pc, in, wea, wlen);
                c->halted = 1; c->exit_code = 97; return;
            }
            if (diff(c, &ref, in, before.pc)) {
                fprintf(stderr, "  before:"); for (int g = 0; g < 32; g++) fprintf(stderr, " r%d=%08x", g, before.gpr[g]); fprintf(stderr, "\n  block code:");
                for (uint32_t *p = ran; p < ran + 24; p++) fprintf(stderr, " %08x", *p);
                CPU jit1 = *c; *c = before; enter(c, ran);
                fprintf(stderr, "\n  same block repeated: r%u = %08x (first run %08x)\n", in >> 21 & 31, c->gpr[in >> 21 & 31], jit1.gpr[in >> 21 & 31]);
                c->halted = 1; c->exit_code = 97; return;
            }
            continue;
        }
        switch (why) {
        case X_LINK: { unsigned g = gen; uint32_t *site = (uint32_t *)(c->jit_link - jit_delta);
                       kir = !!(c->msr & MSR_IR); kpr = !!(c->msr & MSR_PR);   // the mode may have changed in the VM (rfi/sc): translate for it
                       code = block(c);
                       if (code && g == gen && !nolink && !check) { b_at(site, code); __builtin___clear_cache((char *)site, (char *)(site + 1)); } break; }
        case X_LOOKUP: fill_jc = !nojc; break;                                   // block and cache at the top of the loop (the mode may have changed)
        case X_MODE: if (stats) { uint8_t *p = chk_ptr(c, c->pc - 4, 4, 2, MSR_IR); if (p) st_exit_op[(be32(p) >> 26) << 10 | (be32(p) >> 1 & 0x3ff)]++; } break;   // mtsr/mtmsr counted as exit reasons
        case X_FAULT: case X_IRQ: break;                                        // DSI delivered by the environment; interrupt at the top of the loop
        case X_SC: sys_call(c); break;
        case X_FSF: if (jit_fsf_fail(c)) break;                                 // else one interpreter step, as X_INTERP
            /* fall through */
        case X_INTERP: tv = stats ? jit_now() : 0;
            for (int i = 0, run = 0; i < interp_batch && !c->halted; i++) {    // batch: back to the JIT once more than 8 translatable instructions come in a row
                uint8_t *p = chk_ptr(c, c->pc, 4, 2, MSR_IR); uint32_t k = p ? (be32(p) >> 26) << 10 | (be32(p) >> 1 & 0x3ff) : 0;
                int u = k == (31u << 10 | 339) || k == (31u << 10 | 467) ? untr_spr[(k & 1023) == 339][(be32(p) >> 16 & 31) | (be32(p) >> 6 & 0x3e0)] : p ? untr[k] : 0;
                if (nhot && c->pc >= 0xFFF00000u && is_hot(c->pc)) u = 1;
                if (i && (!p || (run = u ? 0 : run + 1) > 8)) break;
                if (stats && p && !i && k == (19u << 10 | 50)) {   // why rfi left the VM (the s1-s4 tests of the rfi emitter)
                    uint32_t n = c->spr[SPR_SRR1] & jmsr & ~(uint32_t)MSR_POW, x = c->msr ^ n;
                    st_rfi[c->icbi_n ? 1 : x & ~(uint32_t)(MSR_EE | MSR_FP | MSR_ME | MSR_FE0 | MSR_FE1 | MSR_RI | MSR_IP | MSR_PMM |
                           MSR_IR | MSR_DR | MSR_PR | vm_vec) ? 2 : x & n & MSR_EE && c->pend_masked ? 3 : n & MSR_DR && c->srgen != c->srgen_ok ? 4 : 0]++;
                }
                if (stats && p && !i && (k >> 10 == 59 || k >> 10 == 63)) {
                    st_fp_fpscr[c->fpscr & 0xff]++;
                    static uint64_t nfp; uint32_t w = be32(p);       // operands of sampled FP exits (why the fast path refused)
                    if (nfp++ % 65536 < 4) { uint64_t fa, fb, fc; memcpy(&fa, &c->fpr[w >> 16 & 31], 8); memcpy(&fb, &c->fpr[w >> 11 & 31], 8); memcpy(&fc, &c->fpr[w >> 6 & 31], 8);
                        fprintf(stderr, "ptolemy: FP exit %u/%u @%08x a %016llx b %016llx c %016llx fpscr %08x\n", k >> 10, k & 1023, c->pc,
                                (unsigned long long)fa, (unsigned long long)fb, (unsigned long long)fc, c->fpscr); }
                }
                if (stats && p) { st_interp_op[k]++; if (!i) st_exit_op[k == (31 << 10 | 339) || k == (31 << 10 | 467) ? 65536 + ((k & 1023) == 339 ? 1024 : 0) + ((be32(p) >> 16 & 31) | (be32(p) >> 6 & 0x3e0)) : k]++; }
                uint32_t pc0 = c->pc; interp_step(c);          // IRQs, exceptions and the MMU are handled at every instruction
                if (c->so) { if (++st_so <= 8) fprintf(stderr, "ptolemy: XER.SO=1 from %08x (op %u/%u, lr %08x, PR %d) — interpreter until SO clears\n", pc0, k >> 10, k & 1023, c->lr, !!(c->msr & MSR_PR)); break; }
            }
            if (stats) jit_tm[2] += jit_now() - tv;
            break;
        default: fprintf(stderr, "ptolemy: unexpected exit %d\n", why); c->halted = 1; c->exit_code = 99;
        }
        if (c->so && !c->jit_off) c->jit_off = 1;                          // the JIT assumes SO = 0 (CR0 of record forms, cmp, mfxer)
    }
    if (st_so) fprintf(stderr, "ptolemy: XER.SO=1 episodes %llu, interpreter instructions in those episodes %llu\n", (unsigned long long)st_so, (unsigned long long)st_so_ins);
#ifdef __linux__
    if (prof) { struct itimerval it = {0}; setitimer(ITIMER_PROF, &it, 0); prof_report(c); }
#endif
    if (getenv("PT_STATS") && c->sys != 2) fprintf(stderr, "ptolemy: blocks %llu, exits: link %llu lookup %llu sc %llu interp %llu\n",
        (unsigned long long)st_blocks, (unsigned long long)st_exit[1], (unsigned long long)st_exit[2], (unsigned long long)st_exit[3], (unsigned long long)st_exit[4]);
    if (getenv("PT_STATS") && c->sys != 2) for (int i = 0; i < 64 * 1024; i++) if (st_interp_op[i] > 1000) fprintf(stderr, "  to interpreter: op %d/%d x%llu\n", i >> 10, i & 1023, (unsigned long long)st_interp_op[i]);
}
