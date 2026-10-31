/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_RADEON_H
#define PT_RADEON_H
#include "platform.h"
#include "board.h"
#include "dma.h"
#include <stdio.h>
typedef struct PtRadeon PtRadeon;
typedef struct {uint64_t epoch,seq;} PtGpuTicket;
typedef struct {
    PtRam ram;PtEvent *wake;_Atomic uint32_t *kick;
    void (*irq)(void *,int);void *irq_opaque;
    unsigned width,height,vram_mb; /* Zero: 128 MiB. mac99 supports 128/256. */
    /* Experimental: CPU-owner hook, only while all CP/RQ/GPU work is idle.
     * Revoke finishes BEFORE publishing any new GPU command. NULL = trap all.
     * Interpreter aliases must be revoked together with the stage-2 mapping. */
    void (*direct_map)(void *,uint8_t *,size_t,int enable);void *map_opaque;
    /* CPU owner: a device write reached guest RAM [pa, pa+bytes) (CPU reservation/coherence). NULL: not reported. */
    void (*ram_written)(void *map_opaque,uint64_t pa,size_t bytes);
    /* Own frontend, guarded direct VRAM (direct_map enable 2: stage-2 only, no interpreter alias, at aperture 0; enable 3:
     * the same at aperture 1, the BAR offset of the mapping's size). vram_protect sets the stage-2 permission of the mapped
     * bytes [offset, offset+bytes), 16 KiB granules, prot bit 1 read, 2 write; returns 0
     * (also when nothing is mapped: every access traps then). vram_guard installs the CPU-owner route for stage-2 faults
     * inside the mapping (offset, fault VA, write, native PC and instruction): 0 not ours, 1 resolved, <0 fatal. */
    int (*vram_protect)(void *map_opaque,uint64_t offset,uint64_t bytes,unsigned prot);
    void (*vram_guard)(void *map_opaque,int (*fault)(void *device,uint64_t offset,uint64_t far,int write,uint64_t pc,uint32_t instruction),void *device);
    /* Own frontend: nonzero runs the command processor on its own thread (WPTR only wakes it); the entry points below
     * then take the device lock themselves, and irq/ram_written may be called from that thread. Zero: the CP runs on
     * the caller (deterministic directed tests and trace replay). */
    int cp_thread;
} PtRadeonConfig;
typedef struct {
    void *metal_buffer;const uint8_t *pixels;
    uint32_t width,height,bpp,line;int big_endian;
    uint8_t palette[256][4];
    PtGpuTicket ticket;unsigned slot;
    /* Native GPU frame: an id<MTLTexture> (RGBA8 channels = VRAM bytes 2,1,0,3 of each pixel), pixels NULL. */
    void *metal_texture;
} PtRadeonFrame;
/* One card per process: the Metal device and caches are process-wide, not per-device contexts.
 * All guest/device accesses below are CPU-owner calls, never UI calls. */
int pt_radeon_create(PtRadeon **,const PtRadeonConfig *);
void pt_radeon_config(PtRadeon *,const uint8_t pci[256],const PtAgp *);
int pt_radeon_mmio(PtRadeon *,uint32_t offset,uint8_t *,unsigned,int write);
int pt_radeon_vram(PtRadeon *,uint32_t offset,uint8_t *,unsigned,int write);
int pt_radeon_port(PtRadeon *,unsigned port,uint8_t *,unsigned,int write);
void pt_radeon_service(PtRadeon *,uint64_t guest_ns);
/* CPU owner: one line of executed/refused work, fences, frames and failures (at exit). */
void pt_radeon_stats(PtRadeon *,FILE *);
int pt_radeon_reset(PtRadeon *);
/* CPU owner, the display NDRV (hypercall): switch the scanout mode (depth 8, 16 or 32; fb_offset: the framebuffer's
 * offset in the VRAM BAR, which aperture the CPU draws through), set colour table entries. */
int pt_radeon_display_mode(PtRadeon *,unsigned width,unsigned height,unsigned depth,uint32_t *fb_offset);
int pt_radeon_palette(PtRadeon *,unsigned start,unsigned count,const uint32_t *rgb);
/* CPU owner: the VRAM BAR moved and the board withdrew its mapping; the CPU view (fast VRAM) is mapped anew. */
void pt_radeon_remap(PtRadeon *);
/* UI: acquire a completed immutable snapshot, release after its Metal read
 * completes. Never reuse a slot merely because a newer frame exists. */
int pt_radeon_frame_acquire(PtRadeon *,PtRadeonFrame *);
void pt_radeon_frame_release(PtRadeon *,const PtRadeonFrame *);
/* CPU stopped, UI detached, final UI submission drained before close. */
void pt_radeon_close(PtRadeon *);
#endif
