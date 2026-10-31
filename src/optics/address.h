/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_ADDRESS_H
#define PT_R2_ADDRESS_H
#include <stddef.h>
#include <stdint.h>
/* CPU-owner only. Map callbacks and the mapping generation must remain stable
 * during a call. GPU addresses, guest RAM addresses and host pointers differ. */
typedef struct {
    uint8_t *bytes;
    size_t length;
    uint64_t mapping_generation;
    int gart;
    uint64_t physical; /* VRAM byte offset, or guest RAM physical address; zero is valid. */
} PtR2Span;
typedef int (*PtR2MapPage)(void *,uint64_t gpu_page,int write,PtR2Span *);
typedef struct {
    uint64_t fb_base,fb_length,gart_base,gart_length,generation;
    uint8_t *vram;
    size_t vram_bytes;
    PtR2MapPage map_page;
    void (*dirty)(void *,uint64_t address,size_t bytes,int gart);
    void *context;
} PtR2AddressSpace;
int pt_r2_address_resolve(const PtR2AddressSpace *,uint64_t,int write,PtR2Span *);
/* No bytes are written unless every destination span is mapped. The caller
 * must materialize GPU-owned bytes first. This is not direct-map coherence. */
int pt_r2_address_read(const PtR2AddressSpace *,uint64_t,void *,size_t);
int pt_r2_address_write(const PtR2AddressSpace *,uint64_t,const void *,size_t);
#endif
