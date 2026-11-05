/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_BRIDGE_H
#define PT_G5_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
/* Single-CPU bring-up subset. No HT, DART, ECC or power transition is advertised.
 * Register definitions: Linux arch/powerpc/include/asm/uninorth.h; Apple U3.cpp. */
typedef struct {
    uint32_t clock, arbitration;
} PtG5Bridge;
int pt_g5_bridge_access(PtG5Bridge *, uint32_t, uint8_t *, unsigned, int);
#endif
