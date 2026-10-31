/* SPDX-License-Identifier: GPL-2.0-or-later */
/* PT_CHECK's comparator must reject deliberate corruption of MSR, VSCR and VR before it can validate code.
 * Build: tools/build-perf.sh. */
#include "../epicycle/jit.c"
int sys_call(CPU *c) { (void)c; abort(); }

int main(void)
{
    static const uint32_t msr_bits[] = {MSR_IR, MSR_DR, MSR_PR, MSR_EE};
    CPU a = {0}, b = {0};
    unsigned failures = diff(&a, &b, 0, 0) != 0, cases = 1;
    for (unsigned k = 0; k < 8; k++) {
        memset(&a, 0, sizeof a);
        if (k < 4) a.msr = msr_bits[k];
        else if (k < 6) a.vscr = k == 4 ? 1u : 0x10000u;
        else a.vr[k == 6 ? 0 : 31][k == 6 ? 0 : 15] = 1;
        if (!diff(&a, &b, 0, 0)) failures++;
        cases++;
    }
    printf("JIT comparator: %u cases, %u failures (mismatch diagnostics expected)\n", cases, failures);
    return failures ? 1 : 0;
}
