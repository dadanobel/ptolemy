/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_NVRAM_H
#define PT_NVRAM_H
#include "platform.h"
enum { PT_NVRAM_SIZE=16384 };
typedef struct PtNvramStore PtNvramStore;
typedef struct { uint8_t data[PT_NVRAM_SIZE]; uint64_t generation; PtNvramStore *store; } PtNvram;
/* Firmware OF view and guest CHRP bank; CPU-owned bytes, worker-owned snapshots. */
void pt_nvram_init(PtNvram *);
/* Rebuilds a damaged Open Firmware view with default partitions; 1 when it did. */
int pt_nvram_repair_of(PtNvram *);
int pt_nvram_set_of(PtNvram *,const char *name,const char *value);
int pt_nvram_access(PtNvram *,unsigned,uint8_t *,unsigned,int write);
/* Existing files must be exactly 16 KiB. No automatic reformat on bad input.
 * Path is explicit; never derive writable state from a read-only disk path. */
int pt_nvram_open(PtNvram *,const char *path);
int pt_nvram_service(PtNvram *); /* try submit snapshot; nonblocking; errno */
int pt_nvram_close(PtNvram *); /* control thread: commit latest and join */
#endif
