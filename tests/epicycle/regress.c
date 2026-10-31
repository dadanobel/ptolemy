// Semantic regression: each case expects the Book I / PEM result; unverified expectations are marked as hypotheses in
// their labels (the printed labels are Russian). Exit 0: all checks matched.
//   cc -O1 -I epicycle -o regress tests/epicycle/regress.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c -lm      (on Linux/ARM64 also the JIT checks)
#include "ppc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__) && defined(__aarch64__)
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "jit.c"
#endif
int sys_call(CPU *c) { (void)c; return 0; }
static int bad;
static void expect(const char *what, uint64_t got, uint64_t want) {
    bad += got != want;
    printf("%s %-58s %#llx%s%#llx\n", got == want ? "ok    " : "FAILED", what, (unsigned long long)got,
           got == want ? " = " : " ≠ ", (unsigned long long)want);
}
static CPU c;
static uint64_t B(double x) { uint64_t b; memcpy(&b, &x, 8); return b; }
static double Dd(uint64_t b) { double x; memcpy(&x, &b, 8); return x; }
static void run(uint32_t in) { c.pc = 0; wbe32(c.mem, in); interp_step(&c); }
static void fp(uint32_t fpscr, double a, double b, double x) { c.fpscr = fpscr; c.fpr[1] = a; c.fpr[2] = b; c.fpr[3] = x; c.fpr[4] = 7.0; }
#define FA(op, t, a, b, x, xo) ((uint32_t)(op) << 26 | (t) << 21 | (a) << 16 | (b) << 11 | (x) << 6 | (xo) << 1)   // FRT ← FRA·FRC ± FRB
#define FX(t, a, b, xo) (63u << 26 | (t) << 21 | (a) << 16 | (b) << 11 | (xo) << 1)
enum { FXB = 1u << 31, FEX = 1u << 30, ZX = 1u << 26, VXSNAN = 1u << 24, VXISI = 1u << 23, VXIMZ = 1u << 20,
       FI = 1u << 17, VXCVI = 1u << 8, VE = 1u << 7, ZE = 1u << 4 };
#define SNAN Dd(0x7FF0000000000001ull)
#define QNAN Dd(0x7FF8000000000123ull)

int main(void) {
    c.mem = calloc(1, 4096); c.preg[0] = (PRegion){0, 4096, c.mem}; c.npreg = 1;
    c.gpr[2] = 0x40000000; run(7u << 26 | 1u << 21 | 2u << 16 | 4);
    expect("mulli 0x40000000*4 (no UB in C)", c.gpr[1], 0);
    c.gpr[2] = 0x80000000; run(31u << 26 | 1u << 21 | 2u << 16 | 1u << 10 | 104u << 1);
    expect("nego 0x80000000: OV", (uint64_t)c.ov << 32 | c.gpr[1], 1ull << 32 | 0x80000000u);

    fp(1, 0, 1.9, 0); run(FX(4, 0, 2, 14));                    expect("fctiw RN=1 (toward zero), 1.9", (uint32_t)B(c.fpr[4]), 1);
    fp(0, 0, INFINITY, 0); run(FX(4, 0, 2, 14));               expect("fctiw +inf → 0x7FFFFFFF", (uint32_t)B(c.fpr[4]), 0x7fffffff);
                                                               expect("  … VXCVI", !!(c.fpscr & VXCVI), 1);
    fp(0, 0, QNAN, 0); run(FX(4, 0, 2, 14));                   expect("fctiw NaN → 0x80000000", (uint32_t)B(c.fpr[4]), 0x80000000u);
    fp(0, 1.0, 0.0, 0); run(FA(63, 4, 1, 2, 0, 18));           expect("fdiv 1/0: FPSCR = FX|ZX|FPRF(+inf)", c.fpscr, 0x84005000);
    fp(ZE, 1.0, 0.0, 0); run(FA(63, 4, 1, 2, 0, 18));          expect("fdiv 1/0 with ZE=1: FRT unchanged", B(c.fpr[4]), B(7.0));
                                                               expect("  … FX|FEX|ZX", c.fpscr & (FXB | FEX | ZX), FXB | FEX | ZX);
    fp(VE, INFINITY, INFINITY, 0); run(FA(63, 4, 1, 2, 0, 20)); expect("fsub inf-inf with VE=1: FRT unchanged", B(c.fpr[4]), B(7.0));
    fp(0, 0, 0.0, 0); run(FA(59, 4, 0, 2, 0, 24));             expect("fres(+0): ZX", !!(c.fpscr & ZX), 1);
    fp(3, 0.0, 0.0, 2.0); run(FA(59, 4, 1, 2, 3, 28));         expect("fmsubs 0*2-0 with RN=-inf -> -0", B(c.fpr[4]), 0x8000000000000000ull);
    fp(0, 1e10, 0x1p-100, 3.0); run(FA(59, 4, 1, 2, 3, 28));   expect("fmsubs 1e10*3-2^-100: one rounding (not two)", B(c.fpr[4]), 0x421BF08EA0000000ull);
    fp(2, 1.0, -0x1p-60, 1 + 0x1p-52); run(FA(63, 4, 1, 2, 3, 30));
    expect("fnmsub RN=+inf: rounding first, then the sign", B(c.fpr[4]), 0xBFF0000000000002ull);
    fp(0, 0x1p-130, 0, 0); run(FX(4, 0, 1, 12));               expect("frsp 2^-130: FPRF = +denormalized (single)", c.fpscr >> 12 & 31, 0x14);
    fp(0, 1.0, 0x1p-60, 0); run(FA(63, 4, 1, 2, 0, 21)); run(63u << 26 | 1u << 16 | 2u << 11);
    expect("fcmpu leaves FI alone", !!(c.fpscr & FI), 1);
    fp(0, SNAN, 1.0, 0); run(FA(63, 4, 1, 2, 0, 21)); run(FX(0, 0, 0, 70)); run(FA(63, 4, 1, 2, 0, 21));
    expect("FX only on a 0->1 transition (VXSNAN already 1)", !!(c.fpscr & FXB), 0);
    fp(0, 0.0, INFINITY, INFINITY); run(FA(63, 4, 1, 2, 3, 28)); expect("fmsub 0*inf-inf: VXIMZ without VXISI (unverified)", c.fpscr & (VXIMZ | VXISI), VXIMZ);
    fp(0, 0.0, QNAN, INFINITY); run(FA(63, 4, 1, 2, 3, 29));     expect("fmadd 0·∞+QNaN → FRB", B(c.fpr[4]), B(QNAN));
                                                                 expect("  ... without VXIMZ (unverified)", c.fpscr & VXIMZ, 0);
#if defined(__linux__) && defined(__aarch64__)
    CPU a = {0}, b = {0};
    b.fpr[1] = 123; b.fpscr = 0x84000000; b.ov = b.so = 1;
    expect("PT_CHECK sees an FPR/FPSCR/OV/SO difference", diff(&a, &b, 0, 0), 1);
    uint64_t x[32] = {0}; int n, st; x[6] = 0x20000000; x[7] = 0x12345678; x[28] = 0x7f0000000000ull;   // decoding JIT-code accesses
    uint32_t e1 = jit_mem_ea(0xB9400000 | 2u << 10 | 6u << 5 | 5, x, &n, &st);                      // ldr w5, [x6, #8]
    expect("jit_mem_ea: ldr [x6,#8]", (uint64_t)e1 << 8 | n << 4 | st, 0x2000000840);
    e1 = jit_mem_ea(0x78000000 | (0x1ffu & -2) << 12 | 6u << 5 | 5, x, &n, &st);                       // sturh w5, [x6, #-2]
    expect("jit_mem_ea: sturh [x6,#-2]", (uint64_t)e1 << 8 | n << 4 | st, 0x1ffffffe21);
    e1 = jit_mem_ea(0x38604800 | 7u << 16 | 28u << 5 | 5, x, &n, &st);                               // ldrb w5, [x28, w7, uxtw]
    expect("jit_mem_ea: ldrb [x28,w7,uxtw]", (uint64_t)e1 << 8 | n << 4 | st, 0x1234567810);
    fflush(stdout);
    pid_t pid = fork();
    if (!pid) {                                                // translation right at the edge of a mapped page
        size_t p = getpagesize();
        a.mem = mmap(0, 2 * p, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        mprotect(a.mem + p, p, PROT_NONE); a.preg[0] = (PRegion){0, 2 * p, a.mem}; a.npreg = 1;
        a.pc = p - 4; wbe32(a.mem + a.pc, 18u << 26 | ((0u - a.pc) & 0x03fffffc));             // b 0
        jit_vm_buf = calloc(1, 64 << 20); jit_vm_size = 64 << 20;
        jit_vm_jc = calloc(1 << 16, sizeof(JC)); jit_vm_jc_va = (uint64_t)jit_vm_jc;
        init(&a); translate(&a, a.pc, a.pc);
        a.pc = p - 8; wbe32(a.mem + a.pc, 0x2c030000); wbe32(a.mem + a.pc + 4, 0x4182fff8);  // cmpwi; beq: the fusion must not read past the edge
        translate(&a, a.pc, a.pc);
        _exit(0);
    }
    waitpid(pid, &st, 0);
    expect("JIT: translation at a page end without reading past it", WIFEXITED(st) && !WEXITSTATUS(st), 1);
#endif
    printf(bad ? "FAILURES: %d\n" : "all checks passed\n", bad);
    return bad != 0;
}
