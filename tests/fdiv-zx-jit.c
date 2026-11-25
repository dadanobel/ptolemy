/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's fdiv and fdivs against interp_step, bit for bit (FPRs and the whole FPSCR), with zero
 * divisors: a finite non-zero dividend over zero is an exact infinity with ZX, and stays in the JIT (for fdivs when
 * the dividend is a single). The JIT may hand over only what it did before: NaN, 0/0, overflow, tiny results, an
 * operand of fdivs that is no single, a guarded FPSCR. */
#include "jit-test.h"
#include <math.h>
enum {
    MEMSZ = 1 << 16,
    VXBITS = 0x01f80700
};
static double value(int sgl) {
    uint32_t r = rnd(), w = rnd();
    float f;
    memcpy(&f, &w, 4);
    switch (r & 7) {
    case 0:
    case 1:
        return r & 8 ? 0.0 : -0.0;
    case 2:
        return (double[]){1.0,       -1.0,     INFINITY, -INFINITY, NAN,
                          0x1p-149,  0x1p127,  3.0,      0x1p-1074, 0x1p1023,
                          0.1,       -7.5}[(r >> 4 & 15) % 12];
    case 3:
        return sgl ? (double)f : (double)f * 1.0000001;
    default:
        return (double)(float)((double)(int32_t)w / 65536.0);
    }
}
int main(void) {
    rng = 0x2545f4914f6cdd1du;
    uint32_t *code =
            mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *m = calloc(1, MEMSZ);
    assert(code != MAP_FAILED && m);
    unsigned zx = 0, fast = 0, handed = 0;
    for (unsigned n = 0; n < 2000000; n++) {
        unsigned d = rnd() & 31, a = rnd() & 31, b = rnd() & 31, sgl = n & 1;
        uint32_t in = (sgl ? 59u : 63u) << 26 | d << 21 | a << 16 | b << 11 | 18u << 1;
        CPU c = {0};
        c.msr = MSR_FP;
        c.pc = 0x1000;
        c.npreg = 1;
        c.preg[0].base = 0;
        c.preg[0].size = MEMSZ;
        c.preg[0].host = m;
        wbe32(m + 0x1000, in);
        for (unsigned r = 0; r < 32; r++)
            c.fpr[r] = value((int)(rnd() & 3));
        uint32_t f = rnd() & ~(0x60000000u | 0xfbu); /* sticky bits, FR, FI, FPRF, NI; RN nearest */
        if (rnd() % 16 == 0)
            f |= 1u << (rnd() % 8 == 2 ? 3 : rnd() % 8); /* an enable or RN in one case of 16 */
        if (rnd() & 1)
            f &= ~(uint32_t)VXBITS;
        if (f & VXBITS)
            f |= 1u << 29;
        if ((f >> 29 & 1 && f & 0x80) || (f >> 28 & 1 && f & 0x40) || (f >> 27 & 1 && f & 0x20) ||
            (f >> 26 & 1 && f & 0x10) || (f >> 25 & 1 && f & 0x08))
            f |= 1u << 30;
        c.fpscr = f;
        CPU expected = c;
        assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        jmsr = MSR_750 | MSR_FP;
        jsys = 0;
        fp_round_fast = 1;
        CPU before = c;
        run1(code, in, &c);
        double x = before.fpr[a], y = before.fpr[b];
        int by_zero = y == 0 && isfinite(x) && x != 0 && !(before.fpscr & 0xfb) && (!sgl || (double)(float)x == x);
        if (c.jit_exit == X_INTERP) {
            assert(!by_zero && !memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
            handed++;
            continue;
        }
        if (memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
            fprintf(stderr, "MISMATCH n=%u in=%08x a=%a b=%a fpscr %08x jit %a/%08x interp %a/%08x\n", n, in, x, y,
                    before.fpscr, c.fpr[d], c.fpscr, expected.fpr[d], expected.fpscr);
            return 1;
        }
        zx += by_zero;
        fast++;
    }
    assert(zx > 100000 && fast > 500000 && handed > 100000);
    printf("fdiv/fdivs in the JIT: %u results, %u of them x / 0 (exact infinity, ZX), bit-exact (FPR + FPSCR) vs "
           "interpreter; %u handed over untouched. PASS\n",
           fast, zx, handed);
    return 0;
}
