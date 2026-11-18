/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "bench.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
int ant_ppc_state(void) {
#if defined(__ppc__) || defined(__POWERPC__)
    unsigned old_xer, sum, carry, xer, a = ~0u, b = 1, zero = 0, bad = 0, mode;
    union { double f; uint64_t u; } saved, control, flags, value;
    double one = 1.0, tiny = 0x1p-53;
    __asm__ volatile("mfxer %0" : "=r"(old_xer));
    __asm__ volatile("addc %0,%3,%4\n\tadde %1,%5,%5\n\tmfxer %2"
        : "=&r"(sum), "=&r"(carry), "=&r"(xer) : "r"(a), "r"(b), "r"(zero) : "xer");
    __asm__ volatile("mtxer %0" : : "r"(old_xer) : "xer");
    if (sum != 0 || carry != 1 || (xer & 0x20000000u)) ++bad;
    __asm__ volatile("mffs %0" : "=f"(saved.f));
    for (mode = 0; mode < 4; ++mode) {
        control.u = mode;
        __asm__ volatile("mtfsf 255,%2\n\tfadd %0,%3,%4\n\tmffs %1\n\tmtfsf 255,%5"
            : "=&f"(value.f), "=&f"(flags.f) : "f"(control.f), "f"(one), "f"(tiny), "f"(saved.f));
        if (value.u != (mode == 2 ? UINT64_C(0x3ff0000000000001) : UINT64_C(0x3ff0000000000000)) ||
            ((uint32_t)flags.u & 0x82000000u) != 0x82000000u) ++bad;
        {
            char key[40], val[80]; sprintf(key, "fpscr_round_%u", mode);
            sprintf(val, "%08lx result=%016llx", (unsigned long)(uint32_t)flags.u, (unsigned long long)value.u);
            ant_text(key, val);
        }
    }
    control.u = 0;
    __asm__ volatile("mtfsf 255,%2\n\tfadd %0,%3,%3\n\tmffs %1\n\tmtfsf 255,%4"
        : "=&f"(value.f), "=&f"(flags.f) : "f"(control.f), "f"(one), "f"(saved.f));
    if (value.f != 2.0 || ((uint32_t)flags.u & 0x82000000u)) ++bad;
    ant_number("state_failures", bad); return bad == 0;
#else
    ant_text("reason", "PowerPC architectural validation requires PowerPC execution"); return 0;
#endif
}
