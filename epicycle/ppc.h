// Ptolemy engine: PowerPC state (32-bit, big-endian) and the shared interface.
// Guest memory: 4 GiB of host virtual address space at base mem; bytes are stored in PowerPC (big-endian) order.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The production G3/G4 runner has one CPU owner. Pay TLS access costs only
 * in builds that can execute the experimental concurrent 970 owners. */
#ifdef PT_ENABLE_970
#define PT_CPU_LOCAL _Thread_local
#else
#define PT_CPU_LOCAL
#endif

typedef struct {
    uint32_t base;
    uint64_t size;
    uint8_t *host;
    int read_only;
} PRegion; // physical memory the host sees directly
typedef struct Pt970Bus Pt970Bus;
typedef struct {
    uint64_t base, size;
    uint8_t *host;
    int read_only;
} Pt970Region;
typedef struct {
    uint32_t vsid, page, lo, valid;
} PtPpcTlb;
typedef struct {
    uint64_t pc, code;
    uint32_t gen, pad;
    uint64_t pad2;
} JC; // jump-cache cell (jit.c), 32 bytes; gen: EA page generation (tlbie)
static inline int env_on(const char *n) {
    const char *v = getenv(n);
    return v && *v && strcmp(v, "0");
} // a PT_* switch: unset, empty and "0" are off
enum {
    PEND_EXT = 1,
    PEND_DEC = 2
}; // pending interrupts

// A CR field is stored as a 64-bit value V:
//   LT <=> bit 62,  GT <=> (int64)V > 0,  EQ <=> (uint32)V == 0,  SO <=> bit 61 XOR bit 63.
// A compare of a,b with XER.SO=0 gives V = a - b in 64 bits (|V| < 2^32, the high bits are the sign, so SO=0):
// 1-2 ARM64 instructions, and the field's old SO clears by itself. With XER.SO=1 the JIT runs blocks translated for
// SO=1, which set SO in the fields they compute (jit.c so_cr).
typedef struct CPU {
    uint32_t gpr[32];
    uint64_t cr[8];
    uint32_t lr, ctr, pc;
    uint32_t ca, so, ov, xer_bc; // XER split into its fields
    uint32_t fpscr, reserve, reserve_addr;
    /* PT_FP_LAZY: the FPSCR status (FR, FI, FPRF) of the JIT's last fast FP operation is owed, not
     * stored: fs_kind (op 59: 0x20 | xo5, op 63: xo5; 0 = fpscr is complete) and its inputs fs_x[0..2] (A, B, C).
     * Byte 1 of fs_kind: FPCC written by fcmpu since. Every FPSCR read or partial write settles first
     * (ppc_fp_settle in C, the JIT's settle routine in the VM). fs_x[3], fs_lr, fs_save: that routine's scratch. */
    double fs_x[4];
    uint64_t fs_lr;
    uint32_t fs_save;
    uint16_t fs_kind, fs_pad;
    double fpr[32];
    uint8_t *mem;
    uint64_t icount; // for the interpreter/statistics
    int halted, exit_code;
    uint32_t jit_exit; // exit reason from JIT code
    uint64_t jit_link; // branch address for block linking
    void *jit_jc; // jump cache: JC {key, code, page generation}[65536] (jit.c)
    void *jit_codemap; // physical page -> 1: translated code may be there (jit.c; icbi in JIT code reads it)
    uint64_t crtab[16]; // CR nibble -> field value (for mtcrf in the JIT)
    uint64_t tb_a, tb_m; // TB in the VM: tb = tb_a + (CNTVCT * tb_m) >> 32 (mftb without an exit); tb_m = 0: none
    uint64_t dec_tbw;
    uint32_t dec_w, dec_pad; // DEC in the VM: dec = dec_w - (TB - dec_tbw)
    uint64_t ttbr[3]; // VM: TTBR0 per mode (real, DR PR=0, DR PR=1); rfi/sc in the JIT switch the mode themselves
    uint32_t icbi_lea,
            icbi_pad; // icbi: EA page of the previous one (| DR << 1 | 1); the same page again: the JIT does nothing
    uint32_t ttbr_k,
            srgen_ok; // VM: which one is in TTBR0 (written by the environment and JIT code); the srgen whose SRs the tables hold
    /* SR configurations the JIT code installs by itself on an rfi/mtmsr into DR=1 after mtsr in
     * real mode, without leaving the VM: bit s of vs_ok — slot s is usable (its ASIDs current), vs_sr its 16 SRs, vs_ttbr
     * its TTBR0 for DR=1 PR=0/1. vs_cur: the installed slot (written by the VM and the environment). */
    uint32_t vs_ok, vs_cur;
    uint64_t vs_ttbr[8][2];
    uint32_t vs_sr[8][16];
    // Supervisor state (750)
    uint32_t msr, sr[16];
    int sys; // 0: Linux stand; else sc, traps, illegal instructions are PPC exceptions: 1: stand bare metal (`b .` ends), 2: machine (firmware.c)
    uint32_t
            xgen; // bumped on a BAT/SDR1 (or machine state) change: the JIT and the host mappings are flushed completely
    uint32_t srgen; // bumped on an SR change: the JIT switches the segment (code and mappings of other SRs are kept)
    uint32_t tlbie_n,
            tlbie_ea[512]; // tlbie since the last JIT entry: the JIT drops these pages (more than 512: flush all)
    uint8_t *vbase; // Linux stand without base 0: data base for JIT code (memory, or the EA window when DR=1)
    uint32_t spr[1024]; // other SPRs by number: SRR0/1, SPRG, DAR, DSISR, SDR1, BAT, HID...
    // Environment (the machine or the stand). Outside preg[]: devices through env_mmio (bytes in memory order).
    PRegion preg[16];
    int npreg;
    void (*env_mmio)(struct CPU *c, uint32_t pa, uint8_t *buf, int n, int st);
    uint32_t (*env_spr)(struct CPU *c, int spr, int write,
                        uint32_t v); // DEC, TB (268/269/284/285); NULL: the default clock (interp.c)
    uint32_t (*env_irq)(struct CPU *c, uint32_t ack); // pending PEND_* after clearing ack; NULL: the default DEC
    void (*env_idle)(struct CPU *c); // MSR.POW: wait for an interrupt
    _Atomic uint32_t
            irq; // the environment requests an interrupt check: the JIT polls it on block entry and on backward branches
    uint32_t pend_masked,
            jit_tmp; // an interrupt waits for MSR.EE (mtmsr in the JIT exits only then); jit_tmp: scratch word for JIT code
    uint32_t cgen, icbi_n,
            icbi_ea[64]; // icbi ... isync (cgen++): physical addresses whose pages' code the JIT drops (more than 64: all code)
    uint64_t tb_off, dec0;
    uint32_t decv;
    int dec_armed, dec_pend; // default clock state (25 MHz)
    _Alignas(16) uint8_t vr[32][16];
    uint32_t vscr; /* G4 VMX, canonical BE lanes. */
    PtPpcTlb tlb[2][128]; // 7447A: data/instruction, 64 sets, two ways
    uint8_t tlb_lru[2][64];
    uint32_t xbsen;
    /* Separate external-control bus, selected by EAR.RID; never ordinary RAM. */
    void (*env_external)(struct CPU *, unsigned rid, uint32_t pa, uint8_t bytes[4], int store);
    int (*env_hypercall)(struct CPU *, uint32_t instruction);
    /* Stage-2 data abort on guarded (protected) guest memory, checked BEFORE the device route. ipa/far: the
     * faulting IPA and VA; pc and instruction: the native ARM access (0 outside the JIT pool). Returns 0 not guarded
     * memory, 1 resolved (the access re-runs, or was emulated and PC advanced), <0 fatal. */
    int (*env_guard)(struct CPU *, uint32_t ipa, uint64_t far, int write, uint64_t pc, uint32_t instruction);
    /* TLB invalidations by VA (every ASID) the VM runs before its next guest instruction: hvf.c queues the
     * pages whose L3 entry it removed or replaced, the VM entry and the fault-resume stub run them (jit.c tlbi_run). */
    uint32_t tlbi_n;
    uint64_t tlbi_save[2], tlbi_va[256];
    /* Interpreter compatibility boundary: the G4 prefix stays at its existing offsets.
     * A native 970 backend needs canonical 64-bit operands, not the G4 JIT.
     * SLB: valid in esid bit 63, EA >> 28 in the remainder; vsid is slbmte's RS.
     * Reservations are owned by bus970, never the G4 reserve fields. */
    int is970;
    uint32_t gpr_hi[32], lr_hi, ctr_hi, pc_hi, msr_hi, spr_hi[1024];
    struct {
        uint64_t esid, vsid;
    } slb[64];
    Pt970Bus *bus970;
    unsigned pir970;
    uint32_t exception970, unsupported970;
    int (*env_mmio970)(struct CPU *, uint64_t, uint8_t *, int, int);
    uint64_t (*env_ticks970)(struct CPU *); /* Monotonic virtual oscillator, before writable TB offset. */
    uint64_t biu_mode970;
    int (*env_ram970)(struct CPU *, uint64_t, unsigned, int); /* CPU owner: synchronize RAM before explicit accesses. */
} CPU;
enum {
    MSR_POW = 1 << 18,
    MSR_ILE = 1 << 16,
    MSR_EE = 1 << 15,
    MSR_PR = 1 << 14,
    MSR_FP = 1 << 13,
    MSR_ME = 1 << 12,
    MSR_FE0 = 1 << 11,
    MSR_SE = 1 << 10,
    MSR_BE = 1 << 9,
    MSR_FE1 = 1 << 8,
    MSR_IP = 1 << 6,
    MSR_IR = 1 << 5,
    MSR_DR = 1 << 4,
    MSR_PMM = 1 << 2,
    MSR_RI = 1 << 1,
    MSR_LE = 1,
    MSR_750 = 0x0005FF77
}; // MSR_750: implemented bits
enum {
    MSR_VEC = 1u << 25
};
static inline int ppc_has_vec(const CPU *c) {
    unsigned p = c->spr[287] >> 16;
    return p == 0x000c || p == 0x8003 || c->is970;
}
static inline int ppc_7447a(const CPU *c) {
    return c->spr[287] >> 16 == 0x8003;
}
enum {
    HID0_STEN = 0x01000000u,
    HID0_HIGH_BAT = 0x00800000u,
    HID0_XBSEN = 0x100u,
    PT_TLB_MISS = 1,
    PT_TLB_CHANGED = 2
};
static inline unsigned ppc_bat_count(const CPU *c) {
    return ppc_7447a(c) && (c->spr[1008] & HID0_HIGH_BAT) ? 8 : 4;
}
static inline uint32_t ppc_bat_mask(const CPU *c, uint32_t upper) {
    return ((upper & (ppc_7447a(c) && c->xbsen ? 0x1fffcu : 0x1ffcu)) << 15) | 0x1ffffu;
}
static inline uint32_t ppc_msr_mask(const CPU *c) {
    return MSR_750 | (ppc_has_vec(c) ? MSR_VEC : 0);
}
int ppc_store_window(const CPU *, uint32_t, uint32_t *);
void ppc_fp_settle(CPU *); /* FPSCR status owed by the JIT (fs_kind) into fpscr */
void ppc_altivec_stats(void);
int ppc_altivec(CPU *, uint32_t);
int ppc_altivec_mem(CPU *, uint32_t);
enum {
    SPR_DSISR = 18,
    SPR_DAR = 19,
    SPR_DEC = 22,
    SPR_SDR1 = 25,
    SPR_SRR0 = 26,
    SPR_SRR1 = 27
};

static inline uint32_t be32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return __builtin_bswap32(v);
}
static inline uint16_t be16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return __builtin_bswap16(v);
}
static inline void wbe32(uint8_t *p, uint32_t x) {
    x = __builtin_bswap32(x);
    memcpy(p, &x, 4);
}
static inline void wbe16(uint8_t *p, uint16_t x) {
    x = __builtin_bswap16(x);
    memcpy(p, &x, 2);
}
static inline void wbe64(uint8_t *p, uint64_t x) {
    x = __builtin_bswap64(x);
    memcpy(p, &x, 8);
}

static inline uint64_t cr_make(int lt, int gt, int eq, int so) {
    uint64_t v = (gt ? 1ull << 32 : 1ull << 63) | (lt ? 1ull << 62 : 0) | (eq ? 0 : 1);
    return v | ((uint64_t)(!gt ^ so) << 61);
}
static inline int cr_bit(const CPU *c, int b) { // b: 0..31, bit 0 = most significant (LT of field 0)
    uint64_t v = c->cr[b >> 2];
    switch (b & 3) {
    case 0:
        return (v >> 62) & 1;
    case 1:
        return (int64_t)v > 0;
    case 2:
        return (uint32_t)v == 0;
    default:
        return ((v >> 61) ^ (v >> 63)) & 1;
    }
}
static inline uint32_t cr_field(const CPU *c, int f) {
    return (cr_bit(c, 4 * f) << 3) | (cr_bit(c, 4 * f + 1) << 2) | (cr_bit(c, 4 * f + 2) << 1) | cr_bit(c, 4 * f + 3);
}
static inline void cr_set_field(CPU *c, int f, uint32_t n) {
    c->cr[f] = cr_make(n >> 3 & 1, n >> 2 & 1, n >> 1 & 1, n & 1);
}
static inline void cr_set_bit(CPU *c, int b, int v) {
    uint32_t n = cr_field(c, b >> 2), m = 8u >> (b & 3);
    cr_set_field(c, b >> 2, v ? n | m : n & ~m);
}
static inline uint32_t cr_get(const CPU *c) {
    uint32_t r = 0;
    for (int f = 0; f < 8; f++)
        r = r << 4 | cr_field(c, f);
    return r;
}
static inline uint32_t xer_get(const CPU *c) {
    return c->so << 31 | c->ov << 30 | c->ca << 29 | (c->xer_bc & 0x7f);
}
static inline void xer_set(CPU *c, uint32_t x) {
    c->so = x >> 31;
    c->ov = x >> 30 & 1;
    c->ca = x >> 29 & 1;
    c->xer_bc = x & 0x7f;
}

int interp_step(CPU *c); // one instruction; 0 = ok, otherwise a stop code
void ppc_exception(CPU *c, uint32_t vec, uint32_t srr0, uint32_t srr1_bits); // deliver a PPC exception
void ppc_irq(CPU *c); // check and deliver interrupts (external, DEC), honouring MSR.POW
/* Host address of [ra, ra+n), or NULL (device). For a store (st) also NULL in a read-only region: a ROM store must go
 * through env_mmio (ignored by the board), just like a stage-2 HVF fault. */
static inline uint8_t *pmem_at(CPU *c, uint32_t ra, uint32_t n, int st) {
    for (int i = 0; i < c->npreg; i++) {
        if (ra < c->preg[i].base)
            continue;
        uint64_t o = (uint64_t)ra - c->preg[i].base;
        if (o + n <= c->preg[i].size)
            return st && c->preg[i].read_only ? NULL : c->preg[i].host + o;
    }
    return NULL;
}
static inline uint8_t *pmem(CPU *c, uint32_t ra, uint32_t n) {
    return pmem_at(c, ra, n, 0);
}
static inline uint8_t *pmem_write(CPU *c, uint32_t ra, uint32_t n) {
    return pmem_at(c, ra, n, 1);
}
// mmu.c: 750 address translation and memory access through it (1 = exception delivered)
uint32_t pt_xlate(CPU *c, uint32_t ea, int acc, int pr, uint32_t *ra, int *wr);
void ppc_mmu_fault(CPU *, uint32_t ea, int access, uint32_t reason);
void ppc_tlb_load(CPU *, uint32_t ea, int instruction);
void ppc_tlb_invalidate(CPU *, uint32_t ea);
int ppc_external(CPU *, uint32_t ea, int store, uint32_t *value);
int ppc_ld(CPU *c, uint32_t ea, int n, uint64_t *v);
int ppc_st(CPU *c, uint32_t ea, int n, uint64_t v);
int ppc_ifetch(CPU *c, uint32_t pc, uint32_t *in);
// interp970.c / mmu970.c: the PPC970 (is970)
enum {
    PVR_970MP = 0x00440100u
};
#define PT970_SEGMENT UINT32_C(0x80000000)
#define PT970_SF (UINT64_C(1) << 63)
#define PT970_HV (UINT64_C(1) << 60)
static inline int ppc_970(const CPU *c) {
    return c->is970;
}
static inline uint64_t ppc_gpr64(const CPU *c, unsigned r) {
    return (uint64_t)c->gpr_hi[r] << 32 | c->gpr[r];
}
static inline void ppc_set_gpr64(CPU *c, unsigned r, uint64_t v) {
    c->gpr[r] = (uint32_t)v;
    c->gpr_hi[r] = (uint32_t)(v >> 32);
}
static inline uint64_t ppc_msr64(const CPU *c) {
    return (uint64_t)c->msr_hi << 32 | c->msr;
}
static inline uint64_t ppc_pc64(const CPU *c) {
    return (uint64_t)c->pc_hi << 32 | c->pc;
}
static inline uint64_t ppc_ea64(const CPU *c, uint64_t v) {
    return c->msr_hi & 0x80000000u ? v : (uint32_t)v;
}
static inline void ppc_set_pc64(CPU *c, uint64_t v) {
    v = ppc_ea64(c, v);
    c->pc = (uint32_t)v;
    c->pc_hi = (uint32_t)(v >> 32);
}
static inline uint64_t ppc_spr64(const CPU *c, unsigned r) {
    return (uint64_t)c->spr_hi[r] << 32 | c->spr[r];
}
static inline void ppc_set_spr64(CPU *c, unsigned r, uint64_t v) {
    c->spr[r] = (uint32_t)v;
    c->spr_hi[r] = (uint32_t)(v >> 32);
}
Pt970Bus *ppc970_bus_create(const Pt970Region *, unsigned);
/* Single CPU-owner device write, completed before that owner executes again.
 * Refuses a shared/SMP bus: those writers must use ppc970_bus_write instead. */
int ppc970_owner_write(Pt970Bus *, uint64_t, size_t);
int ppc970_agent_prepare(CPU *, uint64_t, unsigned, int);
void ppc970_bus_destroy(Pt970Bus *);
int ppc970_attach(CPU *, Pt970Bus *, unsigned pir, uint64_t boot_hior); /* Zero-initialized CPU; before execution. */
int ppc970_bus_read(Pt970Bus *, uint64_t, void *, unsigned);
int ppc970_bus_write(Pt970Bus *, uint64_t, const void *, unsigned); /* Every RAM writer, including DMA. */
void ppc970_bus_enter(Pt970Bus *, int exclusive);
void ppc970_bus_leave(Pt970Bus *);
int ppc970_bytes(CPU *, uint64_t, uint8_t *, unsigned, int);
void ppc970_alignment(CPU *, uint64_t, int);
int ppc970_zero(CPU *, uint64_t, unsigned);
int ppc970_data_check(CPU *, uint64_t, unsigned, int, int);
int ppc970_reservation(CPU *, uint64_t, unsigned, int, uint64_t *, int *);
int ppc970_execute(CPU *, uint32_t);
void ppc970_clock_init(CPU *);
void ppc970_irq(CPU *);
int ppc_fp_execute(CPU *, uint32_t); /* Shared FP semantics; 0 executed, -1 unsupported. */
int interp970_step(CPU *c);
void ppc970_mmu_fault(CPU *, uint64_t, int, uint32_t);
void ppc970_exception(CPU *c, uint32_t vec, uint64_t srr0, uint32_t srr1_bits);
uint32_t pt_xlate970(CPU *c, uint64_t ea, int acc, int pr, uint64_t *ra,
                     int *wr); // as pt_xlate; 0x80000000u: segment fault
int ppc970_ld(CPU *c, uint64_t ea, int n, uint64_t *v);
int ppc970_st(CPU *c, uint64_t ea, int n, uint64_t v);
int ppc970_ifetch(CPU *c, uint64_t pc, uint32_t *in);
// jit.c <-> environment (linux.c, hvf.c): with DR=1 data goes through host mappings that are filled on access faults
extern void (*jit_dmap)(CPU *c, int flush); // DR/PR changed (flush=0) or the translation changed (flush=1)
extern void (*jit_dsr)(CPU *c); // SRs changed: install segment mappings for the new SRs; NULL: flush all
extern void (*jit_dtlbie)(CPU *c, const uint32_t *ea,
                          int n); // tlbie: drop the EA pages in all segments; NULL: flush all
int jit_restore(CPU *c, const uint64_t *x,
                uint64_t pc); // PPC state before the instruction at host PC, from x0..x30; 0: not JIT code
uint32_t jit_mem_ea(uint32_t in, const uint64_t *x, int *n, int *st); // a JIT-code guest access: EA, size, store
extern void *jit_fault_exit; // Linux: where the thread resumes after a DSI from JIT code
extern unsigned long jit_mmio_n; // device accesses from JIT code (for the PT_CHECK comparison)
enum {
    JIT_FAULT = 5
}; // JIT exit reason: the DSI is already delivered
int sys_call(CPU *c); // `sc` (defined by the environment: Linux stand or VM)
void jit_run(CPU *c); // run until c->halted
void jit_snap(void); // PT_STATS: exits and frequent interpreter instructions since the last snapshot
extern int trace_unimpl;
