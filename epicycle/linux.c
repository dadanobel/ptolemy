// Linux stand: a static ELF32 PowerPC program (Linux ABI, musl) in user mode, or a bare-metal test (-s).
// It is a test harness for the translator core, not the product; Tiger runs in the HVF VM (hvf.c, src/firmware.c).
#define _GNU_SOURCE
#include "ppc.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

static uint32_t brk_cur, brk_base, mmap_top = 0x70000000;
int guest_premapped;                          // guest memory is already fully allocated (VM)

static void map(CPU *c, uint32_t lo, uint32_t hi) {   // reserved [lo,hi) -> RW, 64K aligned (the host page may be 16K/64K)
    lo &= ~0xffffu; hi = (hi + 0xffff) & ~0xffffu;
    if (guest_premapped) return;
    if (mprotect(c->mem + lo, hi - lo, PROT_READ | PROT_WRITE)) { perror("mprotect"); exit(1); }   // mprotect, not mmap: mmap would zero a neighbouring segment
}

static uint32_t load_elf(CPU *c, const char *path, int phys) {     // phys: segments at physical addresses (bare metal)
    int fd = open(path, O_RDONLY); struct stat st;
    if (fd < 0 || fstat(fd, &st)) { perror(path); exit(1); }
    uint8_t *f = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (memcmp(f, "\177ELF\1\2", 6) || be16(f + 18) != 20) { fprintf(stderr, "%s: not a big-endian ELF32 PowerPC file\n", path); exit(1); }
    uint32_t phoff = be32(f + 28), entry = be32(f + 24); int phn = be16(f + 44), phs = be16(f + 42);
    for (int i = 0; i < phn; i++) {
        uint8_t *p = f + phoff + i * phs;
        if (be32(p) != 1) continue;                               // PT_LOAD
        uint32_t off = be32(p + 4), va = be32(p + (phys ? 12 : 8)), fsz = be32(p + 16), msz = be32(p + 20);
        map(c, va, va + msz);
        memcpy(c->mem + va, f + off, fsz);
        if (va + msz > brk_base) brk_base = va + msz;
    }
    brk_base = brk_cur = (brk_base + 0xffff) & ~0xffffu;
    return entry;
}

int sys_call(CPU *c) {
    if (c->sys) { ppc_exception(c, 0xC00, c->pc, 0); return 0; }                    // bare metal: sc is an exception
    uint32_t *r = c->gpr, nr = r[0], res = 0; int err = 0;
    switch (nr) {
    case 1: case 234: c->halted = 1; c->exit_code = r[3]; return 1;                // exit, exit_group
    case 4: res = write(r[3], c->mem + r[4], r[5]); if ((int)res < 0) err = 1; break;
    case 146: {                                                                     // writev
        struct iovec v[16]; int n = r[5] > 16 ? 16 : r[5];
        for (int i = 0; i < n; i++) { v[i].iov_base = c->mem + be32(c->mem + r[4] + 8 * i); v[i].iov_len = be32(c->mem + r[4] + 8 * i + 4); }
        res = writev(r[3], v, n); break; }
    case 45: if (r[3] > brk_cur) { map(c, brk_cur, r[3]); brk_cur = (r[3] + 0xffff) & ~0xffffu; } res = r[3] ? r[3] : brk_cur; break; // brk
    case 90: case 192: { uint32_t len = (r[4] + 0xffff) & ~0xffffu; mmap_top -= len; map(c, mmap_top, mmap_top + len); res = mmap_top; break; } // mmap/mmap2 (anonymous only)
    case 91: res = 0; break;                                                        // munmap: memory is not freed
    case 246: { struct timespec t; clock_gettime(r[3] == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &t);   // clock_gettime (time32)
                wbe32(c->mem + r[4], (uint32_t)t.tv_sec); wbe32(c->mem + r[4] + 4, (uint32_t)t.tv_nsec); break; }
    case 403: { struct timespec t; clock_gettime(r[3] == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &t);   // clock_gettime64
                wbe64(c->mem + r[4], (uint64_t)t.tv_sec); wbe32(c->mem + r[4] + 8, 0); wbe32(c->mem + r[4] + 12, (uint32_t)t.tv_nsec); break; }
    case 232: res = 1000; break;                                                    // set_tid_address
    case 54: case 173: case 174: res = 0; break;                                    // ioctl, rt_sigaction, rt_sigprocmask
    default: fprintf(stderr, "ptolemy: syscall %u not implemented\n", nr); res = 38; err = 1;          // ENOSYS
    }
    r[3] = res;
    cr_set_bit(c, 3, err);                                                          // error = CR0.SO
    return 0;
}

void guest_prepare(CPU *c, int n, char **args) {   // ELF, stack and registers; c->mem is already set up
    for (int f = 0; f < 8; f++) c->cr[f] = cr_make(0, 0, 0, 0);
    c->pc = load_elf(c, args[0], 0);
    c->msr = MSR_PR | MSR_FP; c->spr[287] = 0x80020101;
    c->preg[0] = (PRegion){0, 1ull << 32, c->mem}; c->npreg = 1;
    uint32_t top = 0x7fff0000, sp; map(c, top - 0x100000, top);
    // stack: argc, argv[], NULL, envp NULL, auxv {AT_PAGESZ,4096},{AT_NULL}
    uint32_t strs = top - 0x1000, ptrs[64];
    for (int i = 0; i < n && i < 64; i++) { size_t l = strlen(args[i]) + 1; memcpy(c->mem + strs, args[i], l); ptrs[i] = strs; strs += l; }
    sp = (top - 0x2000) & ~15u; uint32_t p = sp;
    wbe32(c->mem + p, n); p += 4;
    for (int i = 0; i < n; i++, p += 4) wbe32(c->mem + p, ptrs[i]);
    wbe32(c->mem + p, 0); p += 4; wbe32(c->mem + p, 0); p += 4;
    wbe32(c->mem + p, 6); wbe32(c->mem + p + 4, 4096); wbe32(c->mem + p + 8, 0); wbe32(c->mem + p + 12, 0);
    c->gpr[1] = sp;
}

// Bare metal (-s): ELF at physical addresses, 750 reset state; the test results are at
// RES..RES+4K, written to stdout by sys_dump() when the test stops (`b .`).
enum { RES = 0x10000, RESLEN = 0x1000 };
// The stand's test "device": 16 KB (the HVF stage-2 granule) at HOLE behave like memory, but accesses to them go
// through env_mmio. This exercises the device path in the interpreter, in the Linux JIT (SIGSEGV) and in the VM
// (stage-2), while the test journal stays the same as with ordinary RAM at that address.
enum { HOLE = 0x00F00000, HOLESZ = 0x4000 };
static uint8_t hole[HOLESZ];
static void hole_mmio(CPU *c, uint32_t pa, uint8_t *buf, int n, int st) {
    (void)c;
    if (pa - HOLE + n > HOLESZ) { if (!st) memset(buf, 0xff, n); return; }
    if (st) memcpy(hole + (pa - HOLE), buf, n); else memcpy(buf, hole + (pa - HOLE), n);
}
static void *ticker(void *p) { for (CPU *c = p;;) { usleep(1000); c->irq = 1; } return NULL; }   // stand clock: check DEC every millisecond
void sys_ticker(CPU *c) { pthread_t t; pthread_create(&t, 0, ticker, c); pthread_detach(t); }
void sys_prepare(CPU *c, const char *path) {
    map(c, 0, 16 << 20);
    for (int f = 0; f < 8; f++) c->cr[f] = cr_make(0, 0, 0, 0);
    c->pc = load_elf(c, path, 1);
    c->sys = 1; c->msr = MSR_IP; c->spr[287] = 0x00080301;                              // PVR 750 v3.1
    c->preg[0] = (PRegion){0, HOLE, c->mem};
    c->preg[1] = (PRegion){HOLE + HOLESZ, (1ull << 32) - HOLE - HOLESZ, c->mem + HOLE + HOLESZ}; c->npreg = 2;
    c->env_mmio = hole_mmio;
}
void sys_dump(CPU *c) { if (c->sys) fwrite(pmem(c, RES, RESLEN), 1, RESLEN, stdout); }

#ifndef PT_HVF
#include <signal.h>
#include <ucontext.h>
// Bare metal in the stand: RAM is a memfd. With DR=1 JIT code accesses data through an EA window (one each for PR=0
// and PR=1): window pages are mapped onto RAM pages on access faults, like the ARM tables in the HVF VM.
static int ramfd; static uint8_t *win[2]; static CPU *cpu0; static unsigned long n_fill, n_dsi;
static void win_reset(int w) {
    if (mmap(win[w], 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) == MAP_FAILED) { perror("EA window"); exit(1); }
}
static void sys_dmap(CPU *c, int flush) {
    if (flush) { win_reset(0); win_reset(1); }
    c->vbase = c->msr & MSR_DR ? win[!!(c->msr & MSR_PR)] : c->mem;
}
static void mmio_emul(CPU *c, ucontext_t *uc, uint32_t ra) {    // a JIT-code ldr/str to a device address: emulate it and skip it
    uint32_t in = *(uint32_t *)uc->uc_mcontext.pc; int n = 1 << (in >> 30), st = !(in >> 22 & 1), rt = in & 31;
    uint8_t b[8] = {0}; uint64_t v = rt == 31 ? 0 : uc->uc_mcontext.regs[rt];
    for (int i = 0; i < n; i++) b[i] = (uint8_t)(v >> 8 * i);                    // bytes as str would store them
    c->env_mmio(c, ra, b, n, st);
    if (!st && rt != 31) { v = 0; for (int i = 0; i < n; i++) v |= (uint64_t)b[i] << 8 * i; uc->uc_mcontext.regs[rt] = v; }
    uc->uc_mcontext.pc += 4; jit_mmio_n++;
}
static void on_segv(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_; uint8_t *a = si->si_addr; CPU *c = cpu0; (void)sig;
    int w = a >= win[0] && a < win[0] + (1ull << 32) ? 0 : a >= win[1] && a < win[1] + (1ull << 32) ? 1 : -1;
    if (w < 0 && a >= c->mem && a < c->mem + (1ull << 32) && !pmem(c, (uint32_t)(a - c->mem), 1)) { mmio_emul(c, uc, (uint32_t)(a - c->mem)); return; }   // DR=0
    if (w < 0) { signal(SIGSEGV, SIG_DFL); return; }                             // not a window: a real fault
    int n, st; uint32_t ea = jit_mem_ea(*(uint32_t *)uc->uc_mcontext.pc, (const uint64_t *)uc->uc_mcontext.regs, &n, &st);
    uint32_t pg[2] = {ea, (ea + n - 1) & ~0xFFFu};
    for (int k = 0; k < 1 + ((pg[1] ^ ea) >> 12 != 0); k++) {                       // the access may touch two pages
        uint32_t ra, why; int wr; why = pt_xlate(c, pg[k], st, w, &ra, &wr);
        if (!why && !pmem(c, ra, 1)) { mmio_emul(c, uc, ra); return; }                // DR=1, but it is a device
        if (!why) { mmap(win[w] + (pg[k] & ~0xFFFu), 4096, wr ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED | MAP_FIXED, ramfd, ra & ~0xFFFu); n_fill++; continue; }
        if (!jit_restore(c, (const uint64_t *)uc->uc_mcontext.regs, uc->uc_mcontext.pc)) { signal(SIGSEGV, SIG_DFL); return; }
        c->spr[SPR_DAR] = pg[k]; c->spr[SPR_DSISR] = why; ppc_exception(c, 0x300, c->pc, 0);
        c->jit_exit = JIT_FAULT; uc->uc_mcontext.pc = (uint64_t)jit_fault_exit; n_dsi++;   // back to the dispatcher through the common exit
        return;
    }
}
static void sys_mem(CPU *c) {
    ramfd = memfd_create("ppc-ram", 0);
    if (ramfd < 0 || ftruncate(ramfd, 1ll << 32)) { perror("memfd"); exit(1); }
    c->mem = mmap(0, 1ull << 32, PROT_READ | PROT_WRITE, MAP_SHARED, ramfd, 0);
    for (int w = 0; w < 2; w++) win[w] = mmap(0, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (c->mem == MAP_FAILED || win[0] == MAP_FAILED || win[1] == MAP_FAILED) { perror("4G"); exit(1); }
    cpu0 = c; jit_dmap = sys_dmap;
    struct sigaction sa = {0}; sa.sa_sigaction = on_segv; sa.sa_flags = SA_SIGINFO; sigaction(SIGSEGV, &sa, 0);
}

int main(int argc, char **argv) {
    int interp = 0, sys = 0, ai = 1;
    for (; ai < argc && argv[ai][0] == '-'; ai++) { if (!strcmp(argv[ai], "-i")) interp = 1; else if (!strcmp(argv[ai], "-s")) sys = 1; }
    if (ai >= argc) { fprintf(stderr, "usage: %s [-i] [-s] prog.ppc [args]   (-i interpreter, -s bare metal)\n", argv[0]); return 2; }
    static CPU cpu; CPU *c = &cpu;
    // The guest maps one-to-one onto the host's low 4 GiB (base 0) if that is free; otherwise anywhere, with a base.
    void *lo = getenv("PT_NOZB") || sys ? MAP_FAILED : mmap((void *)0x10000, (1ull << 32) - 0x10000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (sys) sys_mem(c);
    else if (lo == (void *)0x10000) c->mem = NULL;
    else { c->mem = mmap(0, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
           if (c->mem == MAP_FAILED) { perror("reserve 4G"); return 1; } }
    if (sys) { sys_prepare(c, argv[ai]); mprotect(c->mem + HOLE, HOLESZ, PROT_NONE); sys_ticker(c); }   // the device goes through the trap
    else guest_prepare(c, argc - ai, argv + ai);
    if (interp) { extern int fp_fr_on; fp_fr_on = !getenv("PT_FP_FR") || atoi(getenv("PT_FP_FR"));   /* as jit.c's init */
                  while (!c->halted) interp_step(c); }
    else jit_run(c);
    sys_dump(c);
    if (getenv("PT_STATS")) fprintf(stderr, "ptolemy: %llu interpreter instructions; JIT with DR=1: mapped pages %lu, DSI %lu; JIT device accesses %lu\n", (unsigned long long)c->icount, n_fill, n_dsi, jit_mmio_n);
    return c->exit_code;
}
#endif
