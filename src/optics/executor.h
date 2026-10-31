/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 3D executor interface of the Radeon frontend. Plain C without radeon.h, so the Objective-C++ Metal executor
 * shares this one definition with the frontend. */
#ifndef OPTICS_EXECUTOR_H
#define OPTICS_EXECUTOR_H
#include "draw.h"
#include "timeline.h"
#include "diagnostics.h"
#include <stddef.h>
#include <stdint.h>
typedef struct PtRadeon PtRadeon;
/* 3D executor (Metal in the product, a CPU reference in tests). The table is stateless; every call gets the device's
 * own instance from create(), so reset/close reach all GPU-owned state (no process-global executor state).
 * All calls are CPU/I/O-owner calls except where noted. */
enum { OPTICS_EXEC_DONE, OPTICS_EXEC_REFUSED, OPTICS_EXEC_FAILED };          /* draw: refused = unsupported state, counted */
enum { OPTICS_FENCE_DONE, OPTICS_FENCE_PENDING, OPTICS_FENCE_FAILED };       /* fence: every earlier GPU command */
enum { OPTICS_FRAME_NONE, OPTICS_FRAME_SAME, OPTICS_FRAME_PENDING, OPTICS_FRAME_READY, OPTICS_FRAME_FAILED };
typedef struct {
    void *(*create)(PtRadeon *);                     /* per device; NULL instance is allowed (stateless executor) */
    void (*destroy)(void *,PtRadeon *);              /* close: CPU stopped, UI detached; drains its GPU work */
    int (*reset)(void *,PtRadeon *);                 /* device reset: write back and drop every GPU-owned resource */
    int (*draw)(void *,PtRadeon *,const OpticsDraw *,const char **reason);
    /* VRAM byte range [start,end) is about to be accessed by the CPU, 2D, a fetch or the scanout: GPU-produced bytes
     * are written back first. write: the access modifies them. 0 = VRAM holds the current bytes; else they are not
     * current and the access must not proceed (a failed export never turns into clean data). */
    int (*materialize)(void *,PtRadeon *,uint64_t start,uint64_t end,int write);
    /* Fast clear of a buffer (plane 1 colour, 2 depth) by GPU address, applied at its next use. 0 = accepted. */
    int (*clear)(void *,PtRadeon *,uint64_t gpu_offset,int plane);
    /* Guest-visible completion (scratch, SW interrupt, RBBM idle): never blocks. PENDING work must eventually be
     * submitted by this executor and post optics_frontend_wake on completion; lazy policy may defer its commit. */
    int (*fence)(void *,PtRadeon *);
    int (*poll)(void *,PtRadeon *);                  /* completions only (no commit); never blocks; 0 or -1 failed */
    /* Native scanout of VRAM [start, start+line*height): NONE when no GPU-owned surface covers it (the caller copies
     * VRAM), SAME when the last published frame is still current, PENDING while its GPU copy is in flight, READY with
     * a retained immutable completed frame (*handle for frame_release, *texture an id<MTLTexture>). */
    int (*frame)(void *,PtRadeon *,uint64_t start,uint32_t width,uint32_t height,uint32_t line,void **handle,void **texture);
    void (*frame_release)(void *handle);             /* any thread */
    void (*dump)(void *,PtRadeon *,const char *dir,unsigned tag);   /* development: OPTICS_SURFACE_DUMP */
    void (*status)(void *,PtRadeon *,OpticsStatus *);   /* renderer metrics and capabilities (diagnostics.h), CPU owner */
    /* Samples that passed depth and stencil in the draws of occlusion query `query` (OpticsDraw.query), asked
     * once after a fence covering them completed; the query is forgotten. 0 = *count valid. */
    int (*query)(void *,PtRadeon *,uint32_t query,uint64_t *count);
} OpticsExecutor;
void optics_frontend_set_executor(const OpticsExecutor *);
const OpticsExecutor *optics_metal_executor(void);   /* the product Metal executor (installed by its constructor; directed tests) */
/* Any thread (GPU completion): post a CPU-owner event so a CP waiting on a fence resumes. No guest interrupt. */
void optics_frontend_wake(PtRadeon *);
/* Frontend services for the executor (CPU owner). */
int optics_trace(void);   /* OPTICS_TRACE=1: development traces on */
uint32_t optics_frontend_register(const PtRadeon *,uint32_t offset);   /* current register value, no side effects */
int optics_frontend_vram_offset(const PtRadeon *,uint64_t gpu,uint64_t *offset);
int optics_frontend_gpu_read(PtRadeon *,uint64_t gpu,void *,size_t);
/* Trapped CPU aperture accesses so far, and their bytes (for the OPTICS_HITCH log). */
void optics_frontend_cpu_vram(const PtRadeon *,uint64_t *accesses,uint64_t *bytes);
int optics_frontend_gpu_write(PtRadeon *,uint64_t gpu,const void *,size_t);
/* GPU completion ledger (timeline.h): after a commit, the executor submits every job issued since the last submission
 * (token) and reports that token complete once the batches holding those jobs completed (failed if one failed). */
uint64_t optics_frontend_job(PtRadeon *);   /* the frontend issues one sequence per GPU job the executor accepted */
int optics_frontend_timeline_submit(PtRadeon *,PtR2TimelineToken *);
void optics_frontend_timeline_complete(PtRadeon *,PtR2TimelineToken,int success);
/* VRAM page ownership (io.h): GPU-owned destinations (write_begin .. write_complete/abort) and watched clean GPU copies
 * (pt_r2_io_watch .. release) of VRAM byte offsets. Call optics_frontend_cpu_sync on the range first - the
 * granules the CPU holds in its view (fast VRAM) come back to VRAM. 0 = VRAM current. */
struct PtR2Io *optics_frontend_io(PtRadeon *);
int optics_frontend_cpu_sync(PtRadeon *,uint64_t offset,uint64_t bytes);
#endif
