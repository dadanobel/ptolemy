/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's fadds/fsubs/fmuls/fdivs with a non-single input (PT_FP_MIXED) against interp_step: FPR result
 * and the whole FPSCR, bit for bit. Single and non-single operands over a wide range, a fixed regression operand
 * (c24bc19587859393), double results on a single midpoint with an error term (must go to the interpreter; counted from
 * the double result itself), just past a midpoint, tiny results (the denormal and lowest normal binades: UX is tiny
 * before rounding) and huge ones, and FPSCR guard bits. A hand-over must leave FPRs and FPSCR untouched.
 * Negative control: the same cases with PT_FP_MIXED off must stay bit-exact too (the JIT then hands them over). */
#include "jit-test.h"
#include <math.h>

static double single(int lo, int span) {
    uint32_t u = (rnd() & 0x80000000) | (uint32_t)(lo + rnd() % span) << 23 | (rnd() & 0x7fffff);
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static double nonsingle(double x) { /* x with random low 29 mantissa bits, never all zero */
    uint64_t u;
    memcpy(&u, &x, 8);
    u ^= (uint64_t)(rnd() | 1) & 0x1fffffff;
    memcpy(&x, &u, 8);
    return x;
}
static int midpoint(unsigned xo5, double a, double b) { /* the double result is a single midpoint and inexact */
    double r = xo5 == 18 ? a / b : xo5 == 25 ? a * b : xo5 == 21 ? a + b : a - b;
    uint64_t u;
    memcpy(&u, &r, 8);
    if ((u & 0x1fffffff) != 0x10000000)
        return 0;
    if (xo5 == 18)
        return fma(-r, b, a) != 0;
    if (xo5 == 25)
        return fma(a, b, -r) != 0;
    double c = xo5 == 21 ? b : -b, bb = r - a; /* TwoSum error */
    return (a - (r - bb)) + (c - bb) != 0;
}

int main(void) {
    rng = 0x2545f4914f6cdd1du;
    uint32_t *code =
            mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *mem = calloc(1, 1 << 16);
    assert(code != MAP_FAILED && mem);
    unsigned fast[2] = {0}, slow[2] = {0}, mid_fast = 0, mids = 0, ops[32] = {0};
    double operand;
    uint64_t operand_bits = 0xc24bc19587859393ull;
    memcpy(&operand, &operand_bits, 8);
    for (unsigned on = 0; on < 2; on++)
        for (unsigned n = 0; n < 1000000; n++) {
            unsigned xo5 = (unsigned[]){20, 21, 25, 18}[n & 3], d = rnd() & 31, a = rnd() & 31, b = rnd() & 31,
                     kind = n >> 2 & 7;
            if (a == b)
                b = (a + 1) & 31;
            uint32_t in = 59u << 26 | d << 21 | a << 16 | (xo5 == 25 ? b << 6 : b << 11) | xo5 << 1;
            int muldiv = xo5 == 18 || xo5 == 25;
            CPU c = {0};
            c.msr = MSR_FP;
            c.pc = 0x1000;
            c.npreg = 1;
            c.preg[0].base = 0;
            c.preg[0].size = 1 << 16;
            c.preg[0].host = mem;
            wbe32(mem + 0x1000, in);
            for (unsigned r = 0; r < 32; r++)
                c.fpr[r] = single(100, 56);
            double sa = single(100, 56);
            int e = ilogb(sa);
            if (muldiv)
                switch (kind) {
                case 0:
                case 1:
                    c.fpr[a] = sa;
                    c.fpr[b] = nonsingle(single(100, 56));
                    break;
                case 2:
                    c.fpr[a] = nonsingle(single(100, 56));
                    c.fpr[b] = nonsingle(single(100, 56));
                    break;
                case 3:
                    c.fpr[a] = (double)(float)((int)(rnd() % 100000) - 50000);
                    c.fpr[b] = operand;
                    break;
                case 4:
                case 5: { /* near a single midpoint m */
                    double m = sa + copysign(ldexp(1.0, e - 24), sa), k = 1.0 + ldexp(1.0, -(int)(30 + rnd() % 20));
                    if (kind == 5)
                        m = nextafter(m, 0);
                    if (xo5 == 25) {
                        c.fpr[a] = m / k;
                        c.fpr[b] = k;
                    } else {
                        c.fpr[a] = m * k;
                        c.fpr[b] = k;
                    }
                    break;
                }
                case 6:
                    if (xo5 == 25) {
                        c.fpr[a] = nonsingle(single(64, 6));
                        c.fpr[b] = single(60, 6);
                    } /* tiny: 2^-126 +- */
                    else {
                        c.fpr[a] = nonsingle(single(1, 10));
                        c.fpr[b] = single(127, 10);
                    }
                    break;
                case 7:
                    if (xo5 == 25) {
                        c.fpr[a] = nonsingle(single(189, 4));
                        c.fpr[b] = single(189, 4);
                    } /* huge: overflow +- */
                    else {
                        c.fpr[a] = nonsingle(single(250, 5));
                        c.fpr[b] = single(119, 10);
                    }
                    break;
                }
            else
                switch (kind) {
                case 0:
                case 1:
                    c.fpr[a] = sa;
                    c.fpr[b] = nonsingle(single(100, 56));
                    break; /* any */
                case 2:
                    c.fpr[a] = nonsingle(single(100, 56));
                    c.fpr[b] = nonsingle(sa);
                    break; /* both non-single */
                case 3:
                    c.fpr[a] = (double)(float)((int)(rnd() % 100000) - 50000);
                    c.fpr[b] = operand;
                    break; /* the regression operand */
                case 4: /* on a midpoint, e != 0 */
                    c.fpr[a] = sa;
                    c.fpr[b] = copysign(ldexp(1.0 + 0x1p-52, e - 24), xo5 == 21 ? sa : -sa);
                    break;
                case 5: /* just past a midpoint */
                    c.fpr[a] = sa;
                    c.fpr[b] = copysign(ldexp(1.0 + 0x1p-28 + 0x1p-52, e - 24), xo5 == 21 ? sa : -sa);
                    break;
                case 6:
                    c.fpr[a] = single(1, 30);
                    c.fpr[b] = nonsingle(single(1, 30));
                    break; /* tiny: denormal results */
                case 7:
                    c.fpr[a] = single(250, 4);
                    c.fpr[b] = nonsingle(single(250, 4));
                    break; /* huge: overflow */
                }
            int mid = midpoint(xo5, c.fpr[a], c.fpr[b]);
            mids += mid;
            c.fpscr = (uint32_t[]){0, 0x82000000, 0x00020000, 0x00001000, 0x02000000}[rnd() % 5] |
                      (rnd() % 16 == 0 ? 1u << (rnd() % 8) : 0);
            CPU expected = c;
            assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
            jmsr = MSR_750 | MSR_FP;
            jsys = 0;
            fp_old = 0;
            fp_mixed = (int)on;
            CPU before = c;
            run1(code, in, &c);
            if (c.jit_exit == X_INTERP) {
                assert(!memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
                slow[on]++;
            } else {
                if (memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
                    fprintf(stderr,
                            "MISMATCH on=%u n=%u in=%08x kind=%u a=%a b=%a fpscr %08x jit %a/%08x interp %a/%08x\n", on,
                            n, in, kind, before.fpr[a], before.fpr[b], before.fpscr, c.fpr[d], c.fpscr, expected.fpr[d],
                            expected.fpscr);
                    return 1;
                }
                fast[on]++;
                ops[xo5] += on;
                mid_fast += mid;
            }
        }
    printf("fadds/fsubs/fmuls/fdivs with a non-single input: PT_FP_MIXED=0 %u JIT + %u interpreter, =1 %u JIT + %u interpreter "
           "(fadds %u, fsubs %u, fmuls %u, fdivs %u), bit-exact (FPR + FPSCR) vs interpreter; midpoint cases %u, in the JIT %u\n",
           fast[0], slow[0], fast[1], slow[1], ops[21], ops[20], ops[25], ops[18], mids, mid_fast);
    int pass = !mid_fast && mids > 100000 &&
               fast[1] > fast[0] + 500000 /* midpoints never stay in the JIT; most mixed cases do */
               && ops[18] > 150000 && ops[25] > 150000;
    puts(pass ? "PASS" : "FAIL");
    return !pass;
}
