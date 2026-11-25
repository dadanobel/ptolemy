/* SPDX-License-Identifier: GPL-2.0-or-later
 * PT_STATS' signs of an interpreter entry (why_before in jit.c): what the host says about an instruction the JIT did
 * not run, from its operands, the FPSCR and the MSR. */
#include "jit-test.h"
#include <math.h>
static uint32_t signs(uint32_t w, double a, double b, double c3, uint32_t fpscr, uint32_t msr, int untranslated) {
    CPU c = {0};
    c.msr = msr;
    c.fpscr = fpscr;
    c.fpr[1] = a;
    c.fpr[2] = b;
    c.fpr[3] = c3;
    return why_before(&c, w, untranslated);
}
int main(void) {
    enum {
        FDIVS = 59u << 26 | 4u << 21 | 1u << 16 | 2u << 11 | 18u << 1,
        FMULS = 59u << 26 | 4u << 21 | 1u << 16 | 3u << 6 | 25u << 1, /* A and C */
        FCTIWZ = 63u << 26 | 4u << 21 | 2u << 11 | 15u << 1,
        FADD = 63u << 26 | 4u << 21 | 1u << 16 | 2u << 11 | 21u << 1,
        TLBIE = 31u << 26 | 306u << 1,
        VADDFP = 4u << 26 | 10
    };
    assert(signs(FDIVS, 1.0, 0.0, NAN, 0, MSR_FP, 0) == WHY_ZERO); /* the C field is not an operand */
    assert(signs(FDIVS, 0.1, 3.0, 0, 0, MSR_FP, 0) == WHY_NOT_SINGLE);
    assert(signs(FMULS, 2.0, NAN, 0x1p-1060, 0, MSR_FP, 0) == WHY_DENORMAL); /* B is not an operand of fmuls */
    assert(signs(FCTIWZ, 0, 0x1p31, 0, 0, MSR_FP, 0) == WHY_RANGE);
    assert(signs(FCTIWZ, 0, -0x1p31, 0, 0, MSR_FP, 0) == 0 && signs(FCTIWZ, 0, 2147483647.5, 0, 0, MSR_FP, 0) == 0);
    assert(signs(FCTIWZ, 0, NAN, 0, 0, MSR_FP, 0) == WHY_NAN);
    assert(signs(FADD, INFINITY, 1.0, 0, 0x82, MSR_FP, 0) == (WHY_INF | WHY_ENABLE | WHY_RN));
    assert(signs(FADD, 0.1, 1.0, 0, 0, MSR_FP, 0) == 0); /* a double operation: any double is its own format */
    assert(signs(FADD, NAN, 1.0, 0, 0, 0, 0) == WHY_OFF); /* MSR.FP = 0: nothing else matters */
    assert(signs(TLBIE, NAN, NAN, NAN, 0xff, MSR_LE, 1) == (WHY_NOJIT | WHY_LE));
    assert(signs(VADDFP, 0, 0, 0, 0, MSR_FP, 0) == WHY_OFF && signs(VADDFP, 0, 0, 0, 0, MSR_VEC, 0) == 0);
    for (unsigned i = 0; i < 2000; i++) /* the table: one cell per root and set of signs, full without a crash */
        why_count(i % 1500, i % 7);
    uint64_t total = 0;
    for (unsigned h = 0; h < 1024; h++)
        total += st_why[h].n;
    assert(total > 1000 && total <= 2000);
    puts("PT_STATS signs of an interpreter entry: operands by form, FPSCR control, MSR. PASS");
    return 0;
}
