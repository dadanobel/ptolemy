/* SPDX-License-Identifier: GPL-2.0-or-later */
#if !defined(__APPLE__) || !defined(__arm64__)
#error "Apple Silicon macOS only"
#endif
#import "surface_metal.h"
#include "resolve.h"
static BOOL fail(NSError **error,NSString *message)
{
    if(error)*error=[NSError errorWithDomain:@"Ptolemy.Radeon2" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
@implementation PtR2MetalSurface
+ (instancetype)surfaceWithDevice:(id<MTLDevice>)device width:(uint32_t)w
    height:(uint32_t)h input:(PtR2MsaaInput)input budget:(uint64_t)budget error:(NSError **)error
{
    if(!device || !w || !h || w>16384 || h>16384){fail(error,@"Invalid offscreen dimensions/device");return nil;}
    input.metal_sample_counts=0;
    for(unsigned n=1;n<=6;n++)if([device supportsTextureSampleCount:n])input.metal_sample_counts|=1u<<n;
    input.programmable_positions=device.areProgrammableSamplePositionsSupported;
    PtR2MsaaPlan p=pt_r2_msaa_plan(input);uint64_t estimate=0;
    if(p.issues || !pt_r2_msaa_payload_bytes(w,h,4,0,&p,p.count>1,&estimate) || estimate>budget) {
        fail(error,@"Unsupported exact sample pattern or insufficient surface budget");return nil;
    }
    PtR2MetalSurface *s=[PtR2MetalSurface new];s->_samples=p;
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    if(p.count>1){d.textureType=MTLTextureType2DMultisample;d.sampleCount=p.count;}
    s->_color=[device newTextureWithDescriptor:d];
    if(p.count>1) {
        d.textureType=MTLTextureType2D;d.sampleCount=1;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        s->_resolved=[device newTextureWithDescriptor:d];
    } else s->_resolved=s->_color;
    if(!s.color || !s.resolved){fail(error,@"Metal surface allocation failed");return nil;}
    s->_allocatedBytes=s.color.allocatedSize;
    if(p.count>1) {
        if(UINT64_MAX-s->_allocatedBytes<s.resolved.allocatedSize){fail(error,@"Surface accounting overflow");return nil;}
        s->_allocatedBytes+=s.resolved.allocatedSize;
    }
    if(s.allocatedBytes>budget){fail(error,@"Actual Metal allocation exceeds surface budget");return nil;}
    return s;
}
- (MTLRenderPassDescriptor *)renderPassWithLoad:(MTLLoadAction)load clear:(MTLClearColor)clear
{
    MTLRenderPassDescriptor *r=[MTLRenderPassDescriptor renderPassDescriptor];
    r.colorAttachments[0].texture=self.color;r.colorAttachments[0].loadAction=load;
    r.colorAttachments[0].storeAction=MTLStoreActionStore;r.colorAttachments[0].clearColor=clear;
    if(_samples.count>1) {
        MTLSamplePosition positions[6];
        for(unsigned k=0;k<_samples.count;k++)positions[k]=(MTLSamplePosition){_samples.position[k].metal_x/16.0f,_samples.position[k].metal_y/16.0f};
        [r setSamplePositions:positions count:_samples.count];
    }
    return r;
}
@end

/* Byte-domain filtering intentionally uses the same lookup quantization as
 * resolve.c. A standard Metal average or pow() per pixel is not equivalent. */
static NSString *const source=@"#include <metal_stdlib>\n"
"using namespace metal;\n"
"kernel void resolve_rgba(texture2d_ms<float, access::read> src [[texture(0)]],\n"
" texture2d<float, access::write> dst [[texture(1)]], constant uint &ctl [[buffer(0)]],\n"
" constant float *dg [[buffer(1)]], constant uchar *rg [[buffer(2)]], uint2 g [[thread_position_in_grid]]) {\n"
" if(g.x>=dst.get_width() || g.y>=dst.get_height())return;\n"
" uint n=src.get_num_samples(); uint4 first=uint4(rint(saturate(src.read(g,0))*255.0f)); uint4 result=first;\n"
" for(uint c=0;c<4;c++){\n"
"  if(!(ctl&1u) || (c==3 && !(ctl&4u)))continue;\n"
"  if(c!=3 && (ctl&2u)){float sum=0;for(uint k=0;k<n;k++){uint4 v=uint4(rint(saturate(src.read(g,k))*255.0f));sum+=dg[v[c]];}\n"
"   result[c]=rg[min(uint(rint(sum/float(n)*4095.0f)),4095u)];\n"
"  }else{uint sum=n/2;for(uint k=0;k<n;k++){uint4 v=uint4(rint(saturate(src.read(g,k))*255.0f));sum+=v[c];}result[c]=sum/n;}\n"
" }dst.write(float4(result)/255.0f,g);\n"
"}\n";
@implementation PtR2MetalResolver {
    id<MTLDevice> _device;id<MTLComputePipelineState> _pipeline;
    id<MTLBuffer> _degamma,_regamma;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device error:(NSError **)error
{
    if(!(self=[super init]))return nil;
    if(!device){fail(error,@"Missing Metal device");return nil;}_device=device;
    MTLCompileOptions *options=[MTLCompileOptions new];
    if(@available(macOS 15.0,*))options.mathMode=MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled=NO;
#pragma clang diagnostic pop
    }
    id<MTLLibrary> library=[device newLibraryWithSource:source options:options error:error];
    id<MTLFunction> function=[library newFunctionWithName:@"resolve_rgba"];
    if(!function || !(_pipeline=[device newComputePipelineStateWithFunction:function error:error]))return nil;
    PtR2Gamma tables;pt_r2_gamma_init(&tables);
    _degamma=[device newBufferWithBytes:tables.degamma length:sizeof tables.degamma options:MTLResourceStorageModeShared];
    _regamma=[device newBufferWithBytes:tables.regamma length:sizeof tables.regamma options:MTLResourceStorageModeShared];
    if(!_degamma || !_regamma){fail(error,@"Gamma table allocation failed");return nil;}
    return self;
}
- (BOOL)encodeSurface:(PtR2MetalSurface *)s commandBuffer:(id<MTLCommandBuffer>)command
    control:(uint32_t)control error:(NSError **)error
{
    if(!s || !command || s.color.device!=_device || command.commandQueue.device!=_device || !(control&1) || (control&~7u))return fail(error,@"Invalid resolve inputs/device/control");
    if(command.status!=MTLCommandBufferStatusNotEnqueued)return fail(error,@"Resolve requires an uncommitted command buffer");
    if(s.samples.count==1)return YES;
    id<MTLComputeCommandEncoder> e=[command computeCommandEncoder];
    if(!e)return fail(error,@"Could not create resolve encoder (end the render encoder first)");
    [e setComputePipelineState:_pipeline];[e setTexture:s.color atIndex:0];[e setTexture:s.resolved atIndex:1];
    [e setBytes:&control length:sizeof control atIndex:0];[e setBuffer:_degamma offset:0 atIndex:1];[e setBuffer:_regamma offset:0 atIndex:2];
    NSUInteger x=_pipeline.threadExecutionWidth,y=_pipeline.maxTotalThreadsPerThreadgroup/x;
    if(y>8)y=8;
    [e dispatchThreads:MTLSizeMake(s.color.width,s.color.height,1) threadsPerThreadgroup:MTLSizeMake(x,y,1)];[e endEncoding];return YES;
}
@end
