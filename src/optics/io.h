/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_IO_H
#define PT_R2_IO_H
#include "address.h"
typedef struct PtR2Io PtR2Io;
typedef struct {uint64_t epoch,id;} PtR2IoToken;
typedef enum {PT_R2_IO_OK,PT_R2_IO_BUSY,PT_R2_IO_ARGUMENT,PT_R2_IO_MAPPING,
    PT_R2_IO_CAPACITY,PT_R2_IO_COHERENCE,PT_R2_IO_STALE} PtR2IoResult;
typedef enum {PT_R2_IO_OPEN,PT_R2_IO_WATCH_WRITES,PT_R2_IO_GPU_OWNED} PtR2IoGuard;
/* Encoding owner only. RAM mapping/allocation is stable until reset.
 * guard must synchronously revoke ALL relevant HVF/interpreter aliases before
 * returning success. RAM DMA writers must use before_physical(...,write=1).
 * It must not reenter this object. Failure must not weaken prior protection.
 * A stub that returns success without real protection is not integration. */
typedef int (*PtR2IoProtect)(void *,int ram,uint64_t physical_page,PtR2IoGuard);
typedef struct {
    PtR2AddressSpace address;
    uint32_t pages,transfers; /* pages: power of two; fixed admission limits */
    uint64_t snapshot_bytes;
    PtR2IoProtect protect;
    void *context;
    /* Physical write notification, BEFORE ownership/fence publication. Route
     * through the CPU's RAM-dirty/reservation/code-coherence protocol. It must
     * not retranslate a GART address or reenter this I/O object. */
    void (*written)(void *,int ram,uint64_t physical,size_t bytes);
    void *protect_context; /* NULL uses context; lets a guard aggregator wrap protection only. */
} PtR2IoConfig;
typedef struct {
    uint64_t epoch,cpu_reads,cpu_writes,snapshots,gpu_writes,invalidations,busy,guard_calls;
    uint64_t snapshot_bytes;
    uint32_t pages,transfers;
    int fatal;
} PtR2IoStats;
PtR2Io *pt_r2_io_create(const PtR2IoConfig *);
/* Reset first invalidates every token. CPU must be stopped. Protection failure
 * leaves the device failed closed; do not resume CPU or silently ignore it. */
PtR2IoResult pt_r2_io_reset(PtR2Io *);
PtR2IoResult pt_r2_io_destroy(PtR2Io *);
/* Raw GPU bytes, or lane XOR 1 (swap16), 2 (half-DWORD), 3 (swap32).
 * Mode is chosen by the register frontend; this does not guess SURFACE state.
 * No allocation on CPU access. Host data buffers must not alias guest storage. */
PtR2IoResult pt_r2_io_cpu_read(PtR2Io *,uint64_t,void *,size_t,unsigned lane_xor);
PtR2IoResult pt_r2_io_cpu_write(PtR2Io *,uint64_t,const void *,size_t,unsigned lane_xor);
/* All RAM/DMA or direct-map fault paths call this BEFORE access. Physical page
 * identity makes GART aliases share ownership and invalidation. BUSY means
 * request export/materialization and yield; no GPU wait while holding a lock. */
PtR2IoResult pt_r2_io_before_physical(PtR2Io *,int ram,uint64_t,size_t,int write);
PtR2IoResult pt_r2_io_snapshot(PtR2Io *,uint64_t,size_t,PtR2IoToken *);
PtR2IoResult pt_r2_io_snapshot_data(PtR2Io *,PtR2IoToken,const uint8_t **,size_t *);
/* After Metal upload: retain only versions/guards, not a second CPU texture copy. */
PtR2IoResult pt_r2_io_snapshot_forget_data(PtR2Io *,PtR2IoToken);
/* Detach malloc storage for an immutable cross-owner job. Caller owns/free().
 * The token retains version metadata. Reset cannot free the detached bytes. */
PtR2IoResult pt_r2_io_snapshot_take_data(PtR2Io *,PtR2IoToken,uint8_t **,size_t *);
int pt_r2_io_snapshot_valid(PtR2Io *,PtR2IoToken);
PtR2IoResult pt_r2_io_release(PtR2Io *,PtR2IoToken);
/* WATCH_WRITES guard and versions of a snapshot without copying its bytes; pt_r2_io_release ends it. */
PtR2IoResult pt_r2_io_watch(PtR2Io *,uint64_t,size_t,PtR2IoToken *);
/* Reserve a RESOURCE VERSION before publishing its first GPU write, not once
 * per draw/scissor. Destination physical spans are captured now. GART remap
 * never redirects later writeback to a newly mapped page. */
PtR2IoResult pt_r2_io_write_begin(PtR2Io *,uint64_t,size_t,PtR2IoToken *);
PtR2IoResult pt_r2_io_write_abort(PtR2Io *,PtR2IoToken);
int pt_r2_io_write_pending(PtR2Io *,PtR2IoToken);
PtR2IoResult pt_r2_io_write_complete(PtR2Io *,PtR2IoToken,const void *,size_t);
/* GPU RGBA8 export: write only row pixels, preserve pitch padding. lanes map
 * output byte -> RGBA component; an explicit permutation, not endian guessing. */
PtR2IoResult pt_r2_io_write_rgba_rows(PtR2Io *,PtR2IoToken,const uint8_t *,size_t,
    uint32_t width,uint32_t height,size_t pitch,const uint8_t lanes[4]);
void pt_r2_io_stats(const PtR2Io *,PtR2IoStats *);
const char *pt_r2_io_result(PtR2IoResult);
#endif
