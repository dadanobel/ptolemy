/* SPDX-License-Identifier: GPL-2.0-or-later */
/* macOS storage for the frontend's scanout snapshots: shared MTLBuffers, so the stand presenter can sample them
 * directly (PtRadeonFrame.metal_buffer). Installed at load. */
#import <Metal/Metal.h>
#include "frontend.h"
static id<MTLDevice> device;
static void *create(void *opaque,size_t bytes,uint8_t **contents) {
    (void)opaque; if(!device) device=MTLCreateSystemDefaultDevice(); if(!device) return NULL;
    id<MTLBuffer> b=[device newBufferWithLength:bytes options:MTLResourceStorageModeShared]; if(!b) return NULL;
    *contents=(uint8_t *)b.contents; return (__bridge_retained void *)b;
}
static void destroy(void *opaque,void *handle) { (void)opaque; id<MTLBuffer> b=(__bridge_transfer id<MTLBuffer>)handle; (void)b; }
__attribute__((constructor)) static void install(void) { OpticsFrameStorage s={create,destroy,NULL}; optics_frontend_set_frame_storage(&s); }
