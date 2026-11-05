/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Test real JIT init, not a replacement parser. No generated instruction is executed. */
/* Run by tools/build-perf.sh --run-unit; built with -Iepicycle. */
#include "jit-test.h"
#include "../include/pt_cpu_profile.h"
int main(void) {
    uint32_t *code = calloc(1, 64u << 20);
    JC *cache = calloc(1, sizeof(JC) * (1u << 16));
    assert(code && cache);
    CPU c = {0};
    c.mem = calloc(1, 65536);
    assert(c.mem);
    c.msr = MSR_FP;
    jit_vm_buf = code;
    jit_vm_size = 64u << 20;
    jit_vm_jc = cache;
    unsetenv("PT_CHECK");
    unsetenv("PT_MAXBLK");
    unsetenv("PT_INTERP_BATCH");
    const char *names[] = {"PT_NOLINK", "PT_NOJC", "PT_FP_OLD", "PT_FMA_FAST"};
    const char *values[] = {NULL, "0", "1"};
    for (unsigned k = 0; k < 3; k++) {
        for (unsigned n = 0; n < 4; n++) {
            if (values[k])
                setenv(names[n], values[k], 1);
            else
                unsetenv(names[n]);
        }
        init(&c);
        int expected = k == 2;
        assert(!!nolink == expected && !!nojc == expected && !!fp_old == expected && !!fma_fast == expected);
    }
    const char *fast[] = {"PT_FMA_FAST",  "PT_FP_ROUND_FAST", "PT_FP_HOIST", "PT_FP_LAZY",   "PT_FP_MORE",
                          "PT_FPR_CACHE", "PT_VMX_NEON",      "PT_VMX_INT2", "PT_VMX_MHADD", "PT_VMX_FP2"};
    const uint32_t pvr[] = {0x00080301, 0x000c0209, 0x80030102};
    unsetenv("PT_FP_OLD");
    unsetenv("PT_NOLINK");
    unsetenv("PT_NOJC");
    for (unsigned model = 0; model < 3; model++)
        for (unsigned mode = 0; mode < 5; mode++) {
            for (unsigned n = 0; n < 10; n++)
                unsetenv(fast[n]);
            unsetenv("PT_CPU_FAST");
            if (mode == 1)
                setenv("PT_CPU_FAST", "0", 1);
            if (mode == 3)
                setenv("PT_CPU_FAST", "1", 1);
            if (mode == 4)
                setenv("PT_FPR_CACHE", "0", 1);
            c.spr[287] = pvr[model];
            pt_cpu_fast_defaults(c.spr[287], mode != 2 && mode != 3);
            int enabled = mode == 0 || mode == 3 || mode == 4;
            init(&c);
            assert(fma_fast == enabled && fp_hoist_enabled == enabled && fp_lazy_enabled == enabled);
            assert(fp_round_fast == enabled && fp_more_enabled == enabled && fcache_enabled == (enabled && mode != 4));
            assert(vmx_neon_enabled == (enabled && model != 0));
            assert(!!(jmsr & MSR_VEC) == (model != 0));
            if (!model)
                for (unsigned n = 6; n < 10; n++)
                    assert(!getenv(fast[n]));
        }
    puts("CPU config: unset/0/1 through real init PASS");
    free(c.mem);
    free(cache);
    free(code);
    return 0;
}
