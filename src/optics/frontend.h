/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Radeon 9700 (R300) device frontend for the stand: the include/radeon.h contract. CPU/I/O-owner only (with
 * config.cp_thread the entry points lock against the CP thread themselves).
 * Internal header for the host adapter and the trace replay test. */
#ifndef OPTICS_FRONTEND_H
#define OPTICS_FRONTEND_H
#include "radeon.h"
#include "diagnostics.h"
#include <stdint.h>
#include <stddef.h>

/* Host storage for one immutable scanout snapshot. The macOS adapter backs it with a shared MTLBuffer whose
 * contents pointer is `bytes`; `handle` is what PtRadeonFrame.metal_buffer carries. NULL hooks use malloc. */
typedef struct {
    void *(*create)(void *opaque,size_t bytes,uint8_t **contents);
    void (*destroy)(void *opaque,void *handle);
    void *opaque;
} OpticsFrameStorage;
void optics_frontend_set_frame_storage(const OpticsFrameStorage *);

/* Counters of everything the frontend refused or did not model, by class. Never a silent success. */
typedef struct {
    uint64_t draws,packets,type0_words,ib_launches,blits,blit_bytes,draws_refused,packets_refused,blits_refused,
        translate_faults,unknown_writes,writebacks,frames;
    uint32_t last_refused_opcode,last_refused_gmc,last_fault_address;
    uint64_t approx_mip,approx_border,approx_aniso,approx_tiled;   /* draws executed with OPTICS_APPROX_* state */
    uint64_t fence_waits,completions_deferred,materialize_failures,native_frames,copied_frames,rbbm_reads,guard_faults,guard_emulated,guard_groups,direct_switches;
    uint64_t cpu_vram_accesses,cpu_vram_bytes;   /* trapped CPU aperture accesses (OPTICS_HITCH) */
    int failed;                         /* GPU/export failure: the CP stopped; only a device reset clears it */
} OpticsFrontendStats;
void optics_frontend_stats(const PtRadeon *,OpticsFrontendStats *);
/* Fixed ABI diagnostics, sampled on the CPU owner. A missing device reports DISABLED. */
void optics_frontend_status(PtRadeon *,OpticsStatus *);
#include "executor.h"
#include "io.h"
/* Fast VRAM, the CPU view of VRAM (cpu_guard.c installs the HVF fault route; tests run without it, trapped). */
typedef int (*OpticsCpuFault)(void *device,uint64_t offset,uint64_t far,int write,uint64_t pc,uint32_t instruction);
void optics_frontend_set_cpu_fault(OpticsCpuFault);
int optics_frontend_cpu_access(PtRadeon *,uint64_t offset,uint64_t bytes,int write);   /* resolve GPU ownership: 0 = VRAM current */
int optics_frontend_cpu_grant(PtRadeon *,uint64_t offset,int write);   /* fast VRAM: stage-2 fault on the CPU view; 1 = re-run, -1 = fatal */
int optics_frontend_guard_state(PtRadeon *,uint64_t offset,PtR2IoGuard *child,PtR2IoGuard *group);
void optics_frontend_guard_count(PtRadeon *,int kind);   /* 0 fault, 1 emulated access, 2 group resolved */
/* Test hook: every device write-back into guest RAM (physical address, LE dword value), in program order. */
typedef void (*OpticsWritebackHook)(void *,uint64_t pa,uint32_t value);
void optics_frontend_set_writeback_hook(OpticsWritebackHook,void *);
/* Test hook: copy of the scanout active rows (the bytes a FRAME record hashes). Returns bytes written. */
size_t optics_frontend_scanout(PtRadeon *,uint8_t *out,size_t capacity,uint32_t *width,uint32_t *height,uint32_t *bpp,uint32_t *line);
/* Test hook: 1 when the CP holds no unexecuted packet (none waits on a completion fence) and is not running. */
int optics_frontend_idle(PtRadeon *);
/* Another device's DMA to guest RAM [at, at+bytes) passes the GPU's ownership first: 0, EAGAIN (retry later), EIO. */
int optics_frontend_ram_before(PtRadeon *,uint64_t at,size_t bytes,int write);
/* Scanout byte lane XOR a CPU would see through aperture 0 (0, 1 or 3): converts native rows to the CPU view. */
unsigned optics_frontend_cpu_view_xor(const PtRadeon *);
#endif
