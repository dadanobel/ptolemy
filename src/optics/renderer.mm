/* SPDX-License-Identifier: GPL-2.0-or-later
 * Metal executor for the R300 model: render targets, depth storage, textures, programs and batches. */
#import "renderer.h"
#include "raw_vertex.h"
#import "foundation/surface_metal.h"
#import <os/signpost.h>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <pthread.h>
#include <time.h>
#include <vector>
#include <cmath>

namespace {
static NSString *const unpackSource=@"#include <metal_stdlib>\nusing namespace metal;\n"
"kernel void optics_unpack(device const uchar *src [[buffer(0)]],constant uint *p [[buffer(1)]],texture2d<float,access::write> dst [[texture(0)]],uint2 g [[thread_position_in_grid]]){\n"
"if(g.x>=p[0]||g.y>=p[1])return;ulong a=ulong(g.y)*p[2]+ulong(g.x)*p[3];uint f=p[4];float4 c=float4(0,0,0,1);\n"
/* DXT1/3/5 (13-15): p[2] a row of 4x4 blocks, p[3] the block; pixel.c's reference decodes the same way. */
"if(f>=13u){ulong b=ulong(g.y>>2)*p[2]+ulong(g.x>>2)*p[3],q=f==13u?b:b+8u;uint i=(g.y&3u)*4u+(g.x&3u);\n"
"uint c0=uint(src[q])|(uint(src[q+1])<<8),c1=uint(src[q+2])|(uint(src[q+3])<<8),s=(uint(src[q+4u+i/4u])>>(2u*(i&3u)))&3u;\n"
"float3 e0=float3(c0>>11,(c0>>5)&63u,c0&31u)/float3(31,63,31),e1=float3(c1>>11,(c1>>5)&63u,c1&31u)/float3(31,63,31);\n"
"c.rgb=s==0u?e0:s==1u?e1:(f!=13u||c0>c1)?(s==2u?(2.0f*e0+e1)/3.0f:(e0+2.0f*e1)/3.0f):s==2u?(e0+e1)*0.5f:float3(0);if(f==13u&&c0<=c1&&s==3u)c.a=0;\n"
"if(f==14u)c.a=float((uint(src[b+i/2u])>>(4u*(i&1u)))&15u)/15.0f;\n"
"if(f==15u){float a0=src[b],a1=src[b+1];uint t=3u*i,k=((uint(src[b+2u+t/8u])|(uint(src[b+3u+t/8u])<<8))>>(t&7u))&7u;\n"
"c.a=(k==0u?a0:k==1u?a1:a0>a1?(float(8u-k)*a0+float(k-1u)*a1)/7.0f:k==6u?0.0f:k==7u?255.0f:(float(6u-k)*a0+float(k-1u)*a1)/5.0f)/255.0f;}}\n"
"else if(f==0)c=float4(src[a],src[a+1],src[a+2],src[a+3])/255.0f;else if(f==7)c.r=float(src[a])/255.0f;\n"
"else if(f==8)c=float4(float(src[a])/255.0f,float(src[a+1])/255.0f,0,1);else if(f>=9){\n"
"ulong b=ulong(g.y)*p[2]+ulong(g.x&~1u)*2;uint uyvy=(f-9u)&1u,sw=f>=11u?128u:0u;float y=float(src[b+(uyvy?1u:0u)+(g.x&1u)*2u])-16.0f;\n"
"float u=float(as_type<char>(uchar(uint(src[b+(uyvy?0u:1u)])^sw))),v=float(as_type<char>(uchar(uint(src[b+(uyvy?2u:3u)])^sw)));\n"
"c=float4(clamp(float3(1.1643835616f*y+1.5960267857f*v,1.1643835616f*y-0.3917622901f*u-0.8129676472f*v,1.1643835616f*y+2.0172321429f*u),0.0f,255.0f)/255.0f,1); }else{\n"
"uint v=uint(src[a])|(uint(src[a+1])<<8);if((f&1u)==0u)v=(v>>8)|((v&255u)<<8);\n"
"if(f<=2)c=float4(float(v>>11)/31.0f,float((v>>5)&63u)/63.0f,float(v&31u)/31.0f,1);\n"
"else if(f<=4)c=float4(float((v>>10)&31u)/31.0f,float((v>>5)&31u)/31.0f,float(v&31u)/31.0f,float(v>>15));\n"
"else c=float4(float((v>>8)&15u),float((v>>4)&15u),float(v&15u),float(v>>12))/15.0f;}\n"
"float4 out;for(uint k=0;k<4;k++){uint s=(p[5]>>(k*3))&7u;out[k]=s<4?c[s]:s==4?0.0f:1.0f;}dst.write(out,g);}\n";
struct Runtime {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<PtR2Completion> completions;
    std::map<uint64_t,PtR2Completion> retired;
    uint64_t nextCompletion=1;
    uint64_t epoch=1,resident=0,peak=0;
    unsigned inflight=0,wakes=0;
    bool closed=false,failed=false;
    std::string error;
    uint64_t completed=0,callbackNS=0,maxCallbackNS=0,startNS=0,endNS=0;   /* startNS: submit -> GPU start, endNS: GPU end -> callback (host clock) */
    double gpuSeconds=0;
    dispatch_block_t __strong wake=nil;
    /* Upload buffers of retired batches, reused instead of a new MTLBuffer per batch (a kernel call and
     * fresh zero-filled pages each time: about 1400 per second in Xbench OpenGL). */
    std::vector<id<MTLBuffer>> spare;
};
/* The caller reserves under the same lock as retirement. close waits for both GPU and callback users. */
static dispatch_block_t reserveWake(Runtime &s) {
    if(s.closed||!s.wake)return nil;
    s.wakes++;return s.wake;
}
static void runWake(const std::shared_ptr<Runtime> &s,dispatch_block_t wake) {
    if(!wake)return;
    wake();
    {std::lock_guard<std::mutex> lock(s->mutex);s->wakes--;}
    s->changed.notify_all();
}
static bool blendValid(const PtR2Route &r) {
    uint32_t b=r.blendState;
    return !r.blend&&b>>31&&(b&15)<=14&&(b>>4&15)<=14&&(b>>8&7)<=4&&(b>>12&15)<=14&&(b>>16&15)<=14&&(b>>20&7)<=4;
}
static bool epilogueValid(const PtR2Route &r) {
    if(r.vte>2||r.selectVaryings>1||r.pad[0]||r.pad[1])return false;
    for(unsigned j=0;j<8;j++)for(unsigned c=0;c<4;c++)if(r.select[j][c]>5||(!r.selectVaryings&&r.select[j][c]))return false;
    return true;
}
static uint64_t nowNS() {timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
static BOOL fail(NSError **e,NSString *s,NSInteger code=1) {
    if(e)*e=[NSError errorWithDomain:@"Ptolemy.Radeon2.Prototype" code:code userInfo:@{NSLocalizedDescriptionKey:s}];return NO;
}
static void word(NSMutableData *data,uint32_t x) {uint8_t b[4]={(uint8_t)x,(uint8_t)(x>>8),(uint8_t)(x>>16),(uint8_t)(x>>24)};[data appendBytes:b length:4];}
static NSString *body(const PtR2ShaderIR *ir,const PtR2ShaderInterface *iface,NSError **e) {
    PtR2ShaderError error={};size_t needed=0;
    if(pt_r2_shader_msl_body(ir,iface,NULL,0,&needed,&error)!=PT_R2_SHADER_BUFFER||!needed) {fail(e,[NSString stringWithUTF8String:error.reason]);return nil;}
    std::vector<char> bytes(needed);
    if(pt_r2_shader_msl_body(ir,iface,bytes.data(),bytes.size(),NULL,&error)!=PT_R2_SHADER_OK) {fail(e,[NSString stringWithUTF8String:error.reason]);return nil;}
    return [NSString stringWithUTF8String:bytes.data()];
}
static uint32_t constantBytes(const PtR2ShaderIR *ir) {
    uint32_t bytes=16;
    auto use=[&](PtR2ShaderOperand op){if(op.bank==PT_R2_CONSTANT)bytes=std::max(bytes,op.relative?256u*16:(uint32_t(op.index)+1)*16);};
    for(unsigned i=0;i<ir->step_count;i++) {
        const PtR2ShaderStep &s=ir->step[i];
        if(s.kind==PT_R2_STEP_PVS)for(unsigned j=0;j<3;j++)use(s.u.pvs.arg[j]);
        if(s.kind==PT_R2_STEP_US)for(unsigned j=0;j<3;j++){use(s.u.us.rgb_source[j]);use(s.u.us.alpha_source[j]);use(s.u.us.rgb_arg[j]);use(s.u.us.alpha_arg[j]);}
    }
    return bytes;
}
static NSString *varyings() {
    NSMutableString *s=[NSMutableString stringWithString:@"struct PtVary {float4 position [[position]];\n"];
    for(unsigned i=0;i<8;i++)[s appendFormat:@"float4 v%u [[user(v%u), center_perspective]];\n",i,i];
    [s appendString:@"};\n"];return s;
}
/* Emit the existing VAP integer contract as native vertex arithmetic. All
 * selectors/formats are pipeline constants; guest bytes remain immutable. */
static void rawInput(NSMutableString *s,unsigned j,PtR2Attribute a,PtR2Route r) {
    if(a.format==OPTICS_RAW_DEFAULT){[s appendFormat:@"io.input[%u]=float4(0.0f,0.0f,0.0f,1.0f);\n",j];return;}
    unsigned bits=a.format-OPTICS_RAW_INT,kind=bits&3,mode=bits>>5;
    bool noZero=a.format>=OPTICS_RAW_NORM2;bool floating=a.format>=OPTICS_RAW_FLOAT&&a.format<OPTICS_RAW_DEFAULT;
    if(noZero){kind=(a.format-OPTICS_RAW_NORM2)&3;mode=(a.format-OPTICS_RAW_NORM2)>>2;bits=kind|4|8;}
    if(floating){kind=(a.format-OPTICS_RAW_FLOAT)&3;mode=(a.format-OPTICS_RAW_FLOAT)>>2;}
    [s appendFormat:@"float4 pt_raw%u=float4(0.0f,0.0f,0.0f,1.0f);\n",j];
    for(unsigned w=0;w<optics_raw_width(a.format,a.components)/4;w++)
        [s appendFormat:@"uint pt_w%u_%u=pt_load_word(v,ulong(id)*%uu+%uu,%uu);\n",j,w,r.stride,a.offset+4*w,mode];
    for(unsigned c=0;c<a.components;c++){
        if(floating){[s appendFormat:@"pt_raw%u.%c=as_type<float>(pt_w%u_%u);\n",j,"xyzw"[c],j,c];continue;}
        unsigned k=kind==1?(c<3?2-c:3):c,n=kind>=2?16:8,word=kind>=2?k/2:0,shift=n*(kind>=2?k&1:k);
        [s appendFormat:@"uint pt_u%u_%u=(pt_w%u_%u>>%uu)&%uu;float pt_f%u_%u=",j,c,j,word,shift,(1u<<n)-1,j,c];
        if(bits&4)[s appendFormat:@"pt_u%u_%u>=%uu?float(pt_u%u_%u)-%u.0f:float(pt_u%u_%u);\n",j,c,1u<<(n-1),j,c,1u<<n,j,c];
        else [s appendFormat:@"float(pt_u%u_%u);\n",j,c];
        [s appendFormat:@"pt_raw%u.%c=",j,"xyzw"[c]];
        if(noZero)[s appendFormat:@"(2.0f*pt_f%u_%u+1.0f)/%u.0f",j,c,(1u<<n)-1];
        else if(bits&8){if(bits&16)[s appendString:@"max("];[s appendFormat:@"pt_f%u_%u/%u.0f",j,c,(1u<<(n-((bits>>2)&1)))-1];if(bits&16)[s appendString:@",-1.0f)"];}
        else [s appendFormat:@"pt_f%u_%u",j,c];
        [s appendString:@";\n"];
    }
    [s appendFormat:@"io.input[%u]=float4(",j];uint16_t select=r.vapSelect[j];
    for(unsigned c=0;c<4;c++){
        if(c)[s appendString:@","];
        if(!(select>>(12+c)&1))[s appendString:c==3?@"1.0f":@"0.0f"];
        else {unsigned k=select>>(3*c)&7;if(k<4)[s appendFormat:@"pt_raw%u.%c",j,"xyzw"[k]];else [s appendString:k==5?@"1.0f":@"0.0f"];}
    }[s appendString:@");\n"];
}
static NSString *rawLoadSource(){return @"uint pt_load_word(device const uchar *v,ulong a,uint mode){uint x=uint(v[a])|uint(v[a+1])<<8|uint(v[a+2])<<16|uint(v[a+3])<<24;if(mode==1u)return ((x>>8)&0x00ff00ffu)|((x<<8)&0xff00ff00u);if(mode==2u)return x>>24|((x>>8)&0xff00u)|((x<<8)&0xff0000u)|x<<24;if(mode==3u)return x>>16|x<<16;return x;}\n";}
static NSString *vertexSource(NSString *source,const PtR2ShaderInterface *vi,PtR2Route r) {
    NSMutableString *s=[NSMutableString stringWithString:@"#define PT_BODY_ARGS uint pt_unused\n"];
    [s appendString:source];[s appendString:varyings()];
    bool raw=false;for(unsigned j=0;j<vi->input_count;j++)raw|=r.attribute[j].format>=OPTICS_RAW_INT;
    if(raw)[s appendString:rawLoadSource()];
    [s appendString:@"float pt_load_f32(device const uchar *v,ulong a,uint be){uint b=be?(uint(v[a])<<24|uint(v[a+1])<<16|uint(v[a+2])<<8|uint(v[a+3])):(uint(v[a])|uint(v[a+1])<<8|uint(v[a+2])<<16|uint(v[a+3])<<24);return as_type<float>(b);}\nvertex PtVary pt_vertex(uint id [[vertex_id]],device const uchar *v [[buffer(0)]],constant float4 *k [[buffer(1)]],constant float4 *optics_vte [[buffer(2)]]){PtIO io;\n"];
    for(unsigned j=0;j<vi->input_count;j++) {
        PtR2Attribute a=r.attribute[j];if(a.format>=OPTICS_RAW_INT){rawInput(s,j,a,r);continue;}[s appendFormat:@"io.input[%u]=float4(",j];
        for(unsigned c=0;c<4;c++) {
            if(c)[s appendString:@","];
            if(c>=a.components)[s appendString:c==3?@"1.0f":@"0.0f"];
            else if(a.format==2)[s appendFormat:@"float(v[ulong(id)*%uu+%uu])/255.0f",r.stride,a.offset+c];
            else [s appendFormat:@"pt_load_f32(v,ulong(id)*%uu+%uu,%uu)",r.stride,a.offset+c*4,a.format];
        }
        [s appendString:@");\n"];
    }
    if(r.vte==2){
        [s appendFormat:@"pt_shader_body(io,k,0u);PtVary o;float4 pt_pos=io.output[%u];uint pt_fmt=uint(optics_vte[2].x);float3 pt_win=pt_pos.xyz;\n",r.positionOutput];
        [s appendString:@"if((pt_fmt&256u)==0u){pt_win.x=pt_div(pt_win.x,pt_pos.w);pt_win.y=pt_div(pt_win.y,pt_pos.w);}if((pt_fmt&512u)==0u)pt_win.z=pt_div(pt_win.z,pt_pos.w);\n"];
        [s appendString:@"if(pt_fmt&1u)pt_win.x*=optics_vte[0].x;if(pt_fmt&2u)pt_win.x+=optics_vte[0].y;if(pt_fmt&4u)pt_win.y*=optics_vte[0].z;if(pt_fmt&8u)pt_win.y+=optics_vte[0].w;if(pt_fmt&16u)pt_win.z*=optics_vte[1].x;if(pt_fmt&32u)pt_win.z+=optics_vte[1].y;\n"];
        [s appendString:@"float pt_cw=(pt_fmt&256u)?1.0f:pt_pos.w;float pt_z=(pt_fmt&1024u)?pt_win.z:0.5f;o.position=float4((pt_div(2.0f*pt_win.x,optics_vte[1].z)-1.0f)*pt_cw,(1.0f-pt_div(2.0f*pt_win.y,optics_vte[1].w))*pt_cw,pt_z*pt_cw,pt_cw);\n"];
    }
    else if(r.vte)[s appendFormat:@"pt_shader_body(io,k,0u);PtVary o;float4 pt_pos=io.output[%u];o.position=float4(pt_pos.x*optics_vte[0].x+pt_pos.w*optics_vte[0].y,pt_pos.y*optics_vte[0].z+pt_pos.w*optics_vte[0].w,pt_pos.z*optics_vte[1].x+pt_pos.w*optics_vte[1].y,pt_pos.w);\n",r.positionOutput];
    else [s appendFormat:@"pt_shader_body(io,k,0u);PtVary o;o.position=io.output[%u];o.position.z=(o.position.z+o.position.w)*0.5f;\n",r.positionOutput];
    for(unsigned j=0;j<8;j++) {
        if(j<r.varyingCount && r.selectVaryings) {
            [s appendFormat:@"o.v%u=float4(",j];
            for(unsigned c=0;c<4;c++){uint8_t x=r.select[j][c];if(x<4)[s appendFormat:@"%@io.output[%u].%c",c?@",":@"",r.pvsOutput[j],"xyzw"[x]];else [s appendFormat:@"%@%@",c?@",":@"",x==5?@"1.0f":@"0.0f"];}
            [s appendString:@");\n"];
        }
        else if(j<r.varyingCount)[s appendFormat:@"o.v%u=io.output[%u];\n",j,r.pvsOutput[j]];
        else [s appendFormat:@"o.v%u=float4(0.0f);\n",j];
    }
    [s appendString:@"return o;}\n"];return s;
}
static NSString *fragmentSource(NSString *source,const PtR2ShaderIR *us,const PtR2ShaderInterface *fi,PtR2Route r) {
    NSMutableString *s=[NSMutableString stringWithString:@"#define PT_BODY_ARGS uint pt_unused\n#define PT_TEX(unit,tx,sp,c,op) pt_sample(tx,sp,c,op)\n#include <metal_stdlib>\nusing namespace metal;\nfloat4 pt_sample(texture2d<float>,sampler,float4,uint);\nfloat4 pt_sample(texturecube<float>,sampler,float4,uint);\n"];
    [s appendString:source];[s appendString:varyings()];
    [s appendString:@"float4 pt_sample(texture2d<float> t,sampler s,float4 c,uint op){if(op==3u)c.xyz=pt_div(c.xyz,float3(c.w));if(op==4u)return t.sample(s,c.xy,bias(c.w));return t.sample(s,c.xy);}\n"
        "float4 pt_sample(texturecube<float> t,sampler s,float4 c,uint op){if(op==3u)c.xyz=pt_div(c.xyz,float3(c.w));if(op==4u)return t.sample(s,c.xyz,bias(c.w));return t.sample(s,c.xyz);}\nstruct PtOut {float4 color [[color(0)]];"];
    if(us->writes_depth)[s appendString:@"float depth [[depth(any)]];"];
    [s appendString:@"};\nstruct PtAlpha {uint enabled;uint compare;float reference;};\nbool pt_compare(uint f,float a,float b){switch(f){case 0:return false;case 1:return a<b;case 2:return a==b;case 3:return a<=b;case 4:return a>b;case 5:return a!=b;case 6:return a>=b;default:return true;}}\nfragment PtOut pt_fragment(PtVary v [[stage_in]],constant float4 *k [[buffer(0)]],constant PtAlpha &alpha [[buffer(1)]]"];
    for(unsigned j=0;j<16;j++)if(us->textures&(1u<<j))[s appendFormat:@",texture%@<float> tx%u [[texture(%u)]],sampler sp%u [[sampler(%u)]]",fi->texture_dimension[j]==2?@"cube":@"2d",j,j,j,j];
    [s appendString:@"){PtIO io;\n"];
    for(unsigned j=0;j<32;j++)[s appendFormat:@"io.input[%u]=float4(0.0f);\n",j];
    for(unsigned j=0;j<r.varyingCount;j++)[s appendFormat:@"io.input[%u]=v.v%u;\n",r.usInput[j],j];
    [s appendString:@"pt_shader_body(io,k,0u"];
    for(unsigned j=0;j<16;j++)if(us->textures&(1u<<j))[s appendFormat:@",tx%u,sp%u",j,j];
    [s appendFormat:@");if(io.killed)discard_fragment();PtOut o;o.color=io.output[%u];if(alpha.enabled&&!pt_compare(alpha.compare,saturate(o.color.a),alpha.reference))discard_fragment();",r.colorOutput];
    if(us->writes_depth)[s appendString:@"o.depth=io.depth;"];
    [s appendString:@"return o;}\n"];return s;
}
}

PtR2Limits pt_r2_default_limits(id<MTLDevice> device) {
    if(!device||!device.hasUnifiedMemory)return {};
    uint64_t working=device.recommendedMaxWorkingSetSize;
    if(!working)working=NSProcessInfo.processInfo.physicalMemory/2;
    uint64_t ceiling=NSProcessInfo.processInfo.physicalMemory>=(UINT64_C(16)<<30)?UINT64_C(1)<<30:UINT64_C(512)<<20;
    uint64_t resident=std::min(ceiling,std::max(UINT64_C(64)<<20,working/8));
    return {2,4096,resident,std::min(UINT64_C(64)<<20,resident/8)};
}

@interface R2Lease : NSObject {
@public std::shared_ptr<Runtime> runtime;
    uint64_t epoch,charged;
    R2Lease *parent; /* Texture views retain the charged storage lease. */
    id<MTLBuffer> buffer;
    id<MTLTexture> texture;
    BOOL recycle;   /* a standard upload buffer: back to Runtime.spare when the last holder (its batch) lets go */
}
@end
@implementation R2Lease
- (void)dealloc {
    if(runtime&&charged) {
        dispatch_block_t wake=nil;
        {std::lock_guard<std::mutex> lock(runtime->mutex);runtime->resident-=charged;wake=reserveWake(*runtime);
            if(recycle&&buffer&&!runtime->closed&&runtime->spare.size()<8)runtime->spare.push_back(buffer);}
        runWake(runtime,wake); /* A UI frame lease can free capacity after the last GPU batch. */
    }
}
@end
struct R2Slice {R2Lease *__strong lease;size_t offset;};
@interface PtR2Texture () {@public R2Lease *lease;std::atomic<int> status;PtR2Ticket ticket;} @end
@implementation PtR2Texture @end
@interface PtR2Frame () {@public PtR2Texture *source;} @end
@implementation PtR2Frame
- (id<MTLTexture>)texture {return source->lease->texture;}
- (PtR2Ticket)ticket {return source->ticket;}
@end
@interface PtR2Depth () {@public R2Lease *lease;BOOL initialized;} @end
@implementation PtR2Depth
- (uint32_t)width {return (uint32_t)lease->texture.width;}
- (uint32_t)height {return (uint32_t)lease->texture.height;}
@end
@interface PtR2Target () {
@public R2Lease *lease;PtR2MetalSurface *surface;id<MTLTexture> depth;BOOL initialized;
    PtR2Depth *bound; /* independent depth/stencil for the following passes, nil = depth */
}
@end
@implementation PtR2Target
- (uint32_t)width {return (uint32_t)surface.color.width;}
- (uint32_t)height {return (uint32_t)surface.color.height;}
@end
@interface PtR2Program () {
@public std::shared_ptr<Runtime> runtime;uint64_t epoch;PtR2Route route;uint32_t textureMask;
    uint32_t vertexConstantBytes,fragmentConstantBytes;
    id<MTLRenderPipelineState> pipeline;
}
@property(nonatomic,readwrite,copy) NSString *vertexMSL;
@property(nonatomic,readwrite,copy) NSString *fragmentMSL;
@end
@implementation PtR2Program @end
@interface PtR2Readback () {
@public R2Lease *lease;std::atomic<int> status;size_t rowBytes;PtR2Ticket ticket;
    uint32_t width,height;size_t stencilRow;BOOL depthPlanes;
}
@end
@implementation PtR2Readback
- (PtR2Ticket)ticket {return ticket;}
- (uint32_t)width {return width;}
- (uint32_t)height {return height;}
@end
@interface R2BatchHold : NSObject {
@public NSArray *leases;NSArray<PtR2Readback *> *readbacks;NSArray<PtR2Texture *> *exports;
}
@end
@implementation R2BatchHold @end

@implementation PtR2Renderer {
    std::shared_ptr<Runtime> _runtime;
    id<MTLDevice> _device;id<MTLCommandQueue> _queue;
    PtR2MetalResolver *_resolver;
    id<MTLComputePipelineState> _unpack;
    id<MTLSamplerState> _sampler[2];id<MTLDepthStencilState> _depth[8][2];
    NSMutableDictionary<NSNumber *,id<MTLDepthStencilState>> *_stencilStates;
    std::unordered_map<uint32_t,id<MTLSamplerState>> _samplerStates;   /* by PtR2Raster sampler code, created on first use */
    id<MTLCommandBuffer> _batch;id<MTLRenderCommandEncoder> _encoder;PtR2Target *_activeTarget;
    /* Encoder-local state only. A new pass must bind everything again. */
    BOOL _cacheBindings,_bindingsValid;
    BOOL _completionPrefix;
    PtR2Program *_boundProgram;id<MTLDepthStencilState> _boundDepth;
    id<MTLTexture> _boundTexture[16];id<MTLSamplerState> _boundSampler[16];
    MTLViewport _boundViewport;MTLScissorRect _boundScissor;
    uint8_t _boundCull,_boundWinding,_boundStencil;
    float _boundEpilogue[12];uint8_t _boundAlpha[12];float _boundBlendColor[4],_boundBias[2];
    uint64_t _stateCalls,_stateSkipped;
    NSMutableArray *_leases;NSMutableArray<PtR2Readback *> *_readbacks;
    NSMutableArray<PtR2Texture *> *_exports;
    NSMutableDictionary<NSData *,PtR2Program *> *_programs;
    NSMutableDictionary<NSString *,id<MTLLibrary>> *_libraries;uint64_t _libraryCompiles,_libraryNS;   /* MSL compiled once per source */
    /* Occlusion queries: every render pass writes Metal visibility results into _visibility (8-byte slots, a ring); a
     * query gets a fresh slot in each pass it draws in, so no pass resets or adds to another's count. */
    id<MTLBuffer> _visibility;uint32_t _visibilityNext,_passQuery,_passSlot;int64_t _boundVisibility;
    std::unordered_map<uint32_t,std::vector<uint32_t>> _querySlots;
    NSDictionary *_compilerFailure;
    PtR2Limits _limits;pthread_t _owner;PtR2Ticket _ticket;uint64_t _sequence,_batchBytes;
    uint64_t _jobs,_draws,_commits,_busy,_compiled,_passes,_compileNS;
    uint64_t _passBreak[12];BOOL _passReasons;
    R2Lease *_upload;size_t _uploadUsed;uint64_t _bufferAllocations,_snapshotBytes;
    os_log_t _log;
    dispatch_queue_t _ownerQueue;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device limits:(PtR2Limits)limits error:(NSError **)error {
    return [self initWithDevice:device limits:limits ownerQueue:nil error:error];
}
- (instancetype)initWithDevice:(id<MTLDevice>)device limits:(PtR2Limits)limits ownerQueue:(dispatch_queue_t)queue error:(NSError **)error {
    if(!(self=[super init]))return nil;
    if(!device||!device.hasUnifiedMemory||!limits.maxBatches||limits.maxBatches>16||!limits.maxJobs||limits.maxJobs>65536||!limits.batchBytes||limits.residentBytes<limits.batchBytes) {
        fail(error,@"Invalid Apple Silicon device or explicit resource limits");return nil;
    }
    const char *reasons=getenv("OPTICS_PASS_REASONS");_passReasons=reasons&&atoi(reasons)!=0;
    _owner=pthread_self();_limits=limits;_runtime=std::make_shared<Runtime>();_device=device;
    const char *cache=getenv("OPTICS_BIND_CACHE");_cacheBindings=!cache||atoi(cache)!=0;
    const char *split=getenv("OPTICS_RENDER_OWNER");_completionPrefix=split&&atoi(split)!=0;
    _queue=[device newCommandQueueWithMaxCommandBufferCount:limits.maxBatches];
    _resolver=[[PtR2MetalResolver alloc]initWithDevice:device error:error];
    if(!_queue||!_resolver)return nil;
    MTLCompileOptions *unpackOptions=[MTLCompileOptions new];unpackOptions.mathMode=MTLMathModeSafe;
    id<MTLLibrary> unpack=[device newLibraryWithSource:unpackSource options:unpackOptions error:error];
    id<MTLFunction> function=[unpack newFunctionWithName:@"optics_unpack"];
    if(!function||!(_unpack=[device newComputePipelineStateWithFunction:function error:error]))return nil;
    _visibility=[device newBufferWithLength:8*8192 options:MTLResourceStorageModeShared];_boundVisibility=-1;if(!_visibility){fail(error,@"Visibility buffer allocation failed");return nil;}
    _programs=[NSMutableDictionary new];_libraries=[NSMutableDictionary new];_stencilStates=[NSMutableDictionary new];_log=os_log_create("org.ptolemy.optics","execution");
    for(unsigned i=0;i<2;i++) {
        MTLSamplerDescriptor *d=[MTLSamplerDescriptor new];d.minFilter=d.magFilter=i?MTLSamplerMinMagFilterLinear:MTLSamplerMinMagFilterNearest;
        d.sAddressMode=d.tAddressMode=MTLSamplerAddressModeClampToEdge;_sampler[i]=[device newSamplerStateWithDescriptor:d];
        if(!_sampler[i]){fail(error,@"Sampler allocation failed");return nil;}
    }
    for(unsigned c=0;c<8;c++)for(unsigned w=0;w<2;w++) {
        MTLDepthStencilDescriptor *d=[MTLDepthStencilDescriptor new];d.depthCompareFunction=(MTLCompareFunction)c;d.depthWriteEnabled=w;
        _depth[c][w]=[device newDepthStencilStateWithDescriptor:d];if(!_depth[c][w]){fail(error,@"Depth state allocation failed");return nil;}
    }
    if(queue) {
        void *tag=(__bridge void *)self;dispatch_queue_set_specific(queue,tag,tag,NULL);
        if(dispatch_get_specific(tag)!=tag){dispatch_queue_set_specific(queue,tag,NULL,NULL);fail(error,@"Create renderer on its supplied serial owner queue");return nil;}
        _ownerQueue=queue;
    }
    return self;
}
- (void)adoptOwner {if(!_ownerQueue)_owner=pthread_self();}
- (BOOL)isOwner {return _ownerQueue?dispatch_get_specific((__bridge void *)self)==(__bridge void *)self:pthread_equal(_owner,pthread_self());}
- (BOOL)owner:(NSError **)error {
    if(![self isOwner])return fail(error,@"Renderer requires its encoding owner");
    std::lock_guard<std::mutex> lock(_runtime->mutex);
    if(_runtime->closed)return fail(error,@"Renderer closed");
    if(_runtime->failed)return fail(error,@"GPU command failed; inspect completion, then reset");return YES;
}
- (R2Lease *)charge:(uint64_t)bytes error:(NSError **)error {
    auto s=_runtime;R2Lease *lease=[R2Lease new];lease->runtime=s;
    {std::lock_guard<std::mutex> lock(s->mutex);
        if(bytes>_limits.residentBytes){fail(error,@"One resource exceeds the resident budget");return nil;}
        if(bytes>_limits.residentBytes-s->resident){fail(error,@"Resident resource budget reached",2);return nil;}
        lease->epoch=s->epoch;lease->charged=bytes;s->resident+=bytes;if(s->resident>s->peak)s->peak=s->resident;
    }return lease;
}
- (BOOL)valid:(R2Lease *)lease error:(NSError **)error {
    if(!lease||lease->runtime!=_runtime||lease->epoch!=_runtime->epoch)return fail(error,@"Foreign or stale resource epoch");return YES;
}
- (void)endRender:(unsigned)reason {
    if(_encoder){if(_passReasons&&reason<12)_passBreak[reason]++;[_encoder endEncoding];_encoder=nil;_activeTarget=nil;}
    _passQuery=0;_boundVisibility=-1;   /* a new encoder starts with visibility results disabled */
    _bindingsValid=NO;_boundProgram=nil;_boundDepth=nil;
    for(unsigned j=0;j<16;j++){_boundTexture[j]=nil;_boundSampler[j]=nil;}
}
- (BOOL)admit:(uint64_t)bytes error:(NSError **)error {
    if(![self owner:error])return NO;
    if(bytes>_limits.batchBytes)return fail(error,@"One job exceeds the batch byte budget");
    if(_jobs>=_limits.maxJobs||bytes>_limits.batchBytes-_batchBytes){_busy++;return fail(error,@"Batch budget reached: commit, then retry",2);}
    if(!_batch) {
        {std::lock_guard<std::mutex> lock(_runtime->mutex);
            size_t retired=_completionPrefix?_runtime->retired.size():_runtime->completions.size();
            if(_runtime->inflight+retired>=_limits.maxBatches){_busy++;return fail(error,@"Completion capacity reached: poll or retry later",2);}}
        _batch=[_queue commandBuffer];if(!_batch)return fail(error,@"Command buffer allocation failed");
        _ticket={_runtime->epoch,++_sequence};_batch.label=[NSString stringWithFormat:@"R2 epoch %llu batch %llu",_ticket.epoch,_ticket.sequence];
        _leases=[NSMutableArray new];_readbacks=[NSMutableArray new];_exports=[NSMutableArray new];
    }
    return YES;
}
- (R2Lease *)buffer:(const void *)data bytes:(size_t)length error:(NSError **)error {
    R2Lease *lease=[self charge:length error:error];if(!lease)return nil;
    lease->buffer=data?[_device newBufferWithBytes:data length:length options:MTLResourceStorageModeShared|MTLResourceHazardTrackingModeTracked]
        :[_device newBufferWithLength:length options:MTLResourceStorageModeShared|MTLResourceHazardTrackingModeTracked];
    if(!lease->buffer){fail(error,@"Buffer allocation failed");return nil;}_bufferAllocations++;return lease;
}
- (R2Slice)upload:(const void *)data bytes:(size_t)n error:(NSError **)error {
    size_t aligned=(_uploadUsed+255)&~size_t(255);
    if(!_upload||aligned>_upload->buffer.length||n>_upload->buffer.length-aligned) {
        size_t standard=(size_t)std::min(_limits.batchBytes,UINT64_C(1)<<20),capacity=std::max(n,standard);
        id<MTLBuffer> reuse=nil;
        if(capacity==standard){std::lock_guard<std::mutex> lock(_runtime->mutex);if(!_runtime->spare.empty()){reuse=_runtime->spare.back();_runtime->spare.pop_back();}}
        if(reuse){_upload=[self charge:capacity error:error];if(_upload)_upload->buffer=reuse;}
        else _upload=[self buffer:NULL bytes:capacity error:error];
        _uploadUsed=0;aligned=0;
        if(!_upload)return {};
        _upload->recycle=capacity==standard;
        [_leases addObject:_upload];
    }
    memcpy((uint8_t *)_upload->buffer.contents+aligned,data,n);_uploadUsed=aligned+n;_snapshotBytes+=n;
    return {_upload,aligned};
}
- (PtR2Program *)preparePVS:(const PtR2ShaderIR *)pvs vertexInterface:(const PtR2ShaderInterface *)vi
    fragment:(const PtR2ShaderIR *)us interface:(const PtR2ShaderInterface *)fi route:(PtR2Route)r error:(NSError **)error {
    if(![self owner:error])return nil;
    if(!pvs||!us||!vi||!fi||pvs->stage!=PT_R2_SHADER_PVS||us->stage!=PT_R2_SHADER_US||pvs->textures||vi->input_count>16||
       r.vte>2||r.varyingCount>8||r.positionOutput>=vi->output_count||r.colorOutput>=fi->output_count||r.colorOutput>3||r.colorMask>15||r.blend>3||(r.blendState&&!blendValid(r))||!epilogueValid(r)||
       (us->outputs&~(1u<<r.colorOutput))||!(pvs->outputs&(1u<<r.positionOutput))||
       (r.sampleCount!=1&&r.sampleCount!=2&&r.sampleCount!=4)||![_device supportsTextureSampleCount:r.sampleCount]) {
        fail(error,@"Unsupported normalized stage, route, blend or sample count");return nil;
    }
    uint32_t seen=0;for(unsigned j=0;j<r.varyingCount;j++) {
        if(r.pvsOutput[j]>=vi->output_count||r.usInput[j]>=fi->input_count||(seen&(1u<<r.usInput[j]))) {fail(error,@"Invalid or duplicate varying route");return nil;}
        seen|=1u<<r.usInput[j];
    }
    if(!r.stride) {
        r.stride=sizeof(PtR2Vertex);
        for(unsigned j=0;j<vi->input_count;j++)r.attribute[j]={(uint16_t)(j*16),4,0};
    }
    for(unsigned j=0;j<vi->input_count;j++) {
        PtR2Attribute a=r.attribute[j];unsigned size=optics_raw_width(a.format,a.components);
        if(size==UINT_MAX||a.offset>r.stride||size>r.stride-a.offset||
           (a.format<OPTICS_RAW_INT&&r.vapSelect[j])||(a.format==OPTICS_RAW_DEFAULT&&(a.offset||r.vapSelect[j]))){fail(error,@"Unsupported or out-of-stride vertex attribute");return nil;}
    }
    for(unsigned j=0;j<16;j++)if((us->textures&(1u<<j))&&fi->texture_dimension[j]==1){fail(error,@"3D textures are not supported");return nil;}
    NSMutableData *key=[NSMutableData new];word(key,1);
    for(unsigned stage=0;stage<2;stage++) {
        uint8_t bytes[16384];size_t n=0;PtR2ShaderError e={};
        if(pt_r2_shader_key(stage?us:pvs,stage?fi:vi,bytes,sizeof bytes,&n,&e)!=PT_R2_SHADER_OK){fail(error,[NSString stringWithUTF8String:e.reason]);return nil;}
        word(key,(uint32_t)n);[key appendBytes:bytes length:n];
    }
    for(uint32_t v:{r.positionOutput,r.colorOutput,r.varyingCount,r.sampleCount,r.colorMask,r.blend})word(key,v);
    word(key,r.blendState);word(key,r.vte);word(key,r.selectVaryings);for(unsigned j=0;j<8;j++)for(unsigned c=0;c<4;c++)word(key,r.select[j][c]);
    for(unsigned j=0;j<r.varyingCount;j++){word(key,r.pvsOutput[j]);word(key,r.usInput[j]);}
    word(key,r.stride);for(unsigned j=0;j<vi->input_count;j++){word(key,r.attribute[j].offset);word(key,r.attribute[j].components);word(key,r.attribute[j].format);word(key,r.vapSelect[j]);}
    PtR2Program *cached=_programs[key];if(cached)return cached;
    /* Explicit bounded cache: it only deduplicates, a program in use stays alive in its draws' leases and the frontend's
     * table (a cache that refused new programs once full lost more and more geometry in long sessions). Dropped whole;
     * an LRU if recompiles show up in long sessions. */
    if(_programs.count>=512)[_programs removeAllObjects];
    uint64_t begin=nowNS();os_signpost_id_t sid=os_signpost_id_generate(_log);os_signpost_interval_begin(_log,sid,"R2Compile");
    NSString *vbody=body(pvs,vi,error),*fbody=body(us,fi,error);if(!vbody||!fbody){os_signpost_interval_end(_log,sid,"R2Compile");return nil;}
    PtR2Program *p=[PtR2Program new];p->runtime=_runtime;p->epoch=_runtime->epoch;p->route=r;p->textureMask=us->textures;
    p->vertexConstantBytes=constantBytes(pvs);p->fragmentConstantBytes=constantBytes(us);
    p.vertexMSL=vertexSource(vbody,vi,r);p.fragmentMSL=fragmentSource(fbody,us,fi,r);
    MTLCompileOptions *options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;
    /* Programs that differ only in blend, write mask or other pipeline state have the same vertex or fragment MSL
     * (Halo compiled 888 programs in 38 s before this); each source is compiled once (a bounded table,
     * dropped whole like the program cache). */
    auto library=[&](NSString *source)->id<MTLLibrary> {
        id<MTLLibrary> l=_libraries[source];if(l)return l;
        uint64_t t=nowNS();l=[_device newLibraryWithSource:source options:options error:error];_libraryNS+=nowNS()-t;_libraryCompiles++;
        if(l){if(_libraries.count>=1024)[_libraries removeAllObjects];_libraries[source]=l;}
        return l;
    };
    id<MTLLibrary> vl=library(p.vertexMSL);
    id<MTLLibrary> fl=vl?library(p.fragmentMSL):nil;
    if(fl) {
        MTLRenderPipelineDescriptor *d=[MTLRenderPipelineDescriptor new];d.label=@"Radeon2 direct US/PVS";
        d.vertexFunction=[vl newFunctionWithName:@"pt_vertex"];d.fragmentFunction=[fl newFunctionWithName:@"pt_fragment"];
        d.rasterSampleCount=r.sampleCount;d.depthAttachmentPixelFormat=d.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
        auto c=d.colorAttachments[0];c.pixelFormat=MTLPixelFormatRGBA8Unorm;
        c.writeMask=(MTLColorWriteMask)((r.colorMask&1?MTLColorWriteMaskRed:0)|(r.colorMask&2?MTLColorWriteMaskGreen:0)|(r.colorMask&4?MTLColorWriteMaskBlue:0)|(r.colorMask&8?MTLColorWriteMaskAlpha:0));
        c.blendingEnabled=r.blend!=0;c.sourceRGBBlendFactor=r.blend==1?MTLBlendFactorSourceAlpha:MTLBlendFactorOne;
        c.destinationRGBBlendFactor=r.blend==3?MTLBlendFactorOne:MTLBlendFactorOneMinusSourceAlpha;
        c.sourceAlphaBlendFactor=MTLBlendFactorOne;c.destinationAlphaBlendFactor=c.destinationRGBBlendFactor;
        if(r.blendState) {
            uint32_t b=r.blendState;c.blendingEnabled=YES;
            c.sourceRGBBlendFactor=(MTLBlendFactor)(b&15);c.destinationRGBBlendFactor=(MTLBlendFactor)(b>>4&15);c.rgbBlendOperation=(MTLBlendOperation)(b>>8&7);
            c.sourceAlphaBlendFactor=(MTLBlendFactor)(b>>12&15);c.destinationAlphaBlendFactor=(MTLBlendFactor)(b>>16&15);c.alphaBlendOperation=(MTLBlendOperation)(b>>20&7);
        }
        p->pipeline=[_device newRenderPipelineStateWithDescriptor:d error:error];
    }
    _compileNS+=nowNS()-begin;os_signpost_interval_end(_log,sid,"R2Compile");
    if(!p->pipeline) {
        _compilerFailure=@{@"epoch":@(_runtime->epoch),@"error":error&&*error?(*error).localizedDescription:@"Metal compilation failed",
            @"vertexMSL":p.vertexMSL,@"fragmentMSL":p.fragmentMSL,@"losslessKeyBase64":[key base64EncodedStringWithOptions:0]};
        return nil;
    }
    _compiled++;_programs[[key copy]]=p;return p;
}
- (PtR2Target *)targetWidth:(uint32_t)w height:(uint32_t)h samples:(PtR2MsaaInput)input error:(NSError **)error {
    if(![self owner:error])return nil;
    uint64_t available;{std::lock_guard<std::mutex> lock(_runtime->mutex);available=_limits.residentBytes-_runtime->resident;}
    PtR2MetalSurface *s=[PtR2MetalSurface surfaceWithDevice:_device width:w height:h input:input budget:available error:error];if(!s)return nil;
    if(s.samples.count!=1&&s.samples.count!=2&&s.samples.count!=4){fail(error,@"No pipeline support for this sample count");return nil;}
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageRenderTarget;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
    if(s.samples.count>1){d.textureType=MTLTextureType2DMultisample;d.sampleCount=s.samples.count;}
    id<MTLTexture> depth=[_device newTextureWithDescriptor:d];if(!depth){fail(error,@"Depth allocation failed");return nil;}
    if(UINT64_MAX-s.allocatedBytes<depth.allocatedSize){fail(error,@"Target accounting overflow");return nil;}
    R2Lease *lease=[self charge:s.allocatedBytes+depth.allocatedSize error:error];if(!lease)return nil;
    PtR2Target *target=[PtR2Target new];target->lease=lease;target->surface=s;target->depth=depth;return target;
}
- (PtR2Texture *)textureWidth:(uint32_t)w height:(uint32_t)h bytes:(const void *)bytes rowBytes:(size_t)row error:(NSError **)error {
    if(!bytes||!w||!h||w>16384||h>16384||row<(size_t)w*4||row>SIZE_MAX/h){fail(error,@"Invalid immutable RGBA8 upload");return nil;}
    uint64_t size=(uint64_t)w*h*4;if(![self admit:size error:error])return nil;
    /* Shared immutable textures avoid a synchronous staging submission. Private
     * upload batching is a later measured policy, not a guest-visible choice. */
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
    d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
    id<MTLTexture> tex=[_device newTextureWithDescriptor:d];if(!tex){fail(error,@"Texture allocation failed");return nil;}
    R2Lease *lease=[self charge:tex.allocatedSize error:error];if(!lease)return nil;
    [tex replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:bytes bytesPerRow:row];lease->texture=tex;
    PtR2Texture *result=[PtR2Texture new];result->lease=lease;result->status.store(1);_jobs++;_batchBytes+=size;return result;
}
- (PtR2Texture *)textureWidth:(uint32_t)w height:(uint32_t)h bytes:(const void *)bytes rowBytes:(size_t)row layout:(OpticsPixelLayout)layout error:(NSError **)error {
    return [self textureWidth:w height:h faces:1 faceBytes:0 bytes:bytes rowBytes:row layout:layout error:error];
}
- (PtR2Texture *)textureWidth:(uint32_t)w height:(uint32_t)h faces:(unsigned)faces faceBytes:(size_t)faceBytes bytes:(const void *)bytes rowBytes:(size_t)row layout:(OpticsPixelLayout)layout error:(NSError **)error {
    unsigned bpp=optics_pixel_bytes(layout);
    if(!bpp||!h||!w||row>SIZE_MAX/h){fail(error,@"Invalid linear pixel upload");return nil;}
    size_t one=(size_t)(h-1)*row+(size_t)w*bpp;
    if(faces==6&&faceBytes>(SIZE_MAX-one)/5){fail(error,@"Invalid cube map upload");return nil;}
    PtR2Levels l={1,{0},{row},{faceBytes}};
    return [self textureWidth:w height:h faces:faces levels:&l bytes:bytes length:(faces-1)*faceBytes+one layout:layout error:error];
}
- (PtR2Texture *)textureWidth:(uint32_t)w height:(uint32_t)h faces:(unsigned)faces levels:(const PtR2Levels *)lv bytes:(const void *)bytes length:(size_t)length layout:(OpticsPixelLayout)layout error:(NSError **)error {
    unsigned bpp=optics_pixel_bytes(layout),n=lv?lv->levels:0;
    if(!bytes||!w||!h||w>16384||h>16384||!bpp||(layout.format>=OPTICS_PIXEL_YUYV601_SIGNED&&layout.format<OPTICS_PIXEL_DXT1&&(w&1))||!n||n>13||(w>>(n-1))+(h>>(n-1))==0){fail(error,@"Invalid linear pixel upload");return nil;}
    if((faces!=1&&faces!=6)||(faces==6&&w!=h)){fail(error,@"Invalid cube map upload");return nil;}
    for(unsigned k=0;k<n;k++) {   /* every row of every face of every level inside length (DXT: rows of blocks) */
        size_t lw=OPTICS_PIXEL_UNITS(layout.format,std::max(1u,w>>k)),lh=OPTICS_PIXEL_UNITS(layout.format,std::max(1u,h>>k)),r=lv->row[k];
        if(r<lw*bpp||r>UINT32_MAX||r>SIZE_MAX/lh||(faces>1&&lv->face[k]<(lh-1)*r+lw*bpp)){fail(error,@"Invalid linear pixel upload");return nil;}
        size_t one=(lh-1)*r+lw*bpp,last=(faces-1)*lv->face[k];
        if(faces>1&&lv->face[k]>(SIZE_MAX-one)/(faces-1)){fail(error,@"Invalid cube map upload");return nil;}
        if(lv->offset[k]>length||last+one>length-lv->offset[k]){fail(error,@"Invalid linear pixel upload");return nil;}
    }
    if(![self admit:length error:error])return nil;
    R2Slice input=[self upload:bytes bytes:length error:error];if(!input.lease)return nil;
    MTLTextureDescriptor *d=faces==6?[MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm size:w mipmapped:n>1]
        :[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:n>1];
    d.mipmapLevelCount=n;
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
    id<MTLTexture> tex=[_device newTextureWithDescriptor:d];if(!tex){fail(error,@"Unpack texture allocation failed");return nil;}
    R2Lease *lease=[self charge:tex.allocatedSize error:error];if(!lease)return nil;lease->texture=tex;
    [self endRender:0];id<MTLComputeCommandEncoder> e=[_batch computeCommandEncoder];if(!e){fail(error,@"Unpack encoder failed");return nil;}
    [e setComputePipelineState:_unpack];
    NSUInteger x=_unpack.threadExecutionWidth,y=std::min((NSUInteger)8,_unpack.maxTotalThreadsPerThreadgroup/x);
    for(unsigned k=0;k<n;k++) {
        uint32_t lw=std::max(1u,w>>k),lh=std::max(1u,h>>k);
        uint32_t p[6]={lw,lh,(uint32_t)lv->row[k],bpp,layout.format,0};for(unsigned i=0;i<4;i++)p[5]|=(uint32_t)layout.component[i]<<(i*3);
        [e setBytes:p length:sizeof p atIndex:1];
        for(unsigned f=0;f<faces;f++) {   /* a level (and cube face) through a 2D view of it */
            id<MTLTexture> dst=faces==1&&n==1?tex:[tex newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(k,1) slices:NSMakeRange(f,1)];
            if(!dst){[e endEncoding];fail(error,@"Texture level view failed");return nil;}
            [e setBuffer:input.lease->buffer offset:input.offset+lv->offset[k]+f*lv->face[k] atIndex:0];[e setTexture:dst atIndex:0];
            [e dispatchThreads:MTLSizeMake(lw,lh,1) threadsPerThreadgroup:MTLSizeMake(x,y,1)];
        }
    }
    [e endEncoding];
    PtR2Texture *result=[PtR2Texture new];result->lease=lease;result->ticket=_ticket;result->status.store(0);
    [_leases addObject:result];[_exports addObject:result];_batchBytes+=length;_jobs++;return result;
}
- (BOOL)renderTarget:(PtR2Target *)t clearMask:(uint8_t)mask color:(MTLClearColor)color depth:(double)depth stencil:(uint8_t)stencil error:(NSError **)error {
    if(!t->initialized&&mask!=7)return fail(error,@"Unselected attachment planes require initialization/import");
    if(_encoder&&_activeTarget==t&&!mask)return YES;
    [self endRender:1];MTLRenderPassDescriptor *pass=[t->surface renderPassWithLoad:mask&1?MTLLoadActionClear:MTLLoadActionLoad clear:color];
    PtR2Depth *z=t->bound;BOOL fresh=z&&!z->initialized;   /* uninitialized independent storage starts at 1 / 0 */
    pass.depthAttachment.texture=z?z->lease->texture:t->depth;pass.depthAttachment.loadAction=mask&2||fresh?MTLLoadActionClear:MTLLoadActionLoad;
    pass.depthAttachment.storeAction=MTLStoreActionStore;pass.depthAttachment.clearDepth=mask&2?depth:1;
    pass.stencilAttachment.texture=pass.depthAttachment.texture;pass.stencilAttachment.loadAction=mask&4||fresh?MTLLoadActionClear:MTLLoadActionLoad;
    pass.stencilAttachment.storeAction=MTLStoreActionStore;pass.stencilAttachment.clearStencil=mask&4?stencil:0;
    pass.visibilityResultBuffer=_visibility;
    _encoder=[_batch renderCommandEncoderWithDescriptor:pass];if(!_encoder)return fail(error,@"Render encoder allocation failed");
    _activeTarget=t;[_leases addObject:t];if(z){[_leases addObject:z];z->initialized=YES;}_passes++;t->initialized=YES;return YES;
}
- (PtR2Depth *)depthWidth:(uint32_t)w height:(uint32_t)h error:(NSError **)error {
    if(![self owner:error])return nil;
    if(!w||!h||w>16384||h>16384){fail(error,@"Invalid depth storage size");return nil;}
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8 width:w height:h mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageRenderTarget;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
    id<MTLTexture> tex=[_device newTextureWithDescriptor:d];if(!tex){fail(error,@"Depth allocation failed");return nil;}
    R2Lease *lease=[self charge:tex.allocatedSize error:error];if(!lease)return nil;lease->texture=tex;
    PtR2Depth *z=[PtR2Depth new];z->lease=lease;return z;
}
- (BOOL)target:(PtR2Target *)t depth:(PtR2Depth *)z error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error]||(z&&![self valid:z->lease error:error]))return NO;
    if(z&&(z.width<t.width||z.height<t.height||t->surface.samples.count!=1))return fail(error,@"Depth storage must cover a single-sample target");
    if(t->bound==z)return YES;
    if(_activeTarget==t)[self endRender:2];
    t->bound=z;return YES;
}
- (BOOL)clearDepth:(PtR2Depth *)z mask:(uint8_t)mask depth:(double)v stencil:(uint8_t)s error:(NSError **)error {
    if(![self owner:error]||![self valid:z?z->lease:nil error:error])return NO;
    if(!(mask&6)||(mask&~6u)||!std::isfinite(v)||v<0||v>1)return fail(error,@"Invalid depth storage clear");
    if(![self admit:0 error:error])return NO;
    [self endRender:3];MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];BOOL fresh=!z->initialized;
    pass.depthAttachment.texture=z->lease->texture;pass.depthAttachment.loadAction=mask&2||fresh?MTLLoadActionClear:MTLLoadActionLoad;
    pass.depthAttachment.clearDepth=mask&2?v:1;pass.depthAttachment.storeAction=MTLStoreActionStore;
    pass.stencilAttachment.texture=z->lease->texture;pass.stencilAttachment.loadAction=mask&4||fresh?MTLLoadActionClear:MTLLoadActionLoad;
    pass.stencilAttachment.clearStencil=mask&4?s:0;pass.stencilAttachment.storeAction=MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e=[_batch renderCommandEncoderWithDescriptor:pass];if(!e)return fail(error,@"Depth clear encoder failed");
    [e endEncoding];z->initialized=YES;[_leases addObject:z];_jobs++;_passes++;return YES;
}
- (BOOL)copyDepth:(PtR2Depth *)src to:(PtR2Depth *)dst error:(NSError **)error {
    if(![self owner:error] || ![self valid:src?src->lease:nil error:error] || ![self valid:dst?dst->lease:nil error:error])return NO;
    if(src==dst || src.width>dst.width || src.height>dst.height)return fail(error,@"Invalid depth growth copy");
    if(![self clearDepth:dst mask:6 depth:1 stencil:0 error:error])return NO;
    if(!src->initialized)return YES;
    [self endRender:4];
    id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e)return fail(error,@"Depth growth blit encoder failed");
    [e copyFromTexture:src->lease->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(src.width,src.height,1) toTexture:dst->lease->texture
        destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    [e endEncoding];[_leases addObject:src];[_leases addObject:dst];_jobs++;return YES;
}
- (BOOL)loadDepth:(PtR2Depth *)z y:(uint32_t)y height:(uint32_t)h bytes:(const void *)bytes format:(unsigned)f error:(NSError **)error {
    if(![self owner:error]||![self valid:z?z->lease:nil error:error])return NO;
    unsigned bpp=optics_depth_bytes(f);
    if(!bpp||!bytes||!h||y>z.height||h>z.height-y)return fail(error,@"Invalid linear depth import");
    size_t row=(z.width*4u+255u)&~size_t(255),srow=(z.width+255u)&~size_t(255),n=(row+srow)*h;
    if(![self admit:n error:error])return NO;
    R2Lease *b=[self buffer:NULL bytes:n error:error];if(!b)return NO;
    uint8_t *dst=(uint8_t *)b->buffer.contents;
    for(unsigned j=0;j<h;j++)for(unsigned x=0;x<z.width;x++) {
        float v;uint8_t s;optics_depth_unpack(f,(const uint8_t *)bytes+((size_t)j*z.width+x)*bpp,&v,&s);
        memcpy(dst+j*row+x*4,&v,4);dst[row*h+j*srow+x]=s;
    }
    if(!z->initialized&&![self clearDepth:z mask:6 depth:1 stencil:0 error:error])return NO;
    [self endRender:5];id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e)return fail(error,@"Depth import encoder failed");
    [e copyFromBuffer:b->buffer sourceOffset:0 sourceBytesPerRow:row sourceBytesPerImage:row*h sourceSize:MTLSizeMake(z.width,h,1)
        toTexture:z->lease->texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,y,0) options:MTLBlitOptionDepthFromDepthStencil];
    [e copyFromBuffer:b->buffer sourceOffset:row*h sourceBytesPerRow:srow sourceBytesPerImage:srow*h sourceSize:MTLSizeMake(z.width,h,1)
        toTexture:z->lease->texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,y,0) options:MTLBlitOptionStencilFromDepthStencil];
    [e endEncoding];[_leases addObject:b];[_leases addObject:z];_jobs++;_batchBytes+=n;return YES;
}
- (PtR2Readback *)readbackDepth:(PtR2Depth *)z height:(uint32_t)h error:(NSError **)error {
    if(![self owner:error]||![self valid:z?z->lease:nil error:error])return nil;
    if(!h||h>z.height||!z->initialized){fail(error,@"Invalid depth export");return nil;}
    size_t row=(z.width*4u+255u)&~size_t(255),srow=(z.width+255u)&~size_t(255),n=(row+srow)*h;
    if(![self admit:n error:error])return nil;
    R2Lease *b=[self buffer:NULL bytes:n error:error];if(!b)return nil;
    [self endRender:5];id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e){fail(error,@"Depth export encoder failed");return nil;}
    [e copyFromTexture:z->lease->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(z.width,h,1)
        toBuffer:b->buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*h options:MTLBlitOptionDepthFromDepthStencil];
    [e copyFromTexture:z->lease->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(z.width,h,1)
        toBuffer:b->buffer destinationOffset:row*h destinationBytesPerRow:srow destinationBytesPerImage:srow*h options:MTLBlitOptionStencilFromDepthStencil];
    [e endEncoding];PtR2Readback *r=[PtR2Readback new];r->lease=b;r->rowBytes=row;r->stencilRow=srow;r->depthPlanes=YES;
    r->width=z.width;r->height=h;r->ticket=_ticket;r->status.store(0);
    [_readbacks addObject:r];[_leases addObject:r];[_leases addObject:z];_jobs++;_batchBytes+=n;return r;
}
- (NSData *)copyDepthReadback:(PtR2Readback *)r format:(unsigned)f error:(NSError **)error {
    if(![self owner:error]||![self valid:r?r->lease:nil error:error])return nil;
    unsigned bpp=optics_depth_bytes(f);
    if(!bpp||!r->depthPlanes){fail(error,@"Wrong depth readback format");return nil;}
    if(r->status.load(std::memory_order_acquire)!=1){fail(error,@"Depth readback not successfully completed",2);return nil;}
    NSMutableData *out=[NSMutableData dataWithLength:(size_t)r.width*r.height*bpp];const uint8_t *p=(const uint8_t *)r->lease->buffer.contents;
    for(unsigned y=0;y<r.height;y++)for(unsigned x=0;x<r.width;x++) {
        float v;memcpy(&v,p+y*r->rowBytes+x*4,4);uint8_t s=p[r->rowBytes*r.height+y*r->stencilRow+x];
        if(!optics_depth_pack(f,v,s,(uint8_t *)out.mutableBytes+((size_t)y*r.width+x)*bpp)){fail(error,@"GPU depth is outside normalized integer range");return nil;}
    }
    return out;
}
/* Initialize a single-sample target's colour plane from an immutable texture of the same size (import of a
 * guest surface the GPU does not own yet); its own depth/stencil are cleared. Encoded in the current batch, no wait. */
- (PtR2Texture *)view:(PtR2Target *)t component:(const uint8_t *)c error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error])return nil;
    if(!c||t->surface.samples.count!=1||!t->initialized||c[0]>5||c[1]>5||c[2]>5||c[3]>5){fail(error,@"View needs an initialized single-sample target and components 0..5");return nil;}
    auto channel=[](uint8_t x){return x<4?(MTLTextureSwizzle)(MTLTextureSwizzleRed+x):x==4?MTLTextureSwizzleZero:MTLTextureSwizzleOne;};
    id<MTLTexture> v=[t->surface.color newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0,1)
        slices:NSMakeRange(0,1) swizzle:MTLTextureSwizzleChannelsMake(channel(c[0]),channel(c[1]),channel(c[2]),channel(c[3]))];
    if(!v){fail(error,@"Texture view creation failed");return nil;}
    R2Lease *lease=[R2Lease new];lease->runtime=_runtime;lease->epoch=_runtime->epoch;lease->texture=v;lease->parent=t->lease;   /* storage charged to the target */
    PtR2Texture *r=[PtR2Texture new];r->lease=lease;r->status.store(0);return r; /* Borrowed mutable view: never a completed host frame. */
}
- (PtR2Texture *)copyView:(PtR2Target *)t width:(uint32_t)w height:(uint32_t)h component:(const uint8_t *)c error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error])return nil;
    if(!c||t->surface.samples.count!=1||!t->initialized||!w||!h||w>t.width||h>t.height||c[0]>5||c[1]>5||c[2]>5||c[3]>5){
        fail(error,@"Region view needs an initialized single-sample target, an in-bounds size and components 0..5");return nil;}
    if(![self admit:0 error:error])return nil;
    id<MTLTexture> src=t->surface.color;
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat width:w height:h mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
    id<MTLTexture> tex=[_device newTextureWithDescriptor:d];if(!tex){fail(error,@"Region texture allocation failed");return nil;}
    R2Lease *lease=[self charge:tex.allocatedSize error:error];if(!lease)return nil;
    auto channel=[](uint8_t x){return x<4?(MTLTextureSwizzle)(MTLTextureSwizzleRed+x):x==4?MTLTextureSwizzleZero:MTLTextureSwizzleOne;};
    id<MTLTexture> v=[tex newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:MTLTextureType2D levels:NSMakeRange(0,1)
        slices:NSMakeRange(0,1) swizzle:MTLTextureSwizzleChannelsMake(channel(c[0]),channel(c[1]),channel(c[2]),channel(c[3]))];
    if(!v){fail(error,@"Texture view creation failed");return nil;}
    lease->texture=v;   /* the view keeps its storage */
    [self endRender:6];id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e){fail(error,@"Region blit encoder failed");return nil;}
    [e copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(w,h,1)
        toTexture:tex destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];[e endEncoding];
    PtR2Texture *r=[PtR2Texture new];r->lease=lease;r->ticket=_ticket;r->status.store(0);
    [_leases addObject:t];[_leases addObject:r];[_exports addObject:r];_jobs++;return r;
}
- (BOOL)copy:(PtR2Target *)src x:(uint32_t)sx y:(uint32_t)sy width:(uint32_t)w height:(uint32_t)h
    to:(PtR2Target *)dst x:(uint32_t)dx y:(uint32_t)dy error:(NSError **)error {
    if(![self owner:error]||![self valid:src?src->lease:nil error:error]||![self valid:dst?dst->lease:nil error:error])return NO;
    if(src==dst||src->surface.samples.count!=1||dst->surface.samples.count!=1||!src->initialized||!dst->initialized||!w||!h||
       sx>src.width||w>src.width-sx||sy>src.height||h>src.height-sy||dx>dst.width||w>dst.width-dx||dy>dst.height||h>dst.height-dy)
        return fail(error,@"Copy needs two initialized single-sample targets and an in-bounds rectangle");
    if(![self admit:0 error:error])return NO;
    [self endRender:6];id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e)return fail(error,@"Copy blit encoder failed");
    [e copyFromTexture:src->surface.color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(sx,sy,0) sourceSize:MTLSizeMake(w,h,1)
        toTexture:dst->surface.color destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(dx,dy,0)];[e endEncoding];
    [_leases addObject:src];[_leases addObject:dst];_jobs++;return YES;
}
- (BOOL)load:(PtR2Target *)t from:(PtR2Texture *)src error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error]||![self valid:src?src->lease:nil error:error])return NO;
    if(t->surface.samples.count!=1||src->lease->texture.width!=t.width||src->lease->texture.height!=t.height)
        return fail(error,@"Import needs a single-sample target of the texture's size");
    if(![self admit:0 error:error])return NO;
    [self endRender:7];id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e)return fail(error,@"Import blit encoder failed");
    [e copyFromTexture:src->lease->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(t.width,t.height,1)
        toTexture:t->surface.color destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];[e endEncoding];
    [_leases addObject:src];[_leases addObject:t];_jobs++;
    t->initialized=YES;return [self renderTarget:t clearMask:t->bound?0:6 color:MTLClearColorMake(0,0,0,0) depth:1 stencil:0 error:error];
}
- (BOOL)clear:(PtR2Target *)t color:(MTLClearColor)c depth:(double)d error:(NSError **)error {
    return [self clear:t color:c depth:d stencil:0 error:error];
}
- (BOOL)clear:(PtR2Target *)t color:(MTLClearColor)c depth:(double)d stencil:(uint8_t)stencil error:(NSError **)error {
    return [self clear:t mask:7 color:c depth:d stencil:stencil error:error];
}
- (BOOL)clear:(PtR2Target *)t mask:(uint8_t)mask color:(MTLClearColor)c depth:(double)d stencil:(uint8_t)stencil error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error])return NO;
    if(!mask||(mask&~7u)||!std::isfinite(d)||d<0||d>1||!std::isfinite(c.red)||!std::isfinite(c.green)||!std::isfinite(c.blue)||!std::isfinite(c.alpha))return fail(error,@"Invalid clear values");
    if(![self admit:0 error:error]||![self renderTarget:t clearMask:mask color:c depth:d stencil:stencil error:error])return NO;
    _jobs++;return YES;
}
- (id<MTLDepthStencilState>)depthState:(PtR2Raster)r error:(NSError **)error {
    if(!r.stencilEnable)return _depth[r.depthCompare][r.depthWrite];
    uint64_t key=r.depthCompare|(uint64_t)r.depthWrite<<3|(uint64_t)r.stencilReadMask<<4|(uint64_t)r.stencilWriteMask<<12;
    const PtR2StencilFace faces[2]={r.stencilFront,r.stencilBack};unsigned bit=20;
    for(auto f:faces)for(unsigned v:{f.compare,f.fail,f.depthFail,f.pass}){key|=(uint64_t)v<<bit;bit+=3;}
    id<MTLDepthStencilState> state=_stencilStates[@(key)];if(state)return state;
    if(_stencilStates.count>=256){fail(error,@"Stencil state cache admission limit reached");return nil;}
    MTLDepthStencilDescriptor *d=[MTLDepthStencilDescriptor new];d.depthCompareFunction=(MTLCompareFunction)r.depthCompare;d.depthWriteEnabled=r.depthWrite;
    for(unsigned face=0;face<2;face++) {
        MTLStencilDescriptor *s=[MTLStencilDescriptor new];PtR2StencilFace f=faces[face];
        s.stencilCompareFunction=(MTLCompareFunction)f.compare;s.stencilFailureOperation=(MTLStencilOperation)f.fail;
        s.depthFailureOperation=(MTLStencilOperation)f.depthFail;s.depthStencilPassOperation=(MTLStencilOperation)f.pass;
        s.readMask=r.stencilReadMask;s.writeMask=r.stencilWriteMask;if(face)d.backFaceStencil=s;else d.frontFaceStencil=s;
    }
    state=[_device newDepthStencilStateWithDescriptor:d];if(!state){fail(error,@"Stencil state allocation failed");return nil;}
    _stencilStates[@(key)]=state;return state;
}
- (BOOL)draw:(PtR2Program *)p target:(PtR2Target *)t vertices:(const void *)v count:(uint32_t)count
    indices:(const uint32_t *)index count:(uint32_t)n vertexConstants:(const float *)vc fragmentConstants:(const float *)fc
    textures:(NSArray<PtR2Texture *> *)textures linear:(BOOL)linear raster:(PtR2Raster)r error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error])return NO;
    if(!p||p->runtime!=_runtime||p->epoch!=_runtime->epoch||p->route.sampleCount!=t->surface.samples.count||!t->initialized||
       !v||!count||count>(1u<<24)||!index||!n||r.lines>1||n%(r.lines?2u:3u)||n>(1u<<24)||r.depthCompare>7||r.depthWrite>1||r.cull>2||r.frontCCW>1||
       !std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||r.width<=0||r.height<=0||
       !std::isfinite(r.depthBias)||!std::isfinite(r.depthSlope)||!std::isfinite(r.znear)||!std::isfinite(r.zfar)||r.znear<0||r.zfar>1||r.znear>r.zfar||!r.scissorWidth||!r.scissorHeight||
       r.scissorX>=t.width||r.scissorY>=t.height||r.scissorWidth>t.width-r.scissorX||r.scissorHeight>t.height-r.scissorY||
       r.stencilEnable>1||r.alphaTest>1||r.alphaCompare>7||!std::isfinite(r.alphaReference)||r.alphaReference<0||r.alphaReference>1||r.samplers>1)
        return fail(error,@"Unsupported or invalid normalized draw/target/raster");
    for(auto f:{r.stencilFront,r.stencilBack})if(f.compare>7||f.fail>7||f.depthFail>7||f.pass>7)return fail(error,@"Invalid stencil operation");
    if(r.samplers)for(unsigned j=0;j<16;j++)if((r.sampler[j]&7)>5||(r.sampler[j]>>3&7)>5||(r.sampler[j]>>8&3)>2||(r.sampler[j]>>10&7)>4||r.sampler[j]>>17)return fail(error,@"Invalid sampler code");
    id<MTLDepthStencilState> ds=[self depthState:r error:error];if(!ds)return NO;
    for(unsigned j=0;j<n;j++)if(index[j]>=count)return fail(error,@"Index outside immutable vertex snapshot");
    for(unsigned j=0;j<16;j++)if(p->textureMask&(1u<<j)) {
        if(j>=textures.count||![textures[j] isKindOfClass:PtR2Texture.class]||![self valid:textures[j]->lease error:error])return fail(error,@"Missing, stale or foreign texture binding");
    }
    uint64_t bytes=(uint64_t)count*p->route.stride+(uint64_t)n*4+p->vertexConstantBytes+p->fragmentConstantBytes;
    if(![self admit:bytes error:error])return NO;
    static const float zero[256*4]={0};
    R2Slice vb=[self upload:v bytes:(size_t)count*p->route.stride error:error],ib=[self upload:index bytes:n*4 error:error];
    R2Slice vconstants=[self upload:vc?:zero bytes:p->vertexConstantBytes error:error],fconstants=[self upload:fc?:zero bytes:p->fragmentConstantBytes error:error];
    if(!vb.lease||!ib.lease||!vconstants.lease||!fconstants.lease)return NO;
    if(![self renderTarget:t clearMask:0 color:MTLClearColorMake(0,0,0,0) depth:1 stencil:0 error:error])return NO;
    BOOL fresh=!_cacheBindings||!_bindingsValid;
    auto bind=[&](bool changed){_stateCalls++;if(!changed)_stateSkipped++;return changed;};
    if(bind(fresh||_boundProgram!=p)){[_encoder setRenderPipelineState:p->pipeline];[_leases addObject:p];_boundProgram=p;}
    if(bind(fresh||_boundDepth!=ds)){[_encoder setDepthStencilState:ds];_boundDepth=ds;}
    if(bind(fresh||_boundStencil!=r.stencilReference)){[_encoder setStencilReferenceValue:r.stencilReference];_boundStencil=r.stencilReference;}
    if(bind(fresh||_boundCull!=r.cull)){[_encoder setCullMode:(MTLCullMode)r.cull];_boundCull=r.cull;}
    if(bind(fresh||_boundWinding!=r.frontCCW)){[_encoder setFrontFacingWinding:r.frontCCW?MTLWindingCounterClockwise:MTLWindingClockwise];_boundWinding=r.frontCCW;}
    if(bind(fresh||_boundBias[0]!=r.depthBias||_boundBias[1]!=r.depthSlope)){[_encoder setDepthBias:r.depthBias slopeScale:r.depthSlope clamp:0];_boundBias[0]=r.depthBias;_boundBias[1]=r.depthSlope;}
    MTLViewport vp={r.x,r.y,r.width,r.height,r.znear,r.zfar};
    MTLScissorRect sc={r.scissorX,r.scissorY,r.scissorWidth,r.scissorHeight};
    if(bind(fresh||memcmp(&_boundViewport,&vp,sizeof vp))){[_encoder setViewport:vp];_boundViewport=vp;}
    if(bind(fresh||memcmp(&_boundScissor,&sc,sizeof sc))){[_encoder setScissorRect:sc];_boundScissor=sc;}
    if(bind(fresh||memcmp(_boundBlendColor,r.blendColor,sizeof _boundBlendColor))){
        [_encoder setBlendColorRed:r.blendColor[0] green:r.blendColor[1] blue:r.blendColor[2] alpha:r.blendColor[3]];memcpy(_boundBlendColor,r.blendColor,sizeof _boundBlendColor);}
    [_encoder setVertexBuffer:vb.lease->buffer offset:vb.offset atIndex:0];[_encoder setVertexBuffer:vconstants.lease->buffer offset:vconstants.offset atIndex:1];
    if(bind(fresh||memcmp(_boundEpilogue,r.vertexEpilogue,sizeof _boundEpilogue))){
        [_encoder setVertexBytes:r.vertexEpilogue length:sizeof r.vertexEpilogue atIndex:2];memcpy(_boundEpilogue,r.vertexEpilogue,sizeof _boundEpilogue);}
    [_encoder setFragmentBuffer:fconstants.lease->buffer offset:fconstants.offset atIndex:0];
    struct {uint32_t enabled,compare;float reference;} alpha={r.alphaTest,r.alphaCompare,r.alphaReference};
    static_assert(sizeof alpha==12,"Alpha binding snapshot size");
    if(bind(fresh||memcmp(_boundAlpha,&alpha,sizeof alpha))){[_encoder setFragmentBytes:&alpha length:sizeof alpha atIndex:1];memcpy(_boundAlpha,&alpha,sizeof alpha);}
    for(unsigned j=0;j<16;j++)if(p->textureMask&(1u<<j)) {
        id<MTLSamplerState> sampler=_sampler[!!linear];
        if(r.samplers) {
            uint32_t c=r.sampler[j]; __strong id<MTLSamplerState> &s=_samplerStates[c];
            if(!s) {
                MTLSamplerDescriptor *d=[MTLSamplerDescriptor new];d.magFilter=c&64?MTLSamplerMinMagFilterLinear:MTLSamplerMinMagFilterNearest;
                d.minFilter=c&128?MTLSamplerMinMagFilterLinear:MTLSamplerMinMagFilterNearest;
                d.sAddressMode=(MTLSamplerAddressMode)(c&7);d.tAddressMode=(MTLSamplerAddressMode)(c>>3&7);
                d.mipFilter=(c>>8&3)==2?MTLSamplerMipFilterLinear:(c>>8&3)==1?MTLSamplerMipFilterNearest:MTLSamplerMipFilterNotMipmapped;
                d.maxAnisotropy=1u<<(c>>10&7);d.lodMinClamp=(float)(c>>13&15);
                if(!(c>>8&3))d.lodMaxClamp=d.lodMinClamp;   /* no filter between levels: the first level only */
                s=[_device newSamplerStateWithDescriptor:d];
                if(!s)return fail(error,@"Sampler allocation failed");
            }
            sampler=s;
        }
        id<MTLTexture> texture=textures[j]->lease->texture;
        if(bind(fresh||_boundTexture[j]!=texture)){[_encoder setFragmentTexture:texture atIndex:j];[_leases addObject:textures[j]];_boundTexture[j]=texture;}
        if(bind(fresh||_boundSampler[j]!=sampler)){[_encoder setFragmentSamplerState:sampler atIndex:j];_boundSampler[j]=sampler;}
    }
    int64_t visibility=-1;
    if(r.query) {   /* a fresh slot for this query in this pass, zeroed before the GPU can write it */
        if(_passQuery!=r.query) {
            _passSlot=_visibilityNext++%8192;((uint64_t *)_visibility.contents)[_passSlot]=0;_passQuery=r.query;
            if(_querySlots.size()>=4096)_querySlots.clear();   /* queries never asked for */
            auto &slots=_querySlots[r.query];if(slots.size()<64)slots.push_back(_passSlot);
        }
        visibility=_passSlot;
    }
    if(visibility!=_boundVisibility){
        [_encoder setVisibilityResultMode:visibility<0?MTLVisibilityResultModeDisabled:MTLVisibilityResultModeCounting offset:visibility<0?0:(NSUInteger)visibility*8];
        _boundVisibility=visibility;}
    [_encoder drawIndexedPrimitives:r.lines?MTLPrimitiveTypeLine:MTLPrimitiveTypeTriangle indexCount:n indexType:MTLIndexTypeUInt32 indexBuffer:ib.lease->buffer indexBufferOffset:ib.offset];
    _bindingsValid=YES;_batchBytes+=bytes;_jobs++;_draws++;return YES;
}
- (uint64_t)queryCount:(uint32_t)query {
    auto it=_querySlots.find(query);if(it==_querySlots.end())return 0;
    uint64_t n=0;const uint64_t *v=(const uint64_t *)_visibility.contents;for(uint32_t s:it->second)n+=v[s];
    _querySlots.erase(it);return n;
}
- (BOOL)resolve:(PtR2Target *)t control:(uint32_t)c error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error])return NO;
    if(!t->initialized||!(c&1)||(c&~7u))return fail(error,@"Uninitialized target or unsupported resolve control");
    [self endRender:8];if(![_resolver encodeSurface:t->surface commandBuffer:_batch control:c error:error])return NO;
    [_leases addObject:t];return YES;
}
- (PtR2Texture *)snapshot:(PtR2Target *)t resolveControl:(uint32_t)c error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error])return nil;
    if(!t->initialized||!(c&1)||(c&~7u)){fail(error,@"Uninitialized target or unsupported resolve control");return nil;}
    if(![self admit:0 error:error])return nil;
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:t.width height:t.height mipmapped:NO];
    d.storageMode=MTLStorageModePrivate;d.usage=MTLTextureUsageShaderRead;d.hazardTrackingMode=MTLHazardTrackingModeTracked;
    id<MTLTexture> tex=[_device newTextureWithDescriptor:d];if(!tex){fail(error,@"Snapshot allocation failed");return nil;}
    R2Lease *lease=[self charge:tex.allocatedSize error:error];if(!lease)return nil;
    if(![self resolve:t control:c error:error])return nil;
    id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e){fail(error,@"Snapshot blit encoder failed");return nil;}
    [e copyFromTexture:t->surface.resolved sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(t.width,t.height,1)
        toTexture:tex destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];[e endEncoding];
    lease->texture=tex;PtR2Texture *result=[PtR2Texture new];result->lease=lease;result->ticket=_ticket;result->status.store(0);
    [_leases addObject:result];[_exports addObject:result];_jobs++;return result;
}
- (PtR2Frame *)frame:(PtR2Texture *)texture error:(NSError **)error {
    if(![self owner:error]||![self valid:texture?texture->lease:nil error:error])return nil;
    if(texture->status.load(std::memory_order_acquire)!=1){fail(error,@"Frame not successfully completed",2);return nil;}
    PtR2Frame *frame=[PtR2Frame new];frame->source=texture;return frame;
}
- (PtR2Readback *)readback:(PtR2Target *)t resolveControl:(uint32_t)c error:(NSError **)error {
    if(![self owner:error]||![self valid:t?t->lease:nil error:error])return nil;
    if(!t->initialized||!(c&1)||(c&~7u)){fail(error,@"Uninitialized target or unsupported resolve control");return nil;}
    size_t row=((size_t)t.width*4+255)&~size_t(255),bytes=row*t.height;
    if(![self admit:bytes error:error])return nil;
    R2Lease *lease=[self buffer:NULL bytes:bytes error:error];if(!lease)return nil;
    if(![self resolve:t control:c error:error])return nil;
    id<MTLBlitCommandEncoder> e=[_batch blitCommandEncoder];if(!e){fail(error,@"Readback blit encoder failed");return nil;}
    [e copyFromTexture:t->surface.resolved sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(t.width,t.height,1)
        toBuffer:lease->buffer destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:bytes];[e endEncoding];
    PtR2Readback *r=[PtR2Readback new];r->lease=lease;r->rowBytes=row;r->width=t.width;r->height=t.height;r->ticket=_ticket;r->status.store(0);
    [_readbacks addObject:r];[_leases addObject:r];_jobs++;_batchBytes+=bytes;return r;
}
- (NSData *)copyReadback:(PtR2Readback *)r error:(NSError **)error {
    if(![self owner:error]||![self valid:r?r->lease:nil error:error])return nil;
    if(r->depthPlanes){fail(error,@"Depth readback requires an explicit integer format");return nil;}
    if(r->status.load(std::memory_order_acquire)!=1){fail(error,@"Readback not successfully completed",2);return nil;}
    NSMutableData *data=[NSMutableData dataWithLength:(size_t)r.width*r.height*4];
    for(unsigned y=0;y<r.height;y++)memcpy((uint8_t *)data.mutableBytes+(size_t)y*r.width*4,(const uint8_t *)r->lease->buffer.contents+y*r->rowBytes,(size_t)r.width*4);
    return data;
}
- (PtR2Ticket)commit:(NSError **)error {
    if(![self owner:error]||!_batch){fail(error,@"No batch to commit");return {};}
    [self endRender:9];auto state=_runtime;PtR2Ticket ticket=_ticket;uint64_t submitted=nowNS(),submittedAbs=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);BOOL prefix=_completionPrefix;
    R2BatchHold *hold=[R2BatchHold new];hold->leases=[_leases copy];hold->readbacks=[_readbacks copy];hold->exports=[_exports copy];os_log_t log=_log;
    os_signpost_id_t sid=os_signpost_id_generate(log);os_signpost_interval_begin(log,sid,"R2Batch","epoch=%llu seq=%llu jobs=%llu bytes=%llu",ticket.epoch,ticket.sequence,_jobs,_batchBytes);
    {std::lock_guard<std::mutex> lock(state->mutex);state->inflight++;}
    [_batch addCompletedHandler:^(id<MTLCommandBuffer> done) {
        BOOL ok=done.status==MTLCommandBufferStatusCompleted;
        for(PtR2Readback *r in hold->readbacks)r->status.store(ok?1:-1,std::memory_order_release);
        for(PtR2Texture *t in hold->exports)t->status.store(ok?1:-1,std::memory_order_release);
        NSUInteger retained=hold->leases.count;
        /* Retire wrappers BEFORE signalling capacity. Do not strand a retry
         * behind leases captured by a completed handler with no future wake. */
        hold->leases=nil;hold->readbacks=nil;hold->exports=nil;
        PtR2Completion completion={ticket,submitted,nowNS(),done.GPUEndTime>=done.GPUStartTime?done.GPUEndTime-done.GPUStartTime:0,ok};
        dispatch_block_t wake=nil;
        {std::lock_guard<std::mutex> lock(state->mutex);state->inflight--;
            if(ticket.epoch==state->epoch&&!state->closed){
                if(prefix) {
                    /* Handler order across command buffers is not an execution-order guarantee.
                     * Only a contiguous set of retired callbacks becomes guest-visible. */
                    state->retired.emplace(ticket.sequence,completion);
                    for(;;) {
                        auto found=state->retired.find(state->nextCompletion);if(found==state->retired.end())break;
                        PtR2Completion c=found->second;state->retired.erase(found);state->nextCompletion++;
                        if(state->completions.empty())state->completions.push_back(c);
                        else {auto &last=state->completions.back();c.success=c.success&&last.success;
                            c.submittedNS=last.submittedNS;c.gpuSeconds+=last.gpuSeconds;last=c;}
                    }
                }else state->completions.push_back(completion);
                state->completed++;state->gpuSeconds+=completion.gpuSeconds;
                uint64_t latency=completion.completedNS-completion.submittedNS;state->callbackNS+=latency;state->maxCallbackNS=std::max(state->maxCallbackNS,latency);
                { double gs=done.GPUStartTime*1e9,ge=done.GPUEndTime*1e9,cb=(double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW);   /* GPU times: host seconds, mach absolute */
                  if(gs>=(double)submittedAbs && ge>=gs && cb>=ge){state->startNS+=(uint64_t)(gs-(double)submittedAbs);state->endNS+=(uint64_t)(cb-ge);} }
                if(!ok){state->failed=true;state->error=done.error.localizedDescription.UTF8String?:"Metal command failed";}}
            wake=reserveWake(*state);}
        state->changed.notify_all();
        os_signpost_interval_end(log,sid,"R2Batch","success=%d retained=%lu",ok,(unsigned long)retained);
        runWake(state,wake);
    }];
    [_batch commit];_batch=nil;_leases=nil;_readbacks=nil;_exports=nil;_upload=nil;_uploadUsed=0;_jobs=_batchBytes=0;_commits++;return ticket;
}
- (BOOL)poll:(PtR2Completion *)out {
    if(!out)return NO;
    std::lock_guard<std::mutex> lock(_runtime->mutex);if(_runtime->completions.empty())return NO;
    *out=_runtime->completions.front();_runtime->completions.pop_front();return YES;
}
- (BOOL)setCompletionWake:(dispatch_block_t)wake error:(NSError **)error {
    if(![self owner:error])return NO;
    std::lock_guard<std::mutex> lock(_runtime->mutex);_runtime->wake=[wake copy];return YES;
}
- (BOOL)reset:(NSError **)error {
    if(![self isOwner])return fail(error,@"Reset requires its encoding owner");
    [self endRender:10];for(PtR2Readback *r in _readbacks)r->status.store(-1);for(PtR2Texture *t in _exports)t->status.store(-1);
    _batch=nil;_leases=nil;_readbacks=nil;_exports=nil;_upload=nil;_uploadUsed=0;_jobs=_batchBytes=0;
    {std::lock_guard<std::mutex> lock(_runtime->mutex);
        if(_runtime->closed||_runtime->epoch==UINT64_MAX)return fail(error,@"Closed renderer or exhausted reset epoch");
        _runtime->epoch++;_runtime->failed=false;_runtime->error.clear();_runtime->completions.clear();
        _runtime->retired.clear();_runtime->nextCompletion=_sequence+1;
        _runtime->completed=_runtime->callbackNS=_runtime->maxCallbackNS=_runtime->startNS=_runtime->endNS=0;_runtime->gpuSeconds=0;}
    [_programs removeAllObjects];[_stencilStates removeAllObjects];_compilerFailure=nil;return YES;
}
- (NSDictionary *)diagnostics {
    if(![self isOwner])return @{@"error":@"encoding owner required"};
    std::lock_guard<std::mutex> lock(_runtime->mutex);
    NSMutableDictionary *reasons=[NSMutableDictionary new];
    if(_passReasons){NSArray *names=@[@"texture-unpack",@"target-or-colour-clear",@"depth-binding",@"depth-clear",@"depth-growth",@"depth-transfer",@"colour-copy",@"colour-import",@"resolve",@"commit",@"reset",@"close"];
        for(unsigned i=0;i<12;i++)reasons[names[i]]=@(_passBreak[i]);}
    return @{@"passBreaks":reasons,@"component":@"optics",@"abi":@1,@"device":_device.name,@"epoch":@(_runtime->epoch),@"residentBytes":@(_runtime->resident),@"peakBytes":@(_runtime->peak),
        @"inflightBatches":@(_runtime->inflight),@"completedUnpolled":@(_runtime->completions.size()),@"draws":@(_draws),@"commits":@(_commits),
        @"completionPrefix":@(_completionPrefix),@"retiredOutOfOrder":@(_runtime->retired.size()),
        @"renderPasses":@(_passes),@"backpressure":@(_busy),@"programsCompiled":@(_compiled),@"compileNS":@(_compileNS),@"librariesCompiled":@(_libraryCompiles),@"libraryNS":@(_libraryNS),@"gpuFailed":@(_runtime->failed),
        @"bufferAllocations":@(_bufferAllocations),@"snapshotBytes":@(_snapshotBytes),
        @"bindingCache":@(_cacheBindings),@"stateBindRequests":@(_stateCalls),@"stateBindsSkipped":@(_stateSkipped),
        @"lastGPUError":[NSString stringWithUTF8String:_runtime->error.c_str()],
        @"lastCompilerError":_compilerFailure[@"error"]?:@"",
        @"epochCompletedBatches":@(_runtime->completed),@"epochGPUSeconds":@(_runtime->gpuSeconds),
        @"epochMeanSubmitToCallbackNS":@(_runtime->completed?_runtime->callbackNS/_runtime->completed:0),@"epochMaxSubmitToCallbackNS":@(_runtime->maxCallbackNS),
        @"epochMeanSubmitToStartNS":@(_runtime->completed?_runtime->startNS/_runtime->completed:0),@"epochMeanEndToCallbackNS":@(_runtime->completed?_runtime->endNS/_runtime->completed:0),
        @"residentLimit":@(_limits.residentBytes),@"batchByteLimit":@(_limits.batchBytes),@"batchCountLimit":@(_limits.maxBatches),
        @"guestFrontend":@"own R300 frontend (optics executor)",@"depthContract":@"normalized D32Float/Stencil8, not R300 D24S8",@"legacyFallback":@NO};
}
- (NSDictionary *)compilerFailure {return [self isOwner]?(_compilerFailure?:@{}):@{@"error":@"encoding owner required"};}
- (void)close {
    if(!_runtime)return;
    [self endRender:11];for(PtR2Readback *r in _readbacks)r->status.store(-1);for(PtR2Texture *t in _exports)t->status.store(-1);
    _batch=nil;_leases=nil;_readbacks=nil;_exports=nil;_upload=nil;_uploadUsed=0;
    {std::unique_lock<std::mutex> lock(_runtime->mutex);_runtime->closed=true;_runtime->wake=nil;_runtime->completions.clear();_runtime->retired.clear();_runtime->spare.clear();
        _runtime->changed.wait(lock,[&]{return !_runtime->inflight&&!_runtime->wakes;});}
    [_programs removeAllObjects];
}
- (void)dealloc {[self close];if(_ownerQueue)dispatch_queue_set_specific(_ownerQueue,(__bridge void *)self,NULL,NULL);}
@end
