/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_ATA_H
#define PT_ATA_H
#include "block.h"
#include "dma.h"
#include "scsi.h"
typedef struct {
    PtBlock *block; PtDma *dma;
    int native_reset,native_dma,native_write,native_flush,native_cache;
    PtScsi *optical;unsigned packet_limit;
    void (*irq)(void *,int); void *opaque;
    uint8_t error,feature,count,lba[3],select,status,control,command,multiple,power;
    uint32_t timing;
    int pending, dma_latch, busy, queued, retry, phase;
    uint64_t generation, lba_next;
    unsigned sectors, buffer_size, position, chunk_sectors;
    uint8_t buffer[256*512]; /* Private: worker never borrows guest RAM. */
} PtAta;
void pt_ata_init(PtAta *,PtBlock *,PtDma *,void (*)(void *,int),void *);
int pt_ata_mmio(PtAta *,uint32_t offset,uint8_t *,unsigned size,int write);
/* CPU scheduling point, including before sleep. 1 means retry/service again.
 * Single controller owns the block completion queue. */
int pt_ata_service(PtAta *);
void pt_ata_dma_interrupt(PtAta *);
#endif
