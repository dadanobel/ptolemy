/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's fadds/fsubs with a non-single input (PT_FP_MIXED) against interp_step: FPR result and the
 * whole FPSCR, bit for bit. Single plus non-single operands over a wide range, a fixed regression operand
 * (c24bc19587859393), double results on a single midpoint with an error term (must go to the interpreter), just past
 * a midpoint, tiny and huge results, and FPSCR guard bits. A hand-over must leave FPRs and FPSCR untouched.
 * Negative control: the same cases with PT_FP_MIXED off must stay bit-exact too (the JIT then hands them over). */
#include "../epicycle/jit.c"
#include <assert.h>
#include <math.h>
#include <pthread.h>
int sys_call(CPU *c) { (void)c; abort(); }

static uint64_t rng = 0x2545f4914f6cdd1du;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static double single(int lo, int span) {
    uint32_t u = (rnd() & 0x80000000) | (uint32_t)(lo + rnd() % span) << 23 | (rnd() & 0x7fffff);
    float f; memcpy(&f, &u, 4); return f;
}
static double nonsingle(double x) {                         /* x with random low 29 mantissa bits, never all zero */
    uint64_t u; memcpy(&u, &x, 8); u ^= (uint64_t)(rnd() | 1) & 0x1fffffff; memcpy(&x, &u, 8); return x;
}

int main(void)
{
    uint32_t *code = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *mem = calloc(1, 1 << 16);
    assert(code != MAP_FAILED && mem);
    unsigned fast[2] = { 0 }, slow[2] = { 0 }, mid_fast = 0;
    double operand; uint64_t operand_bits = 0xc24bc19587859393ull; memcpy(&operand, &operand_bits, 8);
    for (unsigned on = 0; on < 2; on++) for (unsigned n = 0; n < 1000000; n++) {
        unsigned xo5 = n & 1 ? 21 : 20, d = rnd() & 31, a = rnd() & 31, b = rnd() & 31, kind = n >> 1 & 7;
        if (a == b) b = (a + 1) & 31;
        uint32_t in = 59u << 26 | d << 21 | a << 16 | b << 11 | xo5 << 1;
        CPU c = { 0 }; c.msr = MSR_FP; c.pc = 0x1000; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = 1 << 16; c.preg[0].host = mem;
        wbe32(mem + 0x1000, in);
        for (unsigned r = 0; r < 32; r++) c.fpr[r] = single(100, 56);
        double sa = single(100, 56);
        int e = ilogb(sa), mid = 0;
        switch (kind) {
        case 0: case 1: c.fpr[a] = sa; c.fpr[b] = nonsingle(single(100, 56)); break;            /* any */
        case 2: c.fpr[a] = nonsingle(single(100, 56)); c.fpr[b] = nonsingle(sa); break;         /* both non-single */
        case 3: c.fpr[a] = (double)(float)((int)(rnd() % 100000) - 50000); c.fpr[b] = operand; break;   /* the regression operand */
        case 4:                                                                                /* on a midpoint, e != 0 */
            c.fpr[a] = sa; c.fpr[b] = copysign(ldexp(1.0 + 0x1p-52, e - 24), xo5 == 21 ? sa : -sa); mid = 1; break;
        case 5:                                                                                /* just past a midpoint */
            c.fpr[a] = sa; c.fpr[b] = copysign(ldexp(1.0 + 0x1p-28 + 0x1p-52, e - 24), xo5 == 21 ? sa : -sa); break;
        case 6: c.fpr[a] = single(1, 30); c.fpr[b] = nonsingle(single(1, 30)); break;           /* tiny: denormal results */
        case 7: c.fpr[a] = single(250, 4); c.fpr[b] = nonsingle(single(250, 4)); break;         /* huge: overflow */
        }
        c.fpscr = (uint32_t[]){ 0, 0x82000000, 0x00020000, 0x00001000, 0x02000000 }[rnd() % 5] | (rnd() % 16 == 0 ? 1u << (rnd() % 8) : 0);
        CPU expected = c; assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        jmsr = MSR_750 | MSR_FP; jsys = 0; fp_old = 0; fp_mixed = (int)on;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code; E(0xa9bf7bfb); E(0xaa0003fb);
        exit_common = code + 2048;
        assert(tr(in, 0x2000) == 0);
        b_to(exit_common);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        CPU before = c;
        ((void (*)(CPU *))(void *)code)(&c);
        if (c.jit_exit == X_INTERP) {
            assert(!memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
            slow[on]++;
        } else {
            if (memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
                fprintf(stderr, "MISMATCH on=%u n=%u in=%08x kind=%u a=%a b=%a fpscr %08x jit %a/%08x interp %a/%08x\n", on, n, in, kind,
                        before.fpr[a], before.fpr[b], before.fpscr, c.fpr[d], c.fpscr, expected.fpr[d], expected.fpscr);
                return 1;
            }
            fast[on]++;
            mid_fast += mid;
        }
    }
    printf("fadds/fsubs with a non-single input: PT_FP_MIXED=0 %u JIT + %u interpreter, =1 %u JIT + %u interpreter, "
           "bit-exact (FPR + FPSCR) vs interpreter; midpoint cases in the JIT %u\n", fast[0], slow[0], fast[1], slow[1], mid_fast);
    int pass = !mid_fast && fast[1] > fast[0] + 300000;   /* midpoints never stay in the JIT; most mixed cases do */
    puts(pass ? "PASS" : "FAIL");
    return !pass;
}
