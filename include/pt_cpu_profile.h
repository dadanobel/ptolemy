/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* CPU selection is architectural; it must not select an optimization level. */
static inline void pt_cpu_fast_defaults(uint32_t pvr, int window) {
    const char *mode = getenv("PT_CPU_FAST");
    if (mode ? strcmp(mode, "1") != 0 : !window)
        return;
    static const char *const fp[] = {"PT_FMA_FAST", "PT_FP_ROUND_FAST", "PT_FP_HOIST",
                                     "PT_FP_LAZY",  "PT_FP_MORE",       "PT_FPR_CACHE"};
    static const char *const vmx[] = {"PT_VMX_NEON", "PT_VMX_INT2", "PT_VMX_MHADD", "PT_VMX_FP2"};
    for (unsigned i = 0; i < sizeof fp / sizeof *fp; i++)
        setenv(fp[i], "1", 0);
    if (pvr >> 16 == 0x000c || pvr >> 16 == 0x8003)
        for (unsigned i = 0; i < sizeof vmx / sizeof *vmx; i++)
            setenv(vmx[i], "1", 0);
}
