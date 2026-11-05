/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_GUARD_H
#define PT_R2_GUARD_H
#include "io.h"
typedef struct PtR2GuardMap PtR2GuardMap;
/* One call protects all actual HVF/interpreter aliases of this physical group.
 * Query the VM's real IPA granule; do not assume guest pages == host pages. */
typedef int (*PtR2GuardApply)(void *, int ram, uint64_t physical_group, size_t granule, PtR2IoGuard);
PtR2GuardMap *pt_r2_guard_create(size_t granule, uint32_t slots, PtR2GuardApply, void *);
/* Directly usable as PtR2IoConfig.protect; set protect_context to this map. */
int pt_r2_guard_set(void *, int ram, uint64_t physical_page, PtR2IoGuard);
/* I/O reset must release every guard before destroying the map. */
int pt_r2_guard_destroy(PtR2GuardMap *);
#endif
