/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#import <Foundation/Foundation.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>
#include <algorithm>
#include <cstring>
#include <time.h>
#include "renderer.h"

/* CPU producer, serial Metal consumer. Jobs contain no guest pointer, callback,
 * OpticsDraw, shader-LRU pointer or RAM writeback. The synchronous lane is a FIFO
 * barrier used for resource creation, visibility, reset and final shutdown. */
class OpticsRenderOwner {
    dispatch_queue_t queue=nil;
    dispatch_semaphore_t capacity=dispatch_semaphore_create(0);
    std::mutex lock;
    std::condition_variable changed;
    size_t bytes=0,peakBytes=0;
    unsigned jobs=0,peakJobs=0;
    bool closing=false;
    NSString *failure=nil;
    std::atomic<bool> failed{false};
public:
    static constexpr unsigned jobLimit=128;
    static constexpr size_t byteLimit=64u<<20;
    OpticsRenderOwner() {
        const char *v=getenv("OPTICS_RENDER_OWNER");
        if(v&&atoi(v)!=0){queue=dispatch_queue_create("org.ptolemy.optics.render-owner",DISPATCH_QUEUE_SERIAL);
            dispatch_queue_set_specific(queue,this,this,NULL);}
    }
    ~OpticsRenderOwner(){closeAdmission();barrier();}
    bool enabled()const{return queue!=nil;}
    dispatch_queue_t ownerQueue()const{return queue;}
    template<class F> auto on(F &&f)->decltype(f()) {
        if(!queue||dispatch_get_specific(this)==this)return f();
        using R=decltype(f());
        if constexpr(std::is_void_v<R>){dispatch_sync(queue,^{@autoreleasepool{f();}});}
        else {__block R result{};dispatch_sync(queue,^{@autoreleasepool{result=f();}});return result;}
    }
    void barrier(){on([]{});}
    void wake(){dispatch_semaphore_signal(capacity);}
    bool error(NSError **e) {
        if(!failed.load(std::memory_order_acquire))return false;
        std::lock_guard<std::mutex> g(lock);
        if(e)*e=[NSError errorWithDomain:@"Ptolemy.RenderOwner" code:1 userInfo:@{NSLocalizedDescriptionKey:failure?:@"Render job failed"}];
        return true;
    }
    void clearError(){barrier();std::lock_guard<std::mutex> g(lock);failure=nil;failed.store(false,std::memory_order_release);}
    void closeAdmission(){std::lock_guard<std::mutex> g(lock);closing=true;changed.notify_all();}
    void fail(NSError *e){std::lock_guard<std::mutex> g(lock);if(!failure)failure=[e.localizedDescription copy]?:@"Render encoding failed";failed.store(true,std::memory_order_release);}
    NSDictionary *diagnostics(){std::lock_guard<std::mutex> g(lock);return @{@"renderOwner":@(enabled()),@"queuedJobs":@(jobs),@"queuedBytes":@(bytes),
        @"peakQueuedJobs":@(peakJobs),@"peakQueuedBytes":@(peakBytes),@"queueJobLimit":@(jobLimit),@"queueByteLimit":@(byteLimit)};}
    bool draw(PtR2Renderer *renderer,PtR2Program *program,PtR2Target *target,
              const void *vertices,uint32_t count,size_t stride,const uint32_t *indices,uint32_t indexCount,
              const float *vc,size_t vcBytes,const float *fc,size_t fcBytes,NSArray<PtR2Texture *> *textures,BOOL linear,PtR2Raster raster,NSError **e) {
        if(!enabled())return [renderer draw:program target:target vertices:vertices count:count indices:indices count:indexCount
            vertexConstants:vc fragmentConstants:fc textures:textures linear:linear raster:raster error:e];
        size_t vb=(size_t)count*stride,ib=(size_t)indexCount*4,charge=vb+ib+256*16+32*16;
        if(!stride||stride>UINT16_MAX||!vertices||!indices||!vc||!fc||vcBytes>256*16||fcBytes>32*16||vb>byteLimit||ib>byteLimit-vb||charge>byteLimit){
            if(e)*e=[NSError errorWithDomain:@"Ptolemy.RenderOwner" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Draw exceeds immutable queue budget"}];return false;
        }
        struct Job {
            PtR2Program *program;PtR2Target *target;NSArray<PtR2Texture *> *textures;
            std::vector<uint8_t> vertex;std::vector<uint32_t> index;
            float vc[256*4],fc[32*4];
        };
        /* One producer may wait for bounded consumer capacity. No device lock is held. */
        {std::unique_lock<std::mutex> g(lock);changed.wait(g,[&]{return closing||failed.load()||(jobs<jobLimit&&charge<=byteLimit-bytes);});
            if(closing||failed.load()){g.unlock();error(e);return false;}
            jobs++;bytes+=charge;peakJobs=std::max(peakJobs,jobs);peakBytes=std::max(peakBytes,bytes);}
        std::unique_ptr<Job> j;
        try {
            j=std::make_unique<Job>();j->program=program;j->target=target;j->textures=[textures copy];
            j->vertex.resize(vb);j->index.assign(indices,indices+indexCount);
            memcpy(j->vertex.data(),vertices,vb);memcpy(j->vc,vc,vcBytes);memcpy(j->fc,fc,fcBytes);
        }catch(const std::bad_alloc &){
            {std::lock_guard<std::mutex> g(lock);jobs--;bytes-=charge;}changed.notify_all();
            if(e)*e=[NSError errorWithDomain:@"Ptolemy.RenderOwner" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Draw snapshot allocation failed"}];return false;
        }
        Job *payload=j.release();
        dispatch_async(queue,^{@autoreleasepool{
            std::unique_ptr<Job> active(payload);
            if(!failed.load(std::memory_order_acquire)) {
                uint64_t deadline=clock_gettime_nsec_np(CLOCK_UPTIME_RAW)+30*NSEC_PER_SEC;
                for(;;){NSError *local=nil;
                    if([renderer draw:active->program target:active->target vertices:active->vertex.data() count:count indices:active->index.data() count:indexCount
                        vertexConstants:active->vc fragmentConstants:active->fc textures:active->textures linear:linear raster:raster error:&local])break;
                    if(local.code!=2||clock_gettime_nsec_np(CLOCK_UPTIME_RAW)>=deadline){fail(local);break;}
                    NSError *commitError=nil;[renderer commit:&commitError];
                    /* GPU completion/lease release only signals; no CPU-owner service is required here. */
                    dispatch_semaphore_wait(capacity,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_MSEC));
                }
            }
            active.reset();
            {std::lock_guard<std::mutex> g(lock);jobs--;bytes-=charge;}changed.notify_all();
        }});
        return true;
    }
};
