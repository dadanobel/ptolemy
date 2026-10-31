/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_GPU_MEMORY_H
#define PT_GPU_MEMORY_H
#include <stdint.h>

typedef struct { uint32_t vram, aperture, base; } PtGpuMemory;
/* mac99's current 1 GiB PCI window also contains MacIO. A 1 GiB-aligned
 * framebuffer BAR cannot fit without overlapping it; 512 MiB VRAM needs a
 * different board map, not merely a larger allocation. Zero means 128 MiB. */
static inline int pt_gpu_memory(unsigned mib, PtGpuMemory *out)
{
    if (!out || (mib && mib != 128 && mib != 256)) return 0;
    uint32_t bytes = (mib ? mib : 128u) << 20;
    *out = (PtGpuMemory){bytes, bytes * 2u,
                        bytes == (128u << 20) ? 0x90000000u : 0xa0000000u};
    return 1;
}
/* Aperture 1 aliases the same VRAM with endian/tiling semantics. Accesses
 * crossing the alias boundary must not be interpreted as contiguous bytes. */
static inline int pt_gpu_memory_access(uint64_t size, uint32_t off, unsigned n)
{
    return size && size <= UINT32_MAX / 2u && n &&
           (uint64_t)off + n <= size * 2 && off % size + n <= size;
}
#endif
