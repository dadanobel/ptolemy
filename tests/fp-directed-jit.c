/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "jit-test.h"
#include <fenv.h>
#include <math.h>
static uint64_t bits(double x) {
    uint64_t u;
    memcpy(&u, &x, 8);
    return u;
}
static double value(unsigned n) {
    const uint64_t edge[] = {0,
                             0x8000000000000000ull,
                             1,
                             0x8000000000000001ull,
                             0x0010000000000000ull,
                             0x3ff0000000000000ull,
                             0xbff0000000000000ull,
                             0x3ff0000010000000ull,
                             0x3ff0000000000001ull,
                             0x7fefffffffffffffull,
                             0xffefffffffffffffull,
                             0x7ff0000000000000ull,
                             0x7ff8000000000001ull,
                             0x7ff0000000000001ull};
    uint64_t u = n % 4 ? ((uint64_t)rnd() << 32 | rnd()) : edge[rnd() % (sizeof edge / sizeof *edge)];
    double d;
    memcpy(&d, &u, 8);
    return d;
}
int main(void) {
    uint32_t *code = mmap(NULL, 32768, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    assert(code != MAP_FAILED);
    uint8_t mem[8192] = {0};
    unsigned fast[4] = {0}, slow = 0, fr = 0;
    fp_directed_enabled = 1;
    nolink = 1;
    jsys = 0;
    fp_hoist = 0;
    fp_lazy_enabled = 0;
    const unsigned ops[] = {18, 20, 21, 25, 28, 29, 30, 31, 12};
    for (unsigned op = 0; op < 9; op++)
        for (unsigned sgl = 0; sgl < 2; sgl++)
            for (unsigned rn = 1; rn < 4; rn++) {
                if (op == 8 && !sgl)
                    continue;
                unsigned xo = ops[op];
                uint32_t in = (sgl && op != 8 ? 59u : 63u) << 26 | 3u << 21 | (op == 8 ? 0u : 1u) << 16 | 2u << 11 |
                              (xo == 25 || xo >= 28 ? 4u << 6 : 0) | xo << 1;
                for (unsigned n = 0; n < 2400; n++) {
                    CPU c = {0};
                    c.msr = MSR_FP;
                    c.pc = 0x1000;
                    c.npreg = 1;
                    c.preg[0].size = sizeof mem;
                    c.preg[0].host = mem;
                    for (unsigned g = 0; g < 32; g++)
                        c.fpr[g] = value(n);
                    if (n % 3 == 0) {
                        c.fpr[1] = 1.0;
                        c.fpr[2] = 0x1p-54;
                        c.fpr[4] = 0x1.0000001p+0;
                    }
                    c.fpscr = rn | (n % 17 == 0 ? 0x8 : 0) | (n % 19 == 0 ? 4 : 0) | (n % 2 ? 0x82000000 : 0);
                    wbe32(mem + 0x1000, in);
                    CPU expected = c;
                    assert(!interp_step(&expected));
                    CPU before = c;
                    run1(code, in, &c);
                    uint64_t fpcr;
                    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
                    assert(fpcr == 0);
                    if (c.jit_exit == X_INTERP) {
                        assert(!memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
                        slow++;
                        continue;
                    }
                    if (memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
                        fprintf(stderr,
                                "directed mismatch op=%u s=%u rn=%u a=%016llx b=%016llx c=%016llx fpscr=%08x result %016llx/%08x expected %016llx/%08x\n",
                                xo, sgl, rn, bits(before.fpr[1]), bits(before.fpr[2]), bits(before.fpr[4]),
                                before.fpscr, bits(c.fpr[3]), c.fpscr, bits(expected.fpr[3]), expected.fpscr);
                        return 1;
                    }
                    assert(c.pc == 0x2004);
                    fast[rn]++;
                    fr += !!(c.fpscr & 0x40000);
                }
            }
    assert(fast[1] > 5000 && fast[2] > 5000 && fast[3] > 5000 && fr > 1000);
    fp_directed_enabled = 0;
    CPU c = {0};
    c.msr = MSR_FP;
    c.fpscr = 1;
    c.fpr[1] = 1;
    c.fpr[2] = 0.1;
    run1(code, 63u << 26 | 3u << 21 | 1u << 16 | 2u << 11 | 21u << 1, &c);
    assert(c.jit_exit == X_INTERP);
    printf("PASS directed: RN1 %u RN2 %u RN3 %u; fallback %u; FR %u; FPCR restored; =0 fallback\n", fast[1], fast[2],
           fast[3], slow, fr);
}
