/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Queue inputs outlive their producer; capacity, reset and close are bounded. */
#include "render_owner.h"
extern "C" {
#include "draw.h"
}
#include <cassert>
#include <thread>
#include <unistd.h>
static void completed(OpticsRenderOwner &o,PtR2Renderer *r) {
    PtR2Ticket t=o.on([&]{NSError *e=nil;return [r commit:&e];});assert(t.sequence);
    bool done=false;
    for(unsigned k=0;k<5000&&!done;k++) {
        o.on([&]{PtR2Completion c;while([r poll:&c]){assert(c.success);done|=c.ticket.sequence==t.sequence;}});
        if(!done)usleep(1000);
    }assert(done);
}
int main(){@autoreleasepool {
    OpticsRenderOwner o;assert(o.enabled());id<MTLDevice> d=MTLCreateSystemDefaultDevice();assert(d);
    PtR2Limits limits=pt_r2_default_limits(d);limits.maxJobs=4;limits.maxBatches=3;
    PtR2Renderer *r=o.on([&]{NSError *e=nil;return [[PtR2Renderer alloc]initWithDevice:d limits:limits ownerQueue:o.ownerQueue() error:&e];});assert(r);
    OpticsRenderOwner *op=&o;dispatch_block_t wake=^{op->wake();};
    o.on([&]{NSError *e=nil;assert([r setCompletionWake:wake error:&e]);});
    PtR2ShaderIR pvs,us;PtR2ShaderError er={};assert(optics_draw_passthrough(2,&pvs));
    uint32_t fs[]={0,0,0,0,0,0,0x400000,0,(7u<<26)|32u,(1u<<24)|32u,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    assert(pt_r2_shader_decode(PT_R2_SHADER_US,PT_R2_SHADER_FP32_FTZ0,fs,12,0,&us,&er)==PT_R2_SHADER_OK);
    PtR2ShaderInterface vi={},fi={};vi.abi=fi.abi=PT_R2_SHADER_ABI;vi.input_count=vi.output_count=2;vi.constant_count=1;
    fi.input_count=1;fi.output_count=4;fi.constant_count=32;
    PtR2Route route={};route.colorMask=15;route.varyingCount=1;route.pvsOutput[0]=1;route.sampleCount=1;route.stride=32;
    route.attribute[0]=(PtR2Attribute){0,4,0};route.attribute[1]=(PtR2Attribute){16,4,0};
    PtR2Program *p=o.on([&]{NSError *e=nil;return [r preparePVS:&pvs vertexInterface:&vi fragment:&us interface:&fi route:route error:&e];});assert(p);
    PtR2Target *t=o.on([&]{NSError *e=nil;return [r targetWidth:32 height:32 samples:(PtR2MsaaInput){} error:&e];});assert(t);
    o.on([&]{NSError *e=nil;assert([r clear:t color:MTLClearColorMake(0,0,0,1) depth:1 error:&e]);});
    PtR2Raster raster={};raster.width=raster.height=raster.scissorWidth=raster.scissorHeight=32;raster.zfar=1;raster.depthCompare=7;
    float v[3][8]={{-1,-1,0,1,1,1,1,1},{3,-1,0,1,1,1,1,1},{-1,3,0,1,1,1,1,1}},original[3][8];memcpy(original,v,sizeof v);
    uint32_t ix[3]={0,1,2};float vc[4]={},fc[128]={0,1,0,1};
    dispatch_semaphore_t gate=dispatch_semaphore_create(0),entered=dispatch_semaphore_create(0);
    dispatch_async(o.ownerQueue(),^{dispatch_semaphore_signal(entered);dispatch_semaphore_wait(gate,DISPATCH_TIME_FOREVER);});
    dispatch_semaphore_wait(entered,DISPATCH_TIME_FOREVER);
    NSError *e=nil;assert(o.draw(r,p,t,v,3,32,ix,3,vc,sizeof vc,fc,sizeof fc,@[],NO,raster,&e));
    memset(v,0,sizeof v);memset(ix,0,sizeof ix);fc[0]=1;fc[1]=0;
    dispatch_semaphore_signal(gate);o.barrier();assert(!o.error(&e));
    PtR2Readback *rb=o.on([&]{NSError *x=nil;return [r readback:t resolveControl:1 error:&x];});assert(rb);completed(o,r);
    NSData *pixels=o.on([&]{NSError *x=nil;return [r copyReadback:rb error:&x];});assert(pixels);
    const uint8_t *b=(const uint8_t *)pixels.bytes;assert(b[(16*32+16)*4]==0&&b[(16*32+16)*4+1]==255);
    memcpy(v,original,sizeof v);ix[0]=0;ix[1]=1;ix[2]=2;fc[0]=0;fc[1]=1;
    dispatch_async(o.ownerQueue(),^{dispatch_semaphore_signal(entered);dispatch_semaphore_wait(gate,DISPATCH_TIME_FOREVER);});
    dispatch_semaphore_wait(entered,DISPATCH_TIME_FOREVER);
    for(unsigned k=0;k<OpticsRenderOwner::jobLimit;k++)assert(o.draw(r,p,t,v,3,32,ix,3,vc,sizeof vc,fc,sizeof fc,@[],NO,raster,&e));
    assert([o.diagnostics()[@"queuedJobs"] unsignedIntValue]==OpticsRenderOwner::jobLimit);
    std::atomic<bool> released{false};
    std::thread release([&]{usleep(20000);released.store(true);dispatch_semaphore_signal(gate);});
    assert(o.draw(r,p,t,v,3,32,ix,3,vc,sizeof vc,fc,sizeof fc,@[],NO,raster,&e));assert(released.load());release.join();
    o.barrier();assert(!o.error(&e));completed(o,r);
    NSDictionary *passes=o.on([&]{return [r diagnostics];});assert([passes[@"passBreaks"][@"commit"] unsignedLongLongValue]>0);
    auto stats=o.diagnostics();assert([stats[@"peakQueuedJobs"] unsignedIntValue]<=OpticsRenderOwner::jobLimit);
    assert([stats[@"peakQueuedBytes"] unsignedLongLongValue]<=OpticsRenderOwner::byteLimit);
    /* Reset while the queue is full; the barrier retires all prior encoding.
     * Several earlier command buffers can still be in flight in the old epoch. */
    auto fill=[&]{
        dispatch_async(o.ownerQueue(),^{dispatch_semaphore_signal(entered);dispatch_semaphore_wait(gate,DISPATCH_TIME_FOREVER);});
        dispatch_semaphore_wait(entered,DISPATCH_TIME_FOREVER);
        for(unsigned k=0;k<OpticsRenderOwner::jobLimit;k++)assert(o.draw(r,p,t,v,3,32,ix,3,vc,sizeof vc,fc,sizeof fc,@[],NO,raster,&e));
        assert([o.diagnostics()[@"queuedJobs"] unsignedIntValue]==OpticsRenderOwner::jobLimit);
    };
    fill();dispatch_semaphore_signal(gate);o.on([&]{NSError *x=nil;assert([r reset:&x]);});
    assert(o.draw(r,p,t,v,3,32,ix,3,vc,sizeof vc,fc,sizeof fc,@[],NO,raster,&e));o.barrier();assert(o.error(&e));
    o.on([&]{NSError *x=nil;assert([r reset:&x]);});o.clearError();assert(!o.error(&e));
    p=o.on([&]{NSError *x=nil;return [r preparePVS:&pvs vertexInterface:&vi fragment:&us interface:&fi route:route error:&x];});assert(p);
    t=o.on([&]{NSError *x=nil;return [r targetWidth:32 height:32 samples:(PtR2MsaaInput){} error:&x];});assert(t);
    o.on([&]{NSError *x=nil;assert([r clear:t color:MTLClearColorMake(0,0,0,1) depth:1 error:&x]);});
    assert(!o.draw(r,p,t,v,UINT32_MAX,32,ix,3,vc,sizeof vc,fc,sizeof fc,@[],NO,raster,&e));
    /* Close with a full queue. Completion callbacks cannot retain either owner afterwards. */
    fill();o.closeAdmission();dispatch_semaphore_signal(gate);o.on([&]{[r close];});r=nil;
    puts("PASS: immutable draw snapshots, 128-job capacity, FIFO, reset epoch, sticky errors and close");
}return 0;}
