/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_SCSI_H
#define PT_SCSI_H
#include "block.h"
enum { PT_SCSI_IDLE,PT_SCSI_DONE,PT_SCSI_IN,PT_SCSI_OUT,PT_SCSI_READ_WAIT,PT_SCSI_WRITE_WAIT,PT_SCSI_FLUSH_WAIT };
typedef struct {
    PtBlock *block;
    int native_cancelled,native_fua;
    unsigned sector,phase,status,key,asc,ascq,unit_attention;
    int optical,read_only,present,locked,pending,retry;
    uint64_t generation,lba,expected,transferred;
    uint32_t blocks,chunk;
    unsigned position,length,event;
    uint8_t data[128*1024];
    void (*eject)(void *);void *opaque; /* CPU owner, enqueue only */
} PtScsi;
void pt_scsi_init(PtScsi *,int optical);
void pt_scsi_reset(PtScsi *);
/* Prepared backend; no host I/O here. Caller closes returned old backend off
 * the CPU thread, and only after successful detach. No simultaneous owners. */
int pt_scsi_media(PtScsi *,PtBlock *,int read_only,PtBlock **old);
int pt_scsi_command(PtScsi *,const uint8_t *cdb,unsigned length);
int pt_scsi_service(PtScsi *);
/* Bytes available, or -EAGAIN while worker is running, 0 at command end. */
int pt_scsi_read(PtScsi *,void *,unsigned);
int pt_scsi_write(PtScsi *,const void *,unsigned);
#endif
