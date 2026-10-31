/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_DMA_H
#define PT_DMA_H
#include "platform.h"
#include <errno.h>
/* Guest RAM has one coherence owner. A device announces every access before it happens: before() returns 0 to go
 * ahead, EAGAIN while the page is still the GPU's (the device retries on a later service turn and never writes over
 * it), any other value to fail closed. Devices run on the CPU owner, so an announced write also covers the CPU
 * reservation and code coherence before the CPU runs again. No hooks: plain memory (host tests without a GPU). */
typedef struct { int (*before)(void *,uint32_t at,size_t n,int write); void *opaque; } PtRamHooks;
typedef struct { uint8_t *bytes; uint64_t size; const PtRamHooks *hooks; } PtRam;
/* Bounds only; a device touches the bytes through pt_ram_use. */
static inline uint8_t *pt_ram(PtRam r,uint32_t a,size_t n) {
    return r.bytes && (uint64_t)a+n<=r.size && n<=UINT32_MAX
        ? r.bytes+a : NULL;
}
/* A device's access: the bytes, or NULL (out of range or refused); *busy (may be NULL) marks a temporary refusal. */
static inline uint8_t *pt_ram_use(PtRam r,uint32_t a,size_t n,int write,int *busy) {
    uint8_t *p=pt_ram(r,a,n);int e=p&&r.hooks?r.hooks->before(r.hooks->opaque,a,n,write):0;
    if(busy)*busy=p&&e==EAGAIN;return e?NULL:p;
}
enum { PT_DMA_RUN=0x8000, PT_DMA_PAUSE=0x4000, PT_DMA_FLUSH=0x2000,
       PT_DMA_WAKE=0x1000, PT_DMA_DEAD=0x800, PT_DMA_ACTIVE=0x400, PT_DMA_BT=0x100 };
typedef struct {
    PtRam ram; PtBus *bus;
    uint32_t regs[16], descriptor, address, dependency;
    uint16_t command, count, done;
    uint64_t generation,reset_generation;
    int loaded, transferred;
    void (*interrupt)(void *);
    void *opaque;
} PtDma;
typedef struct { uint32_t address; unsigned length; int input,last; uint64_t generation; } PtDmaSlice;
/* All methods are CPU-owner-only. No guest pointer may escape to a worker. */
void pt_dma_init(PtDma *,PtRam,PtBus *,void (*)(void *),void *);
uint32_t pt_dma_read(PtDma *,unsigned offset);
void pt_dma_write(PtDma *,unsigned offset,uint32_t);
/* Bounded descriptor walk: 1=stream slice, 0=stopped/wait/budget, -1=DEAD.
 * Call again on the next scheduler turn, not in an unbounded MMIO loop. */
int pt_dma_next(PtDma *,PtDmaSlice *);
/* Publish after the caller copies bytes. Generation protects reset/reuse. */
int pt_dma_advance(PtDma *,uint64_t generation,unsigned bytes);
void pt_dma_fail(PtDma *);
void pt_dma_device_status(PtDma *,uint8_t);
#endif
