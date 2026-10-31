/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_GUARD_H
#define PT_R2_GUARD_H
#include "io.h"
typedef struct PtR2GuardMap PtR2GuardMap;
/* One call protects all actual HVF/interpreter aliases of this physical group.
 * Query the VM's real IPA granule; do not assume guest pages == host pages. */
typedef int (*PtR2GuardApply)(void *,int ram,uint64_t physical_group,size_t granule,PtR2IoGuard);
PtR2GuardMap *pt_r2_guard_create(size_t granule,uint32_t slots,PtR2GuardApply,void *);
/* Directly usable as PtR2IoConfig.protect; set protect_context to this map. */
int pt_r2_guard_set(void *,int ram,uint64_t physical_page,PtR2IoGuard);
/* A child may be OPEN while its group is not. Emulate that one faulting
 * access; NEVER unprotect the group to run it and expose neighboring owners. */
int pt_r2_guard_get(PtR2GuardMap *,int ram,uint64_t physical,PtR2IoGuard *child,PtR2IoGuard *group);
/* Reapply every non-open group (after the storage is mapped again). 1 on success. */
int pt_r2_guard_reapply(PtR2GuardMap *);
/* I/O reset must release every guard before destroying the map. */
int pt_r2_guard_destroy(PtR2GuardMap *);
#endif
