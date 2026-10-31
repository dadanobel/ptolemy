/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_EXPORT_H
#define PT_EXPORT_H
#include "pt_agent.h"
typedef struct PtExport PtExport;
/* Server/control thread creates/services/closes; CPU only enqueues bounded data.
 * One in-order stream, 64-bit offsets, last-chunk retry is idempotent. */
PtExport *pt_export_open(const char *directory,const char *name);
void pt_export_close(PtExport *);
int pt_export_begin(PtExport *,uint64_t size);
int pt_export_chunk(PtExport *,uint64_t offset,const void *,unsigned);
int pt_export_commit(PtExport *,const uint8_t sha[32]);
void pt_export_cancel(PtExport *);
void pt_export_service(PtExport *);
int pt_export_complete(PtExport *);
const char *pt_export_path(PtExport *);
#endif
