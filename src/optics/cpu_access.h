/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPTICS_CPU_ACCESS_H
#define OPTICS_CPU_ACCESS_H
#include "io.h"
typedef struct {
    uint32_t instruction;
    uint8_t rn,rm,rt,bytes,load,simd,sign,destination64;
    uint8_t mode,extend,shift;
    int64_t immediate;
} OpticsA64Memory;
typedef struct {
    void *context;
    /* base=1, reg=31 means SP. Data/index reg31 means ZR, never SP. */
    int (*get_x)(void *,unsigned reg,int base,uint64_t *);
    int (*set_x)(void *,unsigned reg,uint64_t);
    int (*get_q)(void *,unsigned reg,uint8_t bytes[16]);
    int (*set_q)(void *,unsigned reg,const uint8_t bytes[16]);
    /* Atomic-on-failure checked RAM access. Do not treat it as arbitrary MMIO.
     * Resolve the actual native VA/IPA and BOTH pages if crossing a boundary.
     * No second PPC byte swap: these are the bytes of the faulting ARM op. */
    PtR2IoResult (*memory)(void *,uint64_t native_va,uint8_t *,unsigned,int store);
} OpticsCpuMemoryOps;
typedef enum {OPTICS_CPU_DONE,OPTICS_CPU_WAIT,OPTICS_CPU_UNSUPPORTED,OPTICS_CPU_FAILED} OpticsCpuMemoryResult;
/* Non-writeback single LDR/STR: integer and B/H/S/D/Q SIMD, unsigned/unscaled
 * and extended register offsets. Pairs, atomics and pre/post-index fail closed. */
int optics_a64_memory_decode(uint32_t,OpticsA64Memory *);
OpticsCpuMemoryResult optics_a64_memory_step(const OpticsA64Memory *,const OpticsCpuMemoryOps *);
#endif
