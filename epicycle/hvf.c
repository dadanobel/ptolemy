// The translator core inside an ARM64 VM (Hypervisor.framework, macOS).
// The host translates PPC to ARM64; the code runs in the VM at EL1 with the MMU on, where PPC guest addresses (0..4 GiB)
// are mapped one-to-one in 1 GB blocks. Exits (miss, sc, interpreter) are brk #0 (trapped debug exceptions).
// Bare metal (-s) and the machine: with MSR.DR=1 data goes through ARM tables that mirror the PowerPC MMU and are filled
// on access faults (like the EA windows of the Linux stand): root 0 is real mode (EA = RA), roots 1 and 2 are DR=1 for
// PR=0 and PR=1, each with its own ASID, so a mode switch needs no TLBI.
#include "ppc.h"
#include <Hypervisor/Hypervisor.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#define CK(x) do { hv_return_t r_ = (x); if (r_ != HV_SUCCESS) { fprintf(stderr, "FAIL %s = 0x%x\n", #x, r_); exit(1); } } while (0)
// A JIT access address is the 64-bit sum [xN, #imm]: guest EA + offset can cross 2^32 (base 0xFFFFFFFC + 4 is EA 0).
// So VA 4..8 GiB aliases EA 0..4 GiB in every root, and the engine sits above, from 8 GiB, out of the guest's reach.
#define ENG (8ull << 30)                          // the engine at 8 GiB
enum { O_PT = 0, O_VEC = 0x1000, O_TLBI = 0x1800, O_ENV = 0x4000, O_L2 = 0x10000, O_SLOT = 0x20000, O_MAPPED = 0x70000, O_JC = 0x200000, O_PGGEN = 0x400000,
       O_CODE = 0x800000, O_POOL = 72 << 20, ENG_SIZE = 88 << 20, POOLN = (ENG_SIZE - O_POOL) / 4096 };   // jump cache 2 MB, page generations 4 MB
_Static_assert(sizeof(CPU)<=O_L2-O_ENV,"CPU state overlaps ARM page tables");
typedef struct { uint64_t pc, code; uint32_t gen, pad; uint64_t pad2; } JC;
extern void *jit_vm_buf; extern size_t jit_vm_size; extern int64_t jit_delta; extern JC *jit_vm_jc; extern uint64_t jit_vm_jc_va;
extern uint32_t *jit_pggen; extern uint32_t jit_pggen_off, jit_mapped_off; void jit_mmio_note(uint64_t pc); uint32_t jit_ppc_pc(uint64_t pc);
extern int (*jit_vm_enter)(CPU *, void *); extern uint32_t *jit_tramp, *jit_tlbi_eret; extern int jit_zbase;
extern int jit_stats_on; extern uint64_t jit_tm[8]; uint64_t jit_now(void);
extern int guest_premapped; void guest_prepare(CPU *c, int n, char **args); void sys_prepare(CPU *c, const char *path); void sys_dump(CPU *c);
void sys_ticker(CPU *c);

static hv_vcpu_t vcpu; static hv_vcpu_exit_t *vx; static uint64_t env_va; static uint8_t *eng; static int npool;
static unsigned long n_fill, n_dsi, n_rm_tlbie, n_rm_fill, n_tlbi_tlbie, n_tlbi_fill;   /* n_rm_*: by removed() (new ASIDs), n_tlbi_*: by the VM queue; *_tlbie: tlbie/DBAT, *_fill: a fault replaced a valid entry */
/* PT_STATS: DSI causes [protection][store][user], their most frequent guest pcs, and whether a tlbie that left the VM
 * named the page of the last DSI. dsi_unmap (PT_DSI_UNMAP=0 turns it off, for A/B measurements): a protection DSI
 * drops the page's ARM entries at once, so the guest's own tlbie of it (XNU replaces that PTE) stays in the VM. */
static unsigned long d_cls[8], d_same, d_other, d_unmap; static uint32_t d_last = ~0u; static int d_self, dsi_unmap = -1;
static struct { uint32_t pc; unsigned long n; } dsi_h[64];   /* the most frequent DSI pcs */
static struct { uint32_t pg; unsigned long n; uint64_t ns; uint32_t ppc, lr; } mmio_h[256];   // PT_STATS: JIT device accesses by address (16-byte units) and their time (since the last snapshot)
static uint64_t sreg(hv_sys_reg_t r) { uint64_t v; hv_vcpu_get_sys_reg(vcpu, r, &v); return v; }

// --- ARM tables for DR=1 ---
/* NSLOT configurations of all 16 SRs, each with its own roots 1/2, live L2 tables and ASIDs. XNU reloads the segment
 * registers on every exception from user mode and back (Xbench Quartz: up to 126 000 switches a second); a
 * configuration seen before comes back by TTBR0 alone, without a copy and with its TLB entries. A slot fills an L2
 * entry it lacks from the set at the first fault (map_page). Removing or replacing a valid L3 entry bumps clear_gen: the
 * current slot takes new ASIDs at once, every other one when it is next installed. PT_SR_SLOTS=1..8 sets the slot count. */
enum { NSLOT = 8, SLOT_PAGES = 10 };                             // a slot: roots 1, 2 and 8 live L2 pages (O_SLOT .. 0x70000)
static struct Slot { uint32_t sr[16]; unsigned asid[2]; uint64_t gen, used; } slot[NSLOT];   // used: LRU tick, 0 free
static int nslot = NSLOT; static uint64_t clear_gen = 1, slot_tick, flushes, slot_hit, slot_miss;
#define ENVC ((CPU *)(eng + O_ENV))
#define cur (ENVC->vs_cur)                                       // the installed slot: the VM's JIT code switches it too
static uint8_t *slot_page(int s, int n) { return eng + O_SLOT + 0x1000 * (SLOT_PAGES * s + n); }
static uint64_t *root(int k) { return (uint64_t *)(k ? slot_page(cur, k - 1) : eng + O_PT); }
static uint64_t ipa(const void *p) { return ENG + (uint64_t)((const uint8_t *)p - eng); }
static void vm_run_stub(uint64_t va, int imm) {                 // run a stub in the VM up to brk #imm
    CK(hv_vcpu_set_reg(vcpu, HV_REG_PC, va)); CK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5));
    do CK(hv_vcpu_run(vcpu)); while (vx->reason != HV_EXIT_REASON_EXCEPTION);
    if (vx->exception.syndrome >> 26 != 0x3C || (vx->exception.syndrome & 0xffff) != (uint64_t)imm) { fprintf(stderr, "HVF: unexpected stub exit %llx\n", vx->exception.syndrome); exit(3); }
}
static void vm_tlbi(void) { vm_run_stub(ENG + O_TLBI, 1); }
// Roots 1 and 2 (DR=1 for PR=0/1) of each slot point to permanent "live" L2 tables (one per 1 GB; the aliases point to
// the same ones). A PPC segment (256 MB) is 128 L2 entries. Each SR value keeps its own set of these 128 entries (the
// L3 tables are shared): installing a new configuration copies 1 KB per segment into the slot's live tables and takes
// new ASIDs; nothing is flushed or refilled, so the kernel and processes keep their mappings at the same time. A
// configuration seen before is installed without a copy. tlbie removes a page in all sets.
#define NST 4096
static struct St { uint32_t srv, used; uint64_t l2[2][128], has[2]; } st[NST];   // has: 2 MB chunks that have an L3 table
static uint16_t chl[128][NST / 2], chn[128];                    // segment chunk -> sets with an L3 table there (for tlbie)
static int nst;                                                 // sets in use
static unsigned asid_next = 4, asid_last = 253;                // ASID 1: root 0; asid_last: 65533 with 16-bit ASIDs (pt_hv_init)
/* Pages within a segment (EA bits 12..27) for which some set may hold an L3 entry. tlbie of a page no set maps
 * changes nothing, and then needs no new ASID either: XNU's physical copies move DBAT0/1 about 10 000 times a second
 * in Xbench UI, each listing the 32-page window twice (1.3 M tlbie per second, 39% of the vCPU). */
#define mapped ((uint64_t *)(eng + O_MAPPED))                   /* 8 KiB in the engine area: the JIT's tlbie reads it too */
enum { MAPPED_BYTES = 65536 / 8 };
static uint64_t *live_at(int s, int k, int g) { return (uint64_t *)slot_page(s, 2) + 512 * (2 * g + k - 1); }
static uint64_t *live(int k, int g) { return live_at(cur, k, g); }
static uint64_t *l3tab(uint64_t d) { return (uint64_t *)(eng + ((d & 0xFFFFFFFFF000ull) - ENG)); }
static void set_ttbr(CPU *c);
static void new_asid(void);
static void flush_maps(void) {                                  // start over (BAT/SDR1, table pool or sets exhausted)
    for (int s = 0; s < NSLOT; s++) { memset(live_at(s, 1, 0), 0, 8 * 4096); slot[s].used = 0; slot[s].gen = 0; }
    ENVC->vs_ok = 0;
    npool = 0; flushes++; ENVC->tlbi_n = 0;                    // vm_tlbi below drops every translation
    memset(st, 0, sizeof st); nst = 0; memset(chn, 0, sizeof chn); memset(mapped, 0, MAPPED_BYTES);
    cur = 0; memcpy(slot[0].sr, ((CPU *)(eng + O_ENV))->sr, sizeof slot[0].sr); slot[0].used = ++slot_tick;   // empty sets: empty tables
    asid_next = 4; vm_tlbi(); new_asid(); set_ttbr((CPU *)(eng + O_ENV));   // TTBR0 and c->ttbr with the new ASIDs
}
static struct St *store(uint32_t srv) {                         // the set for an SR value (created if missing)
    for (uint32_t i = srv * 0x9E3779B1u >> 20;; i = (i + 1) & (NST - 1)) {
        if (st[i].used && st[i].srv == srv) return &st[i];
        if (st[i].used) continue;
        if (nst == NST / 2) { flush_maps(); return store(srv); }   // too many sets: start over
        st[i].used = 1; st[i].srv = srv; nst++; return &st[i];
    }
}
static void set_ttbr(CPU *c) {
    int k = c->msr & MSR_DR ? 1 + !!(c->msr & MSR_PR) : 0;
    c->ttbr[0] = ipa(root(0)) | 1ull << 48;                      // also for rfi/sc in the JIT
    for (int i = 1; i < 3; i++) c->ttbr[i] = ipa(root(i)) | (uint64_t)slot[cur].asid[i - 1] << 48;
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, c->ttbr[k])); c->ttbr_k = (uint32_t)k;
}
static void new_asid(void) {                                    // old TLB entries of the current configuration are no longer visible
    if (asid_next + 1 > asid_last) { vm_tlbi(); asid_next = 4; for (int s = 0; s < NSLOT; s++) slot[s].gen = 0; ENVC->vs_ok = 0; }   // ASID numbers are reused
    slot[cur].asid[0] = asid_next++; slot[cur].asid[1] = asid_next++; slot[cur].gen = clear_gen;
    CPU *e = ENVC;                                              // the slot, as the JIT code may install it again by itself
    for (int k = 0; k < 2; k++) e->vs_ttbr[cur][k] = ipa(slot_page(cur, k)) | (uint64_t)slot[cur].asid[k] << 48;
    memcpy(e->vs_sr[cur], slot[cur].sr, sizeof e->vs_sr[cur]); e->vs_ok |= 1u << cur;
}
static void removed(void) { clear_gen++; ENVC->vs_ok = 0; new_asid(); }   // a valid L3 entry went away: no slot may keep it in its TLB
/* Instead of new ASIDs (which lose every slot's TLB; Xbench Quartz removed entries ~17 000 times a second), page q of
 * set s goes to the VM's queue of invalidations by VA for every ASID: in each segment to which some slot gives this SR
 * value, at both aliases (EA, EA + 4 GiB). 0: the queue is full, the caller falls back to removed(). PT_VM_TLBI=0:
 * always removed(). */
static int vm_tlbi_on = -1;
static int queue_page(const struct St *s, uint32_t q) {
    if (!vm_tlbi_on) return 0;
    CPU *e = ENVC; unsigned segs = 0;
    for (int x = 0; x < nslot; x++) if (slot[x].used) for (int n = 0; n < 16; n++) if (slot[x].sr[n] == s->srv) segs |= 1u << n;
    if (e->tlbi_n + 2 * (unsigned)__builtin_popcount(segs) > 256) return 0;
    for (int n = 0; n < 16; n++) if (segs >> n & 1) {
        uint64_t va = (uint64_t)n << 28 | (uint64_t)q << 12;
        e->tlbi_va[e->tlbi_n++] = va >> 12; e->tlbi_va[e->tlbi_n++] = (va + (4ull << 30)) >> 12;
    }
    return 1;
}
static void vm_dmap(CPU *c, int flush) { if (flush) flush_maps(); set_ttbr(c); }
static void vm_dsr(CPU *c) {                                    // SRs changed: install the configuration of their values
    slot[cur].used = ++slot_tick;                               // (the JIT code may have installed it)
    if (!memcmp(c->sr, slot[cur].sr, sizeof slot[cur].sr)) return;
    int f = -1;
    for (int s = 0; s < nslot && f < 0; s++) if (slot[s].used && !memcmp(slot[s].sr, c->sr, sizeof c->sr)) f = s;
    if (f >= 0) slot_hit++;
    else {                                                      // new: the least recently installed slot takes the sets of these values
        slot_miss++; f = 0; for (int s = 1; s < nslot; s++) if (slot[s].used < slot[f].used) f = s;
        uint64_t fl = flushes; ENVC->vs_ok &= ~(1u << f);           // its tables change: the VM must not install it meanwhile
        for (int n = 0; n < 16; n++) {
            struct St *s = store(c->sr[n]); if (flushes != fl) return;   // all flushed: slot 0 already holds these values
            memcpy(live_at(f, 1, n >> 2) + (n & 3) * 128, s->l2[0], 1024); memcpy(live_at(f, 2, n >> 2) + (n & 3) * 128, s->l2[1], 1024);
        }
        memcpy(slot[f].sr, c->sr, sizeof c->sr); slot[f].gen = 0;   // its ASIDs named the previous configuration
    }
    cur = f; slot[f].used = ++slot_tick;
    if (slot[f].gen != clear_gen) new_asid();
    set_ttbr(c);
}
static void vm_dtlbie(CPU *c, const uint32_t *ea, int n) {      // remove the EA pages in all sets
    int cleared = 0;
    for (int i = 0; i < n; i++) {
        uint32_t q = ea[i] >> 12 & 0xffff, b = q >> 9, p = q & 511;
        if (!(mapped[q >> 6] >> (q & 63) & 1)) continue;
        if (jit_stats_on && !d_self) { if (ea[i] >> 12 == d_last) d_same++; else d_other++; }
        mapped[q >> 6] &= ~(1ull << (q & 63));
        for (int j = 0; j < chn[b]; j++) for (int k = 0; k < 2; k++) { uint64_t d = st[chl[b][j]].l2[k][b];
            if (d & 1) { uint64_t *e = l3tab(d) + p; if (*e & 1) { *e = 0; if (!queue_page(&st[chl[b][j]], q)) cleared = 1; } } }
    }
    /* Every removal of a valid entry gets new ASIDs, so no TLB holds a translation of a page that no table maps. */
    if (cleared) { removed(); set_ttbr(c); n_rm_tlbie++; ENVC->tlbi_n = 0; }   /* new ASIDs cover the queue too */
    else if (ENVC->tlbi_n) n_tlbi_tlbie++;
}
static int map_page(int k, uint32_t ea, uint32_t ra, int wr) {  // -1: pool exhausted; 1: a valid entry was replaced (2: and queued for the VM)
    struct St *s = store(slot[cur].sr[ea >> 28]);               // before allocating: store() may flush everything
    uint64_t *l2e = live(k, ea >> 30 & 3) + (ea >> 21 & 511);
    if (!(*l2e & 1) && (s->l2[k - 1][ea >> 21 & 127] & 1))
        *l2e = s->l2[k - 1][ea >> 21 & 127];   /* another SR with this value built it: the same VSID
                                                  is the same translation, and a second table would escape vm_dtlbie */
    if (!(*l2e & 1)) {
        if (npool == POOLN) return -1;
        uint64_t *t = (uint64_t *)(eng + O_POOL) + 512 * npool++; memset(t, 0, 4096);
        uint32_t b = ea >> 21 & 127; *l2e = ipa(t) | 3; s->l2[k - 1][b] = *l2e;
        if (!(s->has[b >> 6] >> (b & 63) & 1)) { s->has[b >> 6] |= 1ull << (b & 63); chl[b][chn[b]++] = (uint16_t)(s - st); }
    }
    uint64_t *e = l3tab(*l2e) + (ea >> 12 & 511), old = *e; uint32_t q = ea >> 12 & 0xffff; mapped[q >> 6] |= 1ull << (q & 63);
    *e = (ra & ~0xFFFull) | 3ull << 53 | 1u << 11 | 1u << 10 | 3u << 8 | (wr ? 0 : 1u << 7) | 3;   // XN, nG, AF, ISH, RO?, page
    if (!(old & 1) || old == *e) return 0;                     // the same entry again (a slot's missing L2 link): nothing replaced
    return queue_page(s, q) ? 2 : 1;
}
static int dfault(CPU *c) {                                     // data abort in JIT code: 1 = retry the access, 0 = DSI delivered, -1 = not handled
    uint64_t x[31], elr = sreg(HV_SYS_REG_ELR_EL1), spsr = sreg(HV_SYS_REG_SPSR_EL1);
    for (int i = 0; i < 31; i++) hv_vcpu_get_reg(vcpu, HV_REG_X0 + i, &x[i]);
    int n, st, k = 1 + !!(c->msr & MSR_PR), tlbi = 0;
    if (!(c->msr & MSR_DR) || elr - (uint64_t)jit_delta < (uint64_t)jit_vm_buf || elr - (uint64_t)jit_delta >= (uint64_t)jit_vm_buf + jit_vm_size) return -1;
    if (sreg(HV_SYS_REG_FAR_EL1) >> 33) return -1;                    // EA below 0 (base < 256, offset < 0): a VA outside the aliases, not supported
    uint32_t ea = jit_mem_ea(*(uint32_t *)(elr - jit_delta), x, &n, &st), pg[2] = {ea, (ea + n - 1) & ~0xFFFu};
    for (int i = 0; i < 1 + ((pg[1] ^ ea) >> 12 != 0); i++) {     // the access may touch two pages
        uint32_t ra, why; int wr; why = pt_xlate(c, pg[i], st, k - 1, &ra, &wr);
        if (why) {
            int found = jit_restore(c, x, elr); const uint32_t *ip = (const uint32_t *)(elr - jit_delta);
            if (pg[i] < 0x1000 && (c->msr & MSR_PR)) { static unsigned shown; if (shown++ < 4) {   // diagnostics: which ARM access produced it, host registers, code around it
                fprintf(stderr, "ptolemy: dfault ARM %08x at code+%llx ea %08x size %d store %d FAR %llx block %d pc %08x\nptolemy:   code", ip[0],
                        (unsigned long long)((const uint8_t *)ip - (const uint8_t *)jit_vm_buf), ea, n, st, (unsigned long long)sreg(HV_SYS_REG_FAR_EL1), found, c->pc);
                for (int j = -8; j <= 2; j++) fprintf(stderr, j ? " %08x" : " [%08x]", ip[j]);
                for (int r = 0; r < 31; r++) { if (!(r % 8)) fprintf(stderr, "\nptolemy:   x%-2d", r); fprintf(stderr, " %llx", (unsigned long long)x[r]); }
                fputc('\n', stderr); } }
            if (jit_stats_on) { d_cls[(why >> 27 & 1) << 2 | (unsigned)st << 1 | !!(c->msr & MSR_PR)]++; d_last = pg[i] >> 12;
                for (uint32_t h = c->pc * 0x9E3779B1u >> 26, j = 0; j < 64; j++, h = (h + 1) & 63)
                    if (!dsi_h[h].n || dsi_h[h].pc == c->pc) { dsi_h[h].pc = c->pc; dsi_h[h].n++; break; } }
            if (dsi_unmap < 0) dsi_unmap = !getenv("PT_DSI_UNMAP") || atoi(getenv("PT_DSI_UNMAP"));
            if (why & 0x08000000 && dsi_unmap) { uint32_t e = pg[i]; d_self = 1; vm_dtlbie(c, &e, 1); d_self = 0; d_unmap++; }
            ppc_mmu_fault(c,pg[i],st,why); n_dsi++;
            return 0;
        }
        int m = map_page(k, pg[i], ra, wr);
        if (m < 0) { flush_maps(); tlbi = 0; i = -1; continue; }  // table pool exhausted: start over
        tlbi |= m; n_fill++;
    }
    if (tlbi & 1) { removed(); set_ttbr(c); n_rm_fill++; ENVC->tlbi_n = 0; }   // an entry existed (read-only): drop the old one from the TLB
    if (ENVC->tlbi_n) { CK(hv_vcpu_set_reg(vcpu, HV_REG_PC, (uint64_t)jit_tlbi_eret + jit_delta)); CK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5)); n_tlbi_fill++; return 1; }   // the queue, then eret to the access
    CK(hv_vcpu_set_reg(vcpu, HV_REG_PC, elr)); CK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, spsr));
    return 1;
}

/* PT_PROF=1 (stand): guest-code PC samples. A ticker thread forces a vCPU exit every ~1 ms while the vCPU is inside
 * hv_vcpu_run; jit.c maps the ARM PC to a block and PPC instruction (printed with PT_STATS). */
static _Atomic int vm_in_run; static int vm_prof = -1; void jit_prof_sample(CPU *c, uint64_t pc);
static void *prof_tick(void *u) { (void)u; for (;;) { usleep(1000); if (atomic_load(&vm_in_run)) hv_vcpus_exit(&vcpu, 1); } return NULL; }
static int ttbr_chk = -1; static unsigned long ttbr_bad;            // PT_TTBRCHK=1: check TTBR0 == c->ttbr[ttbr_k] at entry; =2: also fix it
static int vm_enter(CPU *c, void *code) {
    if (ttbr_chk < 0) ttbr_chk = getenv("PT_TTBRCHK") ? atoi(getenv("PT_TTBRCHK")) : 0;
    if (ttbr_chk) { uint64_t t = sreg(HV_SYS_REG_TTBR0_EL1), w = c->ttbr[c->ttbr_k & 3];
                    if (t != w && ttbr_bad++ < 8) fprintf(stderr, "ptolemy: TTBR0 %llx, expected %llx (k %u, MSR %08x, pc %08x)\n", (unsigned long long)t, (unsigned long long)w, c->ttbr_k, c->msr, c->pc);
                    if (t != w && ttbr_chk == 2) CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, w)); }
    CK(hv_vcpu_set_reg(vcpu, HV_REG_X0, env_va));
    CK(hv_vcpu_set_reg(vcpu, HV_REG_X1, (uint64_t)code + jit_delta));
    CK(hv_vcpu_set_reg(vcpu, HV_REG_PC, (uint64_t)jit_tramp + jit_delta));
    CK(hv_vcpu_set_reg(vcpu, HV_REG_CPSR, 0x3c5));
    if (vm_prof < 0 && (vm_prof = !!getenv("PT_PROF"))) { pthread_t t; pthread_create(&t, NULL, prof_tick, NULL); }
    for (;;) {
        atomic_store(&vm_in_run, 1); CK(hv_vcpu_run(vcpu)); atomic_store(&vm_in_run, 0);
        if (vm_prof && vx->reason == HV_EXIT_REASON_CANCELED) { uint64_t pc; hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc); jit_prof_sample(c, pc); }
        if (vx->reason != HV_EXIT_REASON_EXCEPTION) continue;          // vtimer/cancel: continue
        uint64_t esr = vx->exception.syndrome; uint32_t ec = esr >> 26 & 0x3f;
        if (ec == 0x24 && c->env_guard && vx->exception.physical_address >> 32 == 0) {   /* guarded memory first */
            uint64_t pc; hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
            uint32_t in = pc - (uint64_t)jit_delta >= (uint64_t)jit_vm_buf && pc - (uint64_t)jit_delta + 4 <= (uint64_t)jit_vm_buf + jit_vm_size ? *(uint32_t *)(pc - jit_delta) : 0;
            int g = c->env_guard(c, (uint32_t)vx->exception.physical_address, vx->exception.virtual_address, (int)(esr >> 6 & 1), pc, in);
            if (g > 0) continue;
            if (g < 0) { fprintf(stderr, "HVF: guarded access failed at IPA %llx, ARM %08x\n", vx->exception.physical_address, in); exit(3); }
        }
        if (ec == 0x24 && esr >> 24 & 1 && vx->exception.physical_address >> 32 == 0 && c->env_mmio) {   // device: an IPA without memory
            int n = 1 << (esr >> 22 & 3), st = esr >> 6 & 1, rt = esr >> 16 & 31; uint64_t v = 0, pc, tm = jit_stats_on ? jit_now() : 0; uint8_t b[8] = {0};
            if (st && rt != 31) hv_vcpu_get_reg(vcpu, HV_REG_X0 + rt, &v);
            for (int i = 0; i < n; i++) b[i] = (uint8_t)(v >> 8 * i);                // bytes as str would store them
            c->env_mmio(c, (uint32_t)vx->exception.physical_address, b, n, st);
            if (!st && rt != 31) { v = 0; for (int i = 0; i < n; i++) v |= (uint64_t)b[i] << 8 * i; CK(hv_vcpu_set_reg(vcpu, HV_REG_X0 + rt, v)); }
            hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc); CK(hv_vcpu_set_reg(vcpu, HV_REG_PC, pc + 4)); jit_mmio_n++; jit_mmio_note(pc); uint64_t dt = jit_stats_on ? jit_now() - tm : 0; jit_tm[6] += dt;
            for (uint32_t pg = (uint32_t)vx->exception.physical_address >> 4, i = pg * 0x9E3779B1u >> 24, k = 0; k < 256; k++, i = (i + 1) & 255)
                if (!mmio_h[i].n || mmio_h[i].pg == pg) { mmio_h[i].pg = pg; mmio_h[i].ns += dt;   /* the guest pc and LR, every 64th access */
                    if (!(mmio_h[i].n++ & 63) && jit_stats_on) { uint64_t lr = 0; hv_vcpu_get_reg(vcpu, HV_REG_X0 + 14, &lr); mmio_h[i].ppc = jit_ppc_pc(pc); mmio_h[i].lr = (uint32_t)lr; } break; }
            continue;
        }
        if (ec == 0x3C && (esr & 0xffff) == 20 && sreg(HV_SYS_REG_ESR_EL1) >> 26 == 0x25) {   // data abort at EL1 (vector 0x200)
            uint64_t tm = jit_stats_on ? jit_now() : 0; int r = dfault(c); if (jit_stats_on) jit_tm[5] += jit_now() - tm;
            if (r == 1) continue;
            if (r == 0) return JIT_FAULT;
        }
        if (ec == 0x3C && (esr & 0xffff) == 0) {                        // normal block exit: brk #0
            static int once; if (!once++ && getenv("PT_VMREGS"))
                fprintf(stderr, "HVF accepted: SCTLR=0x%llx TCR=0x%llx MAIR=0x%llx TTBR0=0x%llx\n", sreg(HV_SYS_REG_SCTLR_EL1), sreg(HV_SYS_REG_TCR_EL1), sreg(HV_SYS_REG_MAIR_EL1), sreg(HV_SYS_REG_TTBR0_EL1));
            return c->jit_exit;
        }
        uint64_t pc, elr = sreg(HV_SYS_REG_ELR_EL1); hv_vcpu_get_reg(vcpu, HV_REG_PC, &pc);
        fprintf(stderr, "HVF: PPC pc=%08x msr=%08x; TTBR0=0x%llx; root 0[0]=0x%llx", c->pc, c->msr, sreg(HV_SYS_REG_TTBR0_EL1), (unsigned long long)root(0)[0]);
        if (elr - (uint64_t)jit_delta >= (uint64_t)jit_vm_buf && elr - (uint64_t)jit_delta < (uint64_t)jit_vm_buf + jit_vm_size)
            fprintf(stderr, "; ARM instruction 0x%08x", *(uint32_t *)(elr - jit_delta));
        fprintf(stderr, "\n");
        uint64_t r[32]; for (int i = 0; i < 31; i++) hv_vcpu_get_reg(vcpu, HV_REG_X0 + i, &r[i]);
        fprintf(stderr, "HVF: x0=%llx x15=%llx x16=%llx x17=%llx x19=%llx x27=%llx x28=%llx\n", r[0], r[15], r[16], r[17], r[19], r[27], r[28]);
        fprintf(stderr, "HVF: exception EC=0x%x ESR=0x%llx PC=0x%llx FAR_EL1=0x%llx ELR_EL1=0x%llx ESR_EL1=0x%llx IPA=0x%llx\n",
                ec, esr, pc, sreg(HV_SYS_REG_FAR_EL1), sreg(HV_SYS_REG_ELR_EL1), sreg(HV_SYS_REG_ESR_EL1), vx->exception.physical_address);
        exit(3);
    }
}

// The VM, the engine inside it (tables, vectors, code, CPU) and this thread's vCPU. The caller maps guest memory (pt_hv_map).
CPU *pt_hv_init(int sys) {                                            // sys: bare metal, DR=1 data through the ARM tables
    CK(hv_vm_create(NULL));
    eng = mmap(0, ENG_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (eng == MAP_FAILED) { perror("mmap"); exit(1); }
    CK(hv_vm_map(eng, ENG, ENG_SIZE, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC));

    // Level-1 tables (4K granule, 36-bit VA), 1 GB blocks: Normal WB, Inner Shareable, AF, EL1 RW.
    // Root 0: VA 0..4 GiB and 4..8 GiB = IPA 0..4 GiB (nG, under its own ASID); every root maps the engine at 8 GiB (global).
    for (uint64_t i = 0; i < 8; i++) root(0)[i] = (i & 3) << 30 | 1ull << 54 | 1u << 11 | 1u << 10 | 3u << 8 | 1;
    root(0)[8] = ENG | 1ull << 54 | 1u << 10 | 3u << 8 | 1;
    for (int s = 0; s < NSLOT; s++) for (int k = 1; k <= 2; k++) {   // roots 1/2 of each slot: EA and alias point to its live L2 tables
        uint64_t *r = (uint64_t *)slot_page(s, k - 1); r[8] = root(0)[8];
        for (int g = 0; g < 4; g++) r[g] = r[g + 4] = ipa(live_at(s, k, g)) | 3;
    }
    { const char *v = getenv("PT_SR_SLOTS"); if (v && atoi(v) >= 1 && atoi(v) <= NSLOT) nslot = atoi(v); }
    vm_tlbi_on = !getenv("PT_VM_TLBI") || atoi(getenv("PT_VM_TLBI"));   /* =0: removed entries by new ASIDs only (for A/B measurements) */
    // EL1 vectors: each entry is brk #(16+n), so an exception inside the VM is visible on the host (data abort: n = 4).
    for (int n = 0; n < 16; n++) ((uint32_t *)(eng + O_VEC))[n * 32] = 0xD4200000 | (uint32_t)(16 + n) << 5;
    uint32_t *tl = (uint32_t *)(eng + O_TLBI); tl[0] = 0xD5033A9F; tl[1] = 0xD508831F; tl[2] = 0xD5033B9F; tl[3] = 0xD5033FDF; tl[4] = 0xD4200020;   // dsb ishst; tlbi vmalle1is (the vCPU may change cores); dsb ish; isb; brk #1

    CK(hv_vcpu_create(&vcpu, &vx, NULL));
    CK(hv_vcpu_set_trap_debug_exceptions(vcpu, true));                          // brk -> exit to the host
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_MAIR_EL1, 0xFF));                          // attr0 = Normal WB
    /* 16-bit ASIDs (TCR.AS) where the core has them (Apple silicon does): a full TLBI every 32 765 ASID changes
     * instead of every 125 (SR switches reach 126 000 a second in Xbench Quartz). */
    uint64_t mmfr0 = sreg(HV_SYS_REG_ID_AA64MMFR0_EL1); int as16 = (mmfr0 >> 4 & 15) == 2; if (as16) asid_last = 65533;
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TCR_EL1, 28 | 1u << 8 | 1u << 10 | 3u << 12 | 0u << 14 | 1u << 23 | 2ull << 32 | (uint64_t)as16 << 36));
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_TTBR0_EL1, (ENG + O_PT) | 1ull << 48));        // root 0, ASID 1
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_VBAR_EL1, ENG + O_VEC));
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_CPACR_EL1, 3u << 20));                     // FPEN: FP/SIMD at EL1 (lfs, fmr... in the JIT)
    CK(hv_vcpu_set_reg(vcpu, HV_REG_FPCR, 0));                                         // round to nearest, no flush-to-zero: as the interpreter
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SP_EL1, ENG + O_JC));                      // no stack needed (exits are brk)
    uint64_t sctlr = sreg(HV_SYS_REG_SCTLR_EL1);
    CK(hv_vcpu_set_sys_reg(vcpu, HV_SYS_REG_SCTLR_EL1, sctlr | 1 | 1u << 2 | 1u << 12));  // M, C, I

    jit_vm_buf = eng + O_CODE; jit_vm_size = O_POOL - O_CODE; jit_delta = (int64_t)(ENG + O_CODE) - (int64_t)(eng + O_CODE);
    jit_vm_jc = (JC *)(eng + O_JC); jit_vm_jc_va = ENG + O_JC; jit_vm_enter = vm_enter; jit_zbase = 1;
    jit_pggen = (uint32_t *)(eng + O_PGGEN); jit_pggen_off = O_PGGEN - O_ENV; if (sys) jit_mapped_off = O_MAPPED - O_ENV;
    env_va = ENG + O_ENV; if (sys) { jit_dmap = vm_dmap; jit_dsr = vm_dsr; jit_dtlbie = vm_dtlbie; }
    return (CPU *)(eng + O_ENV);
}
// Guest memory at IPA = PowerPC physical address; anything unmapped is a device (stage-2, vm_enter). Multiples of 16 KB.
void pt_hv_map(void *host, uint32_t base, uint64_t size, int ro) {
    CK(hv_vm_map(host, base, size, HV_MEMORY_READ | HV_MEMORY_EXEC | (ro ? 0 : HV_MEMORY_WRITE)));
}
void pt_hv_unmap(uint32_t base, uint64_t size) { CK(hv_vm_unmap(base, size)); }
/* Stage-2 permissions of a mapped range (16 KiB granules): prot bit 1 read, 2 write. 0 on success. */
int pt_hv_protect(uint64_t ipa, uint64_t size, unsigned prot) {
    return hv_vm_protect(ipa, size, (prot & 1 ? HV_MEMORY_READ | HV_MEMORY_EXEC : 0) | (prot & 2 ? HV_MEMORY_WRITE : 0)) == HV_SUCCESS ? 0 : -1;
}
hv_vcpu_t pt_hv_vcpu(void) { return vcpu; }   /* the calling machine thread's vCPU, for a guarded-access emulation */
/* A guest pause of ns host time: CNTVCT (the JIT's fast TB/DEC) skips it. vCPU thread only. */
void pt_hv_vtimer_shift(uint64_t ns) {
    mach_timebase_info_data_t t; mach_timebase_info(&t); uint64_t off = 0;
    CK(hv_vcpu_get_vtimer_offset(vcpu, &off)); CK(hv_vcpu_set_vtimer_offset(vcpu, off + (uint64_t)((unsigned __int128)ns * t.denom / t.numer)));
}
uint64_t pt_hv_cntvct(void) { uint64_t off = 0; hv_vcpu_get_vtimer_offset(vcpu, &off); return mach_absolute_time() - off; }   // the guest's CNTVCT
void pt_hv_stats(void) {
    if (ttbr_bad) fprintf(stderr, "ptolemy: TTBR0 mismatch at entry: %lu times\n", ttbr_bad);
    fprintf(stderr, "ptolemy: JIT with DR=1: removed entries by new ASIDs: tlbie %lu, fault %lu; by VA in the VM: tlbie %lu, fault %lu\n", n_rm_tlbie, n_rm_fill, n_tlbi_tlbie, n_tlbi_fill);
    fprintf(stderr, "ptolemy: JIT with DR=1: mapped pages %lu, DSI %lu; SR configurations: %llu reused, %llu filled (%d slots); JIT device accesses %lu; longest (address x count/ms):",
            n_fill, n_dsi, (unsigned long long)slot_hit, (unsigned long long)slot_miss, nslot, jit_mmio_n);
    for (int t = 0; t < 10; t++) { int b = -1; for (int i = 0; i < 256; i++) if (mmio_h[i].n && (b < 0 || mmio_h[i].ns > mmio_h[b].ns)) b = i;
                                  if (b < 0) break; fprintf(stderr, " %08x×%lu/%llu@%08x<%08x", mmio_h[b].pg << 4, mmio_h[b].n, (unsigned long long)(mmio_h[b].ns / 1000000), mmio_h[b].ppc, mmio_h[b].lr); mmio_h[b].n = 0; }
    fputc('\n', stderr); memset(mmio_h, 0, sizeof mmio_h);
    if (!jit_stats_on) return;
    fprintf(stderr, "ptolemy: DSI (kernel/user): no PTE load %lu/%lu store %lu/%lu, protection load %lu/%lu store %lu/%lu; dropped at once %lu; "
            "tlbie leaving the VM: last DSI page %lu, other %lu; pcs:", d_cls[0], d_cls[1], d_cls[2], d_cls[3], d_cls[4], d_cls[5], d_cls[6], d_cls[7],
            d_unmap, d_same, d_other);
    for (int t = 0; t < 6; t++) { int b = -1; for (int i = 0; i < 64; i++) if (dsi_h[i].n && (b < 0 || dsi_h[i].n > dsi_h[b].n)) b = i;
                                  if (b < 0) break; fprintf(stderr, " %08x×%lu", dsi_h[b].pc, dsi_h[b].n); dsi_h[b].n = 0; }
    fputc('\n', stderr); memset(dsi_h, 0, sizeof dsi_h); memset(d_cls, 0, sizeof d_cls); d_same = d_other = d_unmap = 0;
}

#ifndef PT_NO_MAIN                                                 // linked into the stand firmware, which has its own main
int main(int argc, char **argv) {
    int interp = 0, sys = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-'; ai++) { if (!strcmp(argv[ai], "-i")) interp = 1; else if (!strcmp(argv[ai], "-s")) sys = 1; }
    if (ai >= argc) { fprintf(stderr, "usage: %s [-i] [-s] prog.ppc [args]   (-i interpreter, -s bare metal)\n", argv[0]); return 2; }
    CPU *c = pt_hv_init(sys);
    uint8_t *gram = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (gram == MAP_FAILED) { perror("mmap"); return 1; }
    c->mem = gram; guest_premapped = 1;
    if (sys) { sys_prepare(c, argv[ai]); sys_ticker(c); } else guest_prepare(c, argc - ai, argv + ai);
    for (int i = 0; i < c->npreg; i++) pt_hv_map(c->preg[i].host, c->preg[i].base, c->preg[i].size, 0);
    if (interp) { extern int fp_fr_on; fp_fr_on = !getenv("PT_FP_FR") || atoi(getenv("PT_FP_FR"));   /* as jit.c's init */
                  while (!c->halted) interp_step(c); }
    else jit_run(c);
    sys_dump(c);
    if (getenv("PT_STATS")) pt_hv_stats();
    return c->exit_code;
}
#endif
