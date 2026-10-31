/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_RING_H
#define PT_R2_RING_H
#include "pm4.h"
#include <stddef.h>
#include <stdint.h>
/* Single-owner module. No callback may reenter or mutate the ring object.
 * The device adapter owns GPU addresses, DMA faults and CP endian normalization.
 * read() returns normalized LE words; execute() receives a complete immutable
 * packet, valid only until it returns. Async work must take its own snapshot. */
typedef int (*PtR2RingRead)(void *,uint32_t first,uint32_t words,void *le_bytes);
/* 0: accepted atomically; 1: busy, no side effects; other: error, no side effects. */
typedef int (*PtR2RingExecute)(void *,const PtR2Pm4Packet *,uint64_t epoch);
typedef enum { PT_R2_RING_EMPTY,PT_R2_RING_MORE,PT_R2_RING_BUDGET,
               PT_R2_RING_BUSY,PT_R2_RING_IO,PT_R2_RING_BAD,PT_R2_RING_EXEC_ERROR } PtR2RingResult;
typedef struct {
    uint32_t words,rptr,wptr,pending_words;
    uint64_t epoch;
    uint8_t *packet;
    size_t capacity;
    PtR2RingRead read;
    PtR2RingExecute execute;
    void *context;
} PtR2Ring;
/* One slot remains empty; a modulo WPTR cannot encode an entirely full ring. */
int pt_r2_ring_init(PtR2Ring *,uint32_t words,uint32_t rptr,PtR2RingRead,PtR2RingExecute,void *);
void pt_r2_ring_destroy(PtR2Ring *);
int pt_r2_ring_wptr(PtR2Ring *,uint32_t);
int pt_r2_ring_reset(PtR2Ring *,uint64_t new_epoch);
PtR2RingResult pt_r2_ring_pump(PtR2Ring *,uint32_t packet_budget,uint32_t *accepted);
#endif
