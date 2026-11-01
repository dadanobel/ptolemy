/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#if !defined(__APPLE__) || !defined(__arm64__) || !defined(__OBJC__) || !defined(__cplusplus)
#error "Radeon2 is an Apple Silicon Metal backend, compiled as Objective-C++."
#endif
#import <Metal/Metal.h>
#include <stdint.h>
extern "C" {
#include "foundation/shader_ir.h"
#include "foundation/msaa.h"
#include "pixel.h"
}

/* Normalized backend ABI 1. NOT a fake R300 register or Tiger GLD interface.
 * The device frontend supplies the exact VAP/RS/format/ownership contract.
 * One encoding owner thread; GPU completion handlers only publish tiny events. */
typedef struct { uint64_t epoch,sequence; } PtR2Ticket;
typedef struct {
    uint32_t maxBatches,maxJobs;
    uint64_t residentBytes,batchBytes;
} PtR2Limits;
/* Conservative starting budget, not a performance promise. Explicit benchmark
 * limits remain fixed and recorded; never change guest time or FP precision. */
PtR2Limits pt_r2_default_limits(id<MTLDevice>);
typedef struct { float attribute[8][4]; } PtR2Vertex;
typedef struct {
    uint16_t offset;
    uint8_t components,format; /* 1..4; raw_vertex.h formats (0 LE float, 1 BE float, 2 UNORM8). */
} PtR2Attribute;
typedef struct {
    uint8_t positionOutput,colorOutput,varyingCount;
    uint8_t pvsOutput[8],usInput[8];
    uint8_t sampleCount,colorMask,blend; /* RGBA bits 0..3; blend 0 off, 1 alpha, 2 premultiplied, 3 add */
    uint16_t stride; /* Zero selects the PtR2Vertex fixture ABI. Otherwise raw immutable guest bytes. */
    PtR2Attribute attribute[16];   /* 16, the R300 PVS input vectors */
    /* Explicit blend (blend must be 0): bit 31 enable; RGB src 3:0, dst 7:4, op 10:8; alpha src 15:12, dst 19:16,
     * op 22:20; MTLBlendFactor Zero..OneMinusBlendAlpha (the blend colour is in PtR2Raster) and MTLBlendOperation values. */
    uint32_t blendState;
    /* R300 vertex-stage epilogue, so the guest's PVS program itself runs in the Metal vertex function.
     * vte 1: position = SE_VTE/SE_VPORT transform of PVS output positionOutput (clip space) with the parameters in
     * PtR2Raster.vertexEpilogue[0..3] (x scale, x offset, y scale, y offset, already in Metal clip form) and [4..5] (z scale,
     * z offset); no further z remap. selectVaryings 1: varying j component c = select[j][c] (0..3 component of its PVS
     * output, 4 zero, 5 one: the RS selection). pad must be zero. */
    /* vte 2: full VTE mask in vertexEpilogue[8], viewport [0..5], target [6..7]. */
    uint8_t vte,selectVaryings,select[8][4],pad[2];
    uint16_t vapSelect[16]; /* Raw VAP formats: component selectors and write mask. */
    /* Fixed-function stages (draw.h): fog 1 blends the FG fog after the fragment program, its input from fogSelect 0..3
     * (the alpha of PVS output fogOutput), 4 (clip w) or 5 (clip z); clipPlanes: user planes 0..5 (PtR2Raster.clipPlane);
     * flat: varyings interpolated flat; rop 0 or 1 + the GL logic op truth table (blending off, the target read back in
     * the fragment function); textureFixed 1: samples take PtR2Raster's LOD bias and border emulation; generated: varyings
     * (bit j) that are the negated window position shifted right by generatedShift (x 3:0, y 7:4), the polygon stipple's
     * coordinates, component c from select[j][c] (0 x, 1 y, 4 zero, 5 one); discard 0..6: draw.h's source pixel discard. */
    uint8_t fog,fogSelect,fogOutput,clipPlanes,flat,rop,textureFixed,generated,generatedShift,discard,pad2[2];
} PtR2Route;
typedef struct {uint8_t compare,fail,depthFail,pass;} PtR2StencilFace;
typedef struct {
    double x,y,width,height,znear,zfar;
    uint32_t scissorX,scissorY,scissorWidth,scissorHeight;
    uint8_t depthCompare,depthWrite,cull,frontCCW;
    /* Compare 0..7: never, less, equal, lessEqual, greater, notEqual, greaterEqual, always. */
    uint8_t stencilEnable,stencilReadMask,stencilWriteMask,stencilReference;
    PtR2StencilFace stencilFront,stencilBack;
    uint8_t alphaTest,alphaCompare;
    float alphaReference;
    /* samplers 1 = per texture unit sampler[j]: bits 2:0 S and 5:3 T address mode (MTLSamplerAddressMode values
     * 0..5), bit 6 linear magnification, bit 7 linear minification, bits 9:8 between levels (0 none: level lodMin only,
     * 1 nearest, 2 linear), 12:10 log2 of the largest anisotropy, 16:13 the first level used (lodMinClamp), 19:17 R (3D);
     * 0 = the draw's linear flag. */
    uint8_t samplers;
    uint32_t sampler[16];
    float vertexEpilogue[12]; /* Host VTE uniforms, separate from all 256 guest PVS constants. */
    /* lines 1: the indices are pairs, drawn as one-pixel lines; blendColor: RGBA of the constant blend factors. */
    uint8_t lines;
    float blendColor[4];
    float depthBias,depthSlope;   /* polygon offset (Metal setDepthBias units and slope scale) */
    uint32_t query;               /* occlusion query whose samples this draw counts, 0 none */
    /* Fixed-function values (PtR2Route): fog colour RGB, then scale and offset; clip-space planes; per texture unit the
     * LOD bias, the border colour, the chroma key and textureMode (bits 1:0 S, 3:2 T: 1 border, 2 clamp to 0..1 then
     * border; bit 4 linear filtering; 6:5 chroma key 1 kill, 2 transparent black). fill 1: triangles as lines. */
    float fog[5];
    float clipPlane[6][4];
    float lodBias[16],border[16][4],chromaKey[16][4];
    uint32_t textureMode[16];
    uint8_t fill;
} PtR2Raster;
/* A texture upload with mip levels: level k (max(1, size >> k) texels) of face f starts at offset[k] +
 * f * face[k] in the bytes, with rows of row[k] bytes. */
typedef struct { unsigned levels; size_t offset[13],row[13],face[13]; } PtR2Levels;
typedef struct {
    PtR2Ticket ticket;
    uint64_t submittedNS,completedNS;
    double gpuSeconds;
    int success;
} PtR2Completion;
@class PtR2Program,PtR2Target,PtR2Texture,PtR2Readback,PtR2Frame,PtR2Depth;

@interface PtR2Program : NSObject
@property(nonatomic,readonly) NSString *vertexMSL;
@property(nonatomic,readonly) NSString *fragmentMSL;
@end
@interface PtR2Texture : NSObject @end
@interface PtR2Target : NSObject
@property(nonatomic,readonly) uint32_t width;
@property(nonatomic,readonly) uint32_t height;
@property(nonatomic,readonly) uint32_t samples;   /* Metal samples per pixel */
@end
/* Depth/stencil storage independent of any colour target (normalized D32Float/Stencil8, not the R300
 * D24S8 bytes). Storage no pass has initialized yet is cleared to depth 1, stencil 0 by the first pass that uses it. */
@interface PtR2Depth : NSObject
@property(nonatomic,readonly) uint32_t width;
@property(nonatomic,readonly) uint32_t height;
@end
@interface PtR2Readback : NSObject
@property(nonatomic,readonly) PtR2Ticket ticket;
@property(nonatomic,readonly) uint32_t width;
@property(nonatomic,readonly) uint32_t height;
@end
/* Immutable native frame. The UI may retain it on another thread. Retain this
 * lease through the PRESENTING command buffer's completion, not only encoding.
 * The texture is read-only by contract; do not write or alias its storage. */
@interface PtR2Frame : NSObject
@property(nonatomic,readonly) id<MTLTexture> texture;
@property(nonatomic,readonly) PtR2Ticket ticket;
@end

@interface PtR2Renderer : NSObject
- (instancetype)initWithDevice:(id<MTLDevice>)device limits:(PtR2Limits)limits error:(NSError **)error;
/* Serial queue ownership survives libdispatch worker-thread migration. Create
 * and use on this queue. nil retains the dedicated-pthread contract above. */
- (instancetype)initWithDevice:(id<MTLDevice>)device limits:(PtR2Limits)limits ownerQueue:(dispatch_queue_t)queue error:(NSError **)error;
/* Without an owner queue, the calling thread becomes the encoding owner. The caller guarantees one
 * thread at a time with a lock (the device lock: the CP thread and the CPU thread take turns). */
- (void)adoptOwner;
/* Prepare outside a hot draw loop. Direct US/PVS -> MSL, complete lossless key.
 * Native program objects are reused by draw; no per-draw text/dictionary search. */
- (PtR2Program *)preparePVS:(const PtR2ShaderIR *)pvs vertexInterface:(const PtR2ShaderInterface *)vi
    fragment:(const PtR2ShaderIR *)us interface:(const PtR2ShaderInterface *)fi
    route:(PtR2Route)route error:(NSError **)error;
- (PtR2Target *)targetWidth:(uint32_t)width height:(uint32_t)height samples:(PtR2MsaaInput)samples error:(NSError **)error;
/* Immutable RGBA8 level-zero snapshot. Upload bytes are copied, never retained as a guest pointer. */
- (PtR2Texture *)textureWidth:(uint32_t)width height:(uint32_t)height bytes:(const void *)bytes
    rowBytes:(size_t)rowBytes error:(NSError **)error;
/* GPU linear format/endian conversion, including OpenBIOS XRGB32BE scanout.
 * No CPU per-pixel conversion; unsupported tiled/compressed formats are not
 * accepted by pretending they are linear. */
- (PtR2Texture *)textureWidth:(uint32_t)width height:(uint32_t)height bytes:(const void *)bytes
    rowBytes:(size_t)rowBytes layout:(OpticsPixelLayout)layout error:(NSError **)error;
/* The same for a cube map (faces 6, square): face f's rows start at bytes + f * faceBytes. faces 1: as above. */
- (PtR2Texture *)textureWidth:(uint32_t)width height:(uint32_t)height faces:(unsigned)faces faceBytes:(size_t)faceBytes
    bytes:(const void *)bytes rowBytes:(size_t)rowBytes layout:(OpticsPixelLayout)layout error:(NSError **)error;
/* With mip levels (PtR2Levels). */
- (PtR2Texture *)textureWidth:(uint32_t)width height:(uint32_t)height faces:(unsigned)faces levels:(const PtR2Levels *)levels
    bytes:(const void *)bytes length:(size_t)length layout:(OpticsPixelLayout)layout error:(NSError **)error;
/* A 3D texture with mip levels: level k has max(1, depth >> k) slices, slice z's rows at bytes + offset[k] + z * face[k]. */
- (PtR2Texture *)textureWidth:(uint32_t)width height:(uint32_t)height depth:(uint32_t)depth levels:(const PtR2Levels *)levels
    bytes:(const void *)bytes length:(size_t)length layout:(OpticsPixelLayout)layout error:(NSError **)error;
/* Immutable RGBA32F texture (float formats, expanded by the caller): `levels` levels one after another, tightly packed. */
- (PtR2Texture *)floatTextureWidth:(uint32_t)width height:(uint32_t)height levels:(unsigned)levels bytes:(const float *)rgba error:(NSError **)error;
/* Colour import of a guest surface into a single-sample target (same size); depth/stencil cleared. */
/* RB3D_AARESOLVE: the multisampled source resolved (control: RB3D_AARESOLVE_CTL), then a rectangle of it copied into a
 * single-sample target. */
- (BOOL)resolve:(PtR2Target *)source control:(uint32_t)control x:(uint32_t)x y:(uint32_t)y width:(uint32_t)width height:(uint32_t)height
    to:(PtR2Target *)target x:(uint32_t)tx y:(uint32_t)ty error:(NSError **)error;
- (BOOL)load:(PtR2Target *)target from:(PtR2Texture *)texture error:(NSError **)error;
/* A sampleable view of a single-sample target's colour with a component swizzle (component[k] 0..3 = R,G,B,A
 * of the target, 4 zero, 5 one), to sample a GPU-owned guest surface without a readback. Shares the target's storage;
 * reads follow GPU encoding order. Not for sampling the target of the same draw. */
- (PtR2Texture *)view:(PtR2Target *)target component:(const uint8_t *)component error:(NSError **)error;
/* The top-left width x height of an initialized single-sample target as a new texture, copied on the GPU in the current
 * batch (encoding order, no wait), with view:'s component swizzle. Tiger's window server samples a window's visible
 * width of its padded backing store (892 of 896); without this copy those draws would read the surface back to the CPU. */
- (PtR2Texture *)copyView:(PtR2Target *)target width:(uint32_t)width height:(uint32_t)height component:(const uint8_t *)component error:(NSError **)error;
/* Copy a colour rectangle between initialized single-sample targets (R300 AA resolve of a 1-sample
 * buffer: Apple's GL driver swaps a window's back buffer this way). Encoded in the current batch, no wait. */
- (BOOL)copy:(PtR2Target *)source x:(uint32_t)sx y:(uint32_t)sy width:(uint32_t)w height:(uint32_t)h
    to:(PtR2Target *)destination x:(uint32_t)dx y:(uint32_t)dy error:(NSError **)error;
/* Independent depth/stencil. target:depth: selects the storage a single-sample target's following passes
 * use (same size; nil returns to the target's own); clearDepth clears its planes (mask 2 depth, 4 stencil) in a
 * depth-only pass. Two targets may share one storage; one target may alternate storages. */
- (PtR2Depth *)depthWidth:(uint32_t)width height:(uint32_t)height error:(NSError **)error;
- (BOOL)target:(PtR2Target *)target depth:(PtR2Depth *)depth error:(NSError **)error;
- (BOOL)clearDepth:(PtR2Depth *)depth mask:(uint8_t)mask depth:(double)value stencil:(uint8_t)stencil error:(NSError **)error;
/* Preserve both planes when capacity grows. Fresh destination tails start at 1/0.
 * No CPU readback and no wait; the batch retains both allocations. */
- (BOOL)copyDepth:(PtR2Depth *)source to:(PtR2Depth *)destination error:(NSError **)error;
/* Linear D16/D24S8 plane transfer, kept asynchronous until its own ticket completes.
 * Only rows [y,y+height) are imported; private capacity outside them is preserved. */
- (BOOL)loadDepth:(PtR2Depth *)depth y:(uint32_t)y height:(uint32_t)height
    bytes:(const void *)bytes format:(unsigned)format error:(NSError **)error;
- (PtR2Readback *)readbackDepth:(PtR2Depth *)depth y:(uint32_t)y height:(uint32_t)height error:(NSError **)error;   /* rows y..y+height-1 */
- (NSData *)copyDepthReadback:(PtR2Readback *)readback format:(unsigned)format error:(NSError **)error;
/* Clears and draws stay on GPU. An entire target clear, not a masked/scissored guest clear. */
- (BOOL)clear:(PtR2Target *)target color:(MTLClearColor)color depth:(double)depth error:(NSError **)error;
- (BOOL)clear:(PtR2Target *)target color:(MTLClearColor)color depth:(double)depth stencil:(uint8_t)stencil error:(NSError **)error;
/* Whole target, independent planes: mask bits 1 color / 2 depth / 4 stencil.
 * Unselected planes must already be initialized/imported. Not a scissored clear. */
- (BOOL)clear:(PtR2Target *)target mask:(uint8_t)mask color:(MTLClearColor)color depth:(double)depth stencil:(uint8_t)stencil error:(NSError **)error;
- (BOOL)draw:(PtR2Program *)program target:(PtR2Target *)target
    vertices:(const void *)vertices count:(uint32_t)count
    indices:(const uint32_t *)indices count:(uint32_t)indexCount
    vertexConstants:(const float *)vertexConstants fragmentConstants:(const float *)fragmentConstants
    textures:(NSArray<PtR2Texture *> *)textures linear:(BOOL)linear raster:(PtR2Raster)raster error:(NSError **)error;
/* Explicit RT -> sampleable snapshot and RT -> CPU export. Never wait in encode. */
- (PtR2Texture *)snapshot:(PtR2Target *)target resolveControl:(uint32_t)control error:(NSError **)error;
- (PtR2Frame *)frame:(PtR2Texture *)completedSnapshot error:(NSError **)error;
- (PtR2Readback *)readback:(PtR2Target *)target resolveControl:(uint32_t)control error:(NSError **)error;
/* Copy only after its own successful completion; pending/stale/failed returns nil. Tight RGBA8 rows. */
- (NSData *)copyReadback:(PtR2Readback *)readback error:(NSError **)error;
/* One commit for many jobs. Admission/backpressure returns an error, never waits for the GPU.
 * Callers flush at real guest visibility/fence or bounded batching boundaries. */
- (PtR2Ticket)commit:(NSError **)error;
/* One CPU completion consumer may poll independently of the encoding owner.
 * This only removes immutable records under the runtime mutex; no Metal call.
 * OPTICS_RENDER_OWNER publishes coalesced contiguous completion prefixes. */
- (BOOL)poll:(PtR2Completion *)completion;
/* Called from arbitrary completion/lease-release threads: ONLY post/coalesce
 * an owner event. Never enter this renderer or guest RAM inline. Use a weak
 * owner or event source, not a strong reference cycle back to the renderer. */
- (BOOL)setCompletionWake:(dispatch_block_t)wake error:(NSError **)error;
/* Reset drops uncommitted work and invalidates objects/tickets. In-flight leases survive completion. */
- (BOOL)reset:(NSError **)error;
- (NSDictionary *)diagnostics;
/* Samples that passed depth and stencil in the draws with raster.query == query, summed over their render
 * passes; valid once the batches holding them completed. The query is forgotten (0 when it had no draws). */
- (uint64_t)queryCount:(uint32_t)query;
/* Last failed Metal compilation, including generated source and full key; empty when none. */
- (NSDictionary *)compilerFailure;
/* Encoding owner: shutdown waits for committed work and already-reserved wake callbacks.
 * A wake callback must only post, never synchronously call close. No hidden drain in draw/clear/reset. */
- (void)close;
@end
