/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_SURFACE_METAL_H
#define PT_R2_SURFACE_METAL_H
#import <Metal/Metal.h>
#include "msaa.h"

/* Offscreen building block, not the pt_gpu_core_* device. Per-instance state;
 * no shaderc/GLSL or process globals. Native colour attachments only; Z24S8,
 * CPU aliases and materialization remain executor responsibilities. */
@interface PtR2MetalSurface : NSObject
@property(nonatomic,readonly) id<MTLTexture> color;
@property(nonatomic,readonly) id<MTLTexture> resolved;
@property(nonatomic,readonly) PtR2MsaaPlan samples;
@property(nonatomic,readonly) uint64_t allocatedBytes;
+ (instancetype)surfaceWithDevice:(id<MTLDevice>)device width:(uint32_t)width
    height:(uint32_t)height input:(PtR2MsaaInput)input budget:(uint64_t)budget
    error:(NSError **)error;
/* Caller ends this encoder before resolve; Store preserves all samples.
 * Load is valid only after initialization by a clear/draw/import. */
- (MTLRenderPassDescriptor *)renderPassWithLoad:(MTLLoadAction)load clear:(MTLClearColor)clear;
@end

@interface PtR2MetalResolver : NSObject
- (instancetype)initWithDevice:(id<MTLDevice>)device error:(NSError **)error;
/* Appends compute work to caller's ordered command buffer. Never commits,
 * waits, exports to CPU, or releases a surface early. Keep resources alive
 * through completion when using unretained command buffers. */
- (BOOL)encodeSurface:(PtR2MetalSurface *)surface commandBuffer:(id<MTLCommandBuffer>)command
    control:(uint32_t)control error:(NSError **)error;
@end
#endif
