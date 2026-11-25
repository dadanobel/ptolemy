/* SPDX-License-Identifier: GPL-2.0-or-later
 * fmuls, fdivs and the fmadds family with results at the bottom of the single range (under 2^-95, denormals, zeros
 * from nonzero operands, the boundary 2^-126 from both sides) and tiny dividends: the JIT keeps them (fp_tiny_status)
 * and must agree with interp_step on the FPR and the whole FPSCR (FI, FR, UX, FX, XX, FPRF), bit for bit. The double
 * forms of the same values still go to the interpreter and must leave everything untouched. */
#include "jit-test.h"
#include <math.h>

static double sf(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static uint32_t tiny_bits(void) { /* singles whose products and quotients land low */
    uint32_t r = rnd(), sign = r & 0x80000000;
    switch (r >> 1 & 7) {
    case 0:
        return sign | (uint32_t)(1 + (r >> 8) % 40) << 23 | (rnd() & 0x7fffff); /* 2^-126 .. 2^-87 */
    case 1:
        return sign | (rnd() & 0x7fffff); /* denormals (and zero) */
    case 2:
        return sign | (uint32_t)(60 + (r >> 8) % 40) << 23 | (rnd() & 0x7fffff); /* 2^-67 .. 2^-28 */
    case 3:
        return sign | (uint32_t)(127 + (r >> 8) % 8) << 23 | (rnd() & 0x7fff00); /* 1 .. 256, short mantissas */
    case 4:
        return sign | (r & 0x100 ? 0x00800000 : 0x7f800000); /* 2^-126; an infinity (x / Inf = 0 exactly) */
    default:
        return sign | (uint32_t)(20 + (r >> 8) % 60) << 23 | (rnd() & 0x7fffff);
    }
}
int main(void) {
    rng = 0x6a09e667f3bcc909u;
    uint32_t *code =
            mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *mem = calloc(1, 1 << 16);
    assert(code != MAP_FAILED && mem);
    static const unsigned XO[6] = {25, 18, 28, 29, 30, 31};
    unsigned jit = 0, interp = 0, tiny = 0, n;
    for (unsigned direct = 0; direct < 2; direct++)
        for (n = 0; n < 300000; n++) {
            unsigned xo5 = XO[n % 6], dbl = n % 13 == 0, d = rnd() & 31, a = rnd() & 31, b = rnd() & 31, k = rnd() & 31;
            if (a == k || b == a || b == k)
                a = (k + 1) & 31, b = (k + 2) & 31;
            uint32_t in = (dbl ? 63u : 59u) << 26 | d << 21 | a << 16 | b << 11 | k << 6 | xo5 << 1;
            CPU c = {0};
            c.msr = MSR_FP;
            c.pc = 0x1000;
            c.npreg = 1;
            c.preg[0].base = 0;
            c.preg[0].size = 1 << 16;
            c.preg[0].host = mem;
            wbe32(mem + 0x1000, in);
            for (unsigned r = 0; r < 32; r++)
                c.fpr[r] = sf(tiny_bits());
            if (n % 7 == 3) { /* exact results around 2^-126: a product of two powers of two and a nudge */
                c.fpr[a] = ldexp(1.0 + (rnd() % 3) * 0x1p-23, -60 - (int)(rnd() % 8));
                c.fpr[k] = ldexp(1.0 - (rnd() % 3) * 0x1p-24, -126 + 60 + (int)(rnd() % 8) - (int)(rnd() % 2));
                c.fpr[b] = (rnd() & 1 ? 1 : -1) * sf(rnd() % 4);
            }
            c.fpscr = (uint32_t[]){0, 0x82000000, 0x88000000, 0x00020000, 0x00004000, 0x08000000}[rnd() % 6];
            CPU expected = c;
            assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
            jmsr = MSR_750 | MSR_FP;
            jsys = 0;
            fp_old = 0;
            fma_fast = 1;
            fma_single_direct = (int)direct;
            CPU before = c;
            run1(code, in, &c);
            double r = expected.fpr[d];
            int low = isfinite(r) && fabs(r) < 0x1p-95 && (r != 0 || expected.fpscr & 0x02000000);
            if (c.jit_exit == X_INTERP) {
                assert(!memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
                interp++;
                if (low && !dbl && !isnan(before.fpr[a]) && !(expected.fpscr & 0xA0000000 & ~before.fpscr & 0x20000000)) {
                    fprintf(stderr, "fp-tiny: a single tiny case left the JIT: in=%08x a=%a b=%a c=%a fpscr %08x -> %a/%08x\n",
                            in, before.fpr[a], before.fpr[b], before.fpr[k], before.fpscr, r, expected.fpscr);
                    return 1;
                }
            } else {
#if defined(__x86_64__) /* the container's unicorn run: x86's fenv sees tininess after rounding (PPC and ARM: before), so an
                         * exact value just under 2^-126 that rounds to it raises no UX in the interpreter there */
                if (fabs(expected.fpr[d]) == 0x1p-126 && !((c.fpscr ^ expected.fpscr) & ~0x88000000u) &&
                    (c.fpscr ^ expected.fpscr) & c.fpscr & 0x08000000u)
                    expected.fpscr = c.fpscr;
#endif
                if (memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
                    fprintf(stderr,
                            "fp-tiny MISMATCH direct=%u n=%u in=%08x a=%a b=%a c=%a fpscr %08x jit %a/%08x interp %a/%08x\n",
                            direct, n, in, before.fpr[a], before.fpr[b], before.fpr[k], before.fpscr, c.fpr[d], c.fpscr,
                            expected.fpr[d], expected.fpscr);
                    return 1;
                }
                jit++;
                tiny += low;
            }
        }
    printf("fp-tiny: %u in the JIT (%u with a result under 2^-95 or flushed to zero), %u handed to the interpreter, "
           "bit-exact (FPR + FPSCR) vs interpreter PASS\n",
           jit, tiny, interp);
    return 0;
}
