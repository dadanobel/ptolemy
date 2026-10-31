/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OPTICS_HVF_ACCESS_H
#define OPTICS_HVF_ACCESS_H
#include <Hypervisor/Hypervisor.h>
#include "cpu_access.h"
/* CPU owner, guarded RAM only, BEFORE the env_mmio device route. Fetch instruction
 * only from the validated JIT pool. No reliance on ESR SRT for SIMD faults.
 * DONE advances PC; WAIT changes no PC/registers. FAILED is fatal, not retry.
 * Caller supplies a page-checked VA/IPA resolver and preserves CPU permissions.
 * Do not guess contiguous PA from a fault on a cross-page native access. */
OpticsCpuMemoryResult optics_hvf_guard_step(hv_vcpu_t,uint64_t pc,uint32_t instruction,
    PtR2IoResult (*memory)(void *,uint64_t,uint8_t *,unsigned,int),void *context);
#endif
