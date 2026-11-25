/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's fctiw and fctiwz (PT_FP_ROUND_FAST) against interp_step, bit for bit: FPRs and the whole
 * FPSCR, in every rounding mode. With the exception enables zero the JIT never hands over: a NaN and a value outside
 * the 32-bit range are saturated in the JIT (VXCVI, VXSNAN, FX, VX). With an enable set it hands over untouched. */
#include "jit-test.h"
#include <math.h>
enum {
    MEMSZ = 1 << 16,
    VXBITS = 0x01f80700, /* every VX* cause */
    T_VX = 1u << 29,
    T_FEX = 1u << 30
};
static uint64_t rnd64(void) {
    return (uint64_t)rnd() << 32 | rnd();
}
static double value(void) {
    uint32_t r = rnd();
    uint64_t u;
    double x;
    switch (r & 7) {
    case 0: /* around the limits, and the specials */
        return (double[]){0x1p31,        0x1p31 - 0.5, 0x1p31 - 0.25, 0x1p31 - 1,   0x1p31 + 1,   -0x1p31,
                          -0x1p31 - 0.25, -0x1p31 - 0.5, -0x1p31 - 1,  -0x1p31 + 0.5, 0x1p32,       0x1.8p32,
                          INFINITY,      -INFINITY,    0.0,           -0.0,         0.5,          -0.5,
                          1.5,           2.5,          -2.5,          0x1p-1074,    0x1p63,       -0x1p63,
                          0x1p1023,      2147483646.5, 2147483647.5,  -2147483648.5, -2147483647.5, 4294967295.0,
                          1e10,          -1e10}[r >> 3 & 31];
    case 1:
        u = rnd64();
        break; /* any bits */
    case 2:
        u = 0x7ff0000000000000ull | (rnd64() & 0x800fffffffffffffull) | 1; /* NaNs: signalling and quiet */
        if (r & 8)
            u |= 1ull << 51;
        break;
    case 3: /* 2^31 .. 2^34: what UT2004 converts */
        x = ldexp((double)rnd() / 4294967296.0 + 1.0, 31 + (int)(r >> 3 & 3));
        return r & 64 ? -x : x;
    default: /* in range, with a fraction */
        x = (double)(int32_t)rnd() / (double)(1u << (r >> 3 & 31));
        return x;
    }
    memcpy(&x, &u, 8);
    return x;
}

int main(void) {
    rng = 0x2545f4914f6cdd1du;
    uint32_t *code =
            mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *m = calloc(1, MEMSZ);
    assert(code != MAP_FAILED && m);
    unsigned fast = 0, sat = 0, nan = 0, handed = 0;
    for (unsigned n = 0; n < 2000000; n++) {
        unsigned d = rnd() & 31, b = rnd() & 31, xo = n & 1 ? 15 : 14;
        uint32_t in = 63u << 26 | d << 21 | b << 11 | xo << 1;
        CPU c = {0};
        c.msr = MSR_FP;
        c.pc = 0x1000;
        c.npreg = 1;
        c.preg[0].base = 0;
        c.preg[0].size = MEMSZ;
        c.preg[0].host = m;
        wbe32(m + 0x1000, in);
        for (unsigned r = 0; r < 32; r++)
            c.fpr[r] = value();
        /* any FPSCR the machine can hold: sticky bits, FR, FI, FPRF, NI, RN; an enable in one case of 16; VX and FEX
         * are summaries */
        uint32_t f = rnd() & ~(T_VX | T_FEX | 0xf8u);
        if (rnd() % 16 == 0)
            f |= 1u << (3 + rnd() % 5);
        if (rnd() & 1)
            f &= ~(uint32_t)VXBITS;
        if (f & VXBITS)
            f |= T_VX;
        if ((f & T_VX && f & 0x80) || (f >> 28 & 1 && f & 0x40) || (f >> 27 & 1 && f & 0x20) ||
            (f >> 26 & 1 && f & 0x10) || (f >> 25 & 1 && f & 0x08))
            f |= T_FEX;
        c.fpscr = f;
        CPU expected = c;
        assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        jmsr = MSR_750 | MSR_FP;
        jsys = 0;
        fp_round_fast = 1;
        CPU before = c;
        run1(code, in, &c);
        double x = before.fpr[b];
        if (c.jit_exit == X_INTERP) {
            assert((before.fpscr & 0xf8) && !memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
            handed++;
            continue;
        }
        if ((before.fpscr & 0xf8) || memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
            uint64_t xb, jb, ib;
            memcpy(&xb, &x, 8);
            memcpy(&jb, &c.fpr[d], 8);
            memcpy(&ib, &expected.fpr[d], 8);
            fprintf(stderr, "MISMATCH n=%u in=%08x b=%016llx (%a) fpscr %08x jit %016llx/%08x interp %016llx/%08x\n",
                    n, in, (unsigned long long)xb, x, before.fpscr, (unsigned long long)jb, c.fpscr,
                    (unsigned long long)ib, expected.fpscr);
            return 1;
        }
        if (isnan(x))
            nan++;
        else if (expected.fpscr & 0x100 && !(before.fpscr & 0x100))
            sat++;
        else
            fast++;
    }
    assert(fast > 500000 && sat > 100000 && nan > 100000 && handed > 50000);
    printf("fctiw/fctiwz in the JIT: %u conversions, %u saturated (VXCVI new), %u NaNs, bit-exact (FPR + FPSCR) vs "
           "interpreter; %u handed over with an enable set, untouched. PASS\n",
           fast, sat, nan, handed);
    return 0;
}
