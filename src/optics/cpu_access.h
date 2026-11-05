/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPTICS_CPU_ACCESS_H
#define OPTICS_CPU_ACCESS_H
#include "io.h"
typedef struct {
    uint32_t instruction;
    uint8_t rn, rm, rt, bytes, load, simd, sign, destination64;
    uint8_t mode, extend, shift;
    int64_t immediate;
} OpticsA64Memory;
/* Non-writeback single LDR/STR: integer and B/H/S/D/Q SIMD, unsigned/unscaled
 * and extended register offsets. Pairs, atomics and pre/post-index fail closed. */
int optics_a64_memory_decode(uint32_t, OpticsA64Memory *);
#endif
