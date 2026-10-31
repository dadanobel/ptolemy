/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The 3D executor of the R300 model: OpticsDraw on the Metal renderer.
 * - One instance per device (executor.h create/reset/destroy): renderer, GPU-owned surfaces, caches and counters all
 *   belong to it, so a device reset or close reaches every GPU-owned resource.
 * - Colour surfaces become GPU-owned on their first draw (imported once from VRAM) and stay on the GPU across draws;
 *   independent depth/stencil storage lives only on the GPU. The frontend calls materialize() before CPU, 2D,
 *   fetch or scanout access to VRAM (all CPU VRAM accesses trap), and only then is the colour written back. A failed
 *   export keeps the surface dirty and is reported; it never becomes clean data.
 * - Guest-visible completion (fence) commits the earlier work and is DONE only once every committed batch completed
 *   successfully; a failed batch fails the executor (sticky until reset).
 * - The scanout is published natively: an immutable GPU snapshot of the owned scanout surface, handed to the UI once
 *   its copy completed. VRAM is not written for the display.
 * - Pixels (fragment programs, textures, blending, depth/stencil) run on Metal; the guest PVS program runs in the Metal
 *   vertex function when the draw allows it, otherwise on the CPU (optics_draw_vertices) through a pass-through program.
 * - A read of owned bytes (scanout, texture, CPU read) writes a dirty surface back and keeps it owned (clean); a write
 *   by anything else drops it. Depth/stencil therefore survive reads, and a re-import happens only after foreign writes.
 * The owned-surface list is a small linear array (16); one scanout snapshot texture is allocated per published frame
 *   (pool them if allocation shows up in a profile). */
#import <Metal/Metal.h>
#include "renderer.h"
#include "render_owner.h"
#import "diagnostics_metal.h"
extern "C" {
#include "executor.h"
#include "io.h"
}
#include <algorithm>
#include <time.h>
#include <cstddef>
#include <deque>
#include <set>
#include <unordered_map>
#include <vector>

namespace {
/* clear: pending fast clear planes (1 colour, 2 depth); version: unique per modification (scanout publication) */
/* VRAM pages: own = GPU-owned destination while dirty (CPU access faults/traps into an export), watch = watched clean copy
 * (a CPU write drops the surface). Exactly one of them is held while the surface is in `owned`. */
struct Surface { uint64_t start,end,gpu; uint32_t width,height; PtR2Target *target; bool dirty; uint8_t clear; PtR2Texture *view; uint32_t view_key; uint64_t version;
    PtR2IoToken own,watch; PtR2Depth *bound; uint8_t bpp,fmt,sel;   /* clear: pending colour fast clear; bound: the depth storage its passes use; bpp: guest bytes
                                                                     * per pixel of R300 colour format fmt; sel: its US_OUT_FMT_0 selects (colour_layout) */
    uint64_t cached_seen=~0ull;                                          /* cache_adds when draws into it last dropped overlapping cached textures */
    PtR2Texture *region=nil; uint64_t region_key=0,region_version=0; };   /* its last copyView, valid while its version is */
/* A colour format's guest bytes <-> the executor's RGBA8 target (draw.c lists the formats). sel: US_OUT_FMT_0
 * C0..C3 (2 bits each: A, R, G, B), byte k of a pixel holding C_k; zero (not programmed) reads as B, G, R, A. Tiger's driver
 * programs it (Doom 3): 0x1b for the drawable, 0x39 (R, G, B, A) for some targets. Unverified: UV88 holds C0 and C1,
 * I8 holds C2. */
static unsigned colour_bytes(unsigned f) { return f==10?8:f==6?4:f==9?1:2; }
static unsigned colour_channel(unsigned sel,unsigned k) { unsigned s=(sel?sel:0x1b)>>(2*k)&3; return s?s-1:3; }   /* RGBA index of C_k */
static OpticsPixelLayout colour_layout(unsigned f,unsigned sel) {
    OpticsPixelLayout l={OPTICS_PIXEL_RGBA8,{2,1,0,3}};
    if(f==6||f==10) for(unsigned k=0;k<4;k++) l.component[colour_channel(sel,k)]=(uint8_t)k;   /* ARGB16161616: its high bytes, compacted first */
    else if(f==3||f==4||f==15){ l.format=f==4?OPTICS_PIXEL_RGB565_LE:f==3?OPTICS_PIXEL_ARGB1555_LE:OPTICS_PIXEL_ARGB4444_LE; for(unsigned k=0;k<4;k++) l.component[k]=(uint8_t)k; }
    else if(f==9||f==13){ l.format=f==9?OPTICS_PIXEL_R8:OPTICS_PIXEL_RG8; for(unsigned k=0;k<4;k++) l.component[k]=k==3?5:4;
        if(f==9) l.component[colour_channel(sel,2)]=0; else { l.component[colour_channel(sel,0)]=0; l.component[colour_channel(sel,1)]=1; } }
    return l;
}
static void colour_pack(unsigned f,unsigned sel,const uint8_t *p,uint8_t *o) {   /* one RGBA8 pixel -> guest bytes */
    auto q=[&](unsigned c,unsigned max){ return ((unsigned)p[c]*max+127)/255; };
    unsigned v=0;
    switch(f) {
    case 6: for(unsigned k=0;k<4;k++) o[k]=p[colour_channel(sel,k)]; return;
    case 10: for(unsigned k=0;k<4;k++) o[2*k]=o[2*k+1]=p[colour_channel(sel,k)]; return;   /* each channel *257 */
    case 9: o[0]=p[colour_channel(sel,2)]; return;
    case 13: o[0]=p[colour_channel(sel,0)]; o[1]=p[colour_channel(sel,1)]; return;
    case 4: v=q(0,31)<<11|q(1,63)<<5|q(2,31); break;
    case 3: v=(p[3]>=128?1u:0u)<<15|q(0,31)<<10|q(1,31)<<5|q(2,31); break;
    default: v=q(3,15)<<12|q(0,15)<<8|q(1,15)<<4|q(2,15); break;   /* 15: ARGB4444 */
    }
    o[0]=(uint8_t)v; o[1]=(uint8_t)(v>>8);
}
/* Independent depth/stencil storage by guest identity: ZB_DEPTHOFFSET, ZB_DEPTHPITCH and size (normalized D32F/S8). Two
 * colour targets on one depth buffer share it; one colour target alternating depth buffers rebinds. Depth is never
 * exported to VRAM: a CPU read of its bytes is counted as refused, a CPU write drops the GPU copy. clear: pending fast
 * clear planes (2 depth, 4 stencil) with the values in force at the next use. */
struct Depth { uint64_t zb,start,end; uint32_t pitch,width,height; PtR2Depth *depth; uint8_t clear; PtR2IoToken watch;
    uint32_t format=0,extent=0,layout=0; bool dirty=false,linear_io=false; }; /* height: private capacity; extent: largest guest-visible row used */
static int cull_mode() {static int mode=-1;if(mode<0){const char *v=getenv("OPTICS_CULL");mode=v?atoi(v):1;}return mode;}   /* draw() below */
static bool depth_io() {static int enabled=-1;if(enabled<0){const char *v=getenv("OPTICS_DEPTH_IO");enabled=v&&atoi(v)!=0;}return enabled;}
/* Decoded textures by exact source identity; dropped when anything writes their VRAM bytes (CPU, 2D, our write-back,
 * or a draw into an overlapping owned surface). GART-resident textures are kept by identity and content instead, since
 * guest RAM writes do not trap: every use reads and hashes their bytes, which costs far less than an upload (Halo made
 * about 1700 GART uploads per second without this). */
struct Cached { uint64_t start,end; uint32_t key[10]; PtR2Texture *texture; PtR2IoToken watch; };   /* watch: its source pages */
/* Decoded texture bytes kept across draws; the renderer budget (512 MiB, 1 GiB with 16 GB) covers targets too. With
 * mip levels (+1/3) Doom 3's textures do not fit 64 MiB: every draw re-uploaded evicted ones (17x the uploads, 5x the
 * render passes, 54 -> 34 fps). */
const uint64_t CACHE_LIMIT=192ull<<20; const size_t CACHE_ENTRIES=4096;
/* Prepared Metal programs by (US program, PVS program or pass-through, interface counts, route): the renderer's key
 * build re-validates both stages, so it runs once per distinct combination instead of per draw. */
struct ProgramKey { uint64_t us,pvs; uint32_t outputs,inputs,cube; PtR2Route route; };   /* pvs 0: the pass-through form; cube: units sampled as cube maps */
const size_t KEY_BYTES=offsetof(ProgramKey,route)+sizeof(PtR2Route);   /* no tail padding in hash/compare */
struct KeyHash { size_t operator()(const ProgramKey &k) const {
    const uint8_t *p=(const uint8_t *)&k; uint64_t h=1469598103934665603ull; for(size_t i=0;i<KEY_BYTES;i++){h^=p[i];h*=1099511628211ull;} return (size_t)h; } };
struct KeyEq { bool operator()(const ProgramKey &a,const ProgramKey &b) const { return !memcmp(&a,&b,KEY_BYTES); } };
/* A texture's bytes from TX_OFFSET as Tiger's driver stores them (draw.c): levels one after another from the largest, a
 * level's cube faces together, rows of the pitch at level 0 and of exactly the width below. Returns the total. */
static size_t texture_levels(const OpticsTexture *t,PtR2Levels *lv) {
    size_t total=0; unsigned bpp=t->bpp,faces=t->cube?6:1; lv->levels=t->levels;
    for(unsigned k=0;k<t->levels;k++) {
        uint32_t w=std::max(1u,t->width>>k),h=std::max(1u,t->height>>k);
        lv->row[k]=(size_t)OPTICS_PIXEL_UNITS(t->layout,k?w:t->pitch)*bpp; lv->face[k]=lv->row[k]*OPTICS_PIXEL_UNITS(t->layout,h);   /* DXT: rows of blocks */
        lv->offset[k]=total; total+=lv->face[k]*faces;
    }
    return total;
}
struct TexKey { uint32_t k[10]; bool operator==(const TexKey &o) const { return !memcmp(k,o.k,sizeof k); } };   /* exact source identity; GART: and content hash */
struct TexHash { size_t operator()(const TexKey &t) const { uint64_t h=1469598103934665603ull; for(uint32_t v:t.k){h^=v;h*=1099511628211ull;} return (size_t)h; } };
static uint64_t content_hash(const uint8_t *p,size_t n) {   /* four independent multiply-xor lanes over 8-byte words */
    const uint64_t m=0x9e3779b97f4a7c15ull; uint64_t h[4]={n,n^1,n^2,n^3}; size_t i=0;
    for(;i+32<=n;i+=32) for(int l=0;l<4;l++){ uint64_t v; memcpy(&v,p+i+8*l,8); h[l]=(h[l]^v)*m; h[l]^=h[l]>>29; }
    for(;i<n;i++) h[0]=(h[0]^p[i])*m;
    uint64_t r=0; for(int l=0;l<4;l++){ r=(r^h[l])*0xff51afd7ed558ccdull; r^=r>>33; } return r;
}
const uint64_t GART_LIMIT=128ull<<20; const size_t GART_ENTRIES=2048;
struct PendingClear { uint64_t gpu; int plane; };   /* fast clears of buffers with no owned surface yet */
struct Big { uint64_t gpu; uint32_t w,h; };          /* development: OPTICS_SURFACE_DUMP candidates */
const uint64_t WAIT_LIMIT_NS=10ull*NSEC_PER_SEC;    /* a GPU that completes nothing for this long has failed */

/* The refusal reason is the fixed class text (counted by the frontend); the detail goes to the log a few times. */
int fail(const char **why,const char *s,NSError *e) {
    static unsigned logged; if(e && logged++<32) NSLog(@"optics-frontend: %s: %@",s,e.localizedDescription);
    *why=s; return OPTICS_EXEC_REFUSED;
}
int failhard(const char **why,const char *s,NSError *e) {   /* the executor itself failed: not a capability refusal */
    NSLog(@"optics-frontend: %s failed: %@",s,e?e.localizedDescription:@"no detail");
    *why=s; return OPTICS_EXEC_FAILED;
}

struct Exec {
    OpticsRenderOwner owner;
    PtRadeon *dev=nullptr;
    PtR2Renderer *renderer=nil; dispatch_semaphore_t completed=nil;
    std::vector<Surface> owned;
    uint64_t committed=0,finished=0,next_version=1,published_version=0;   /* renderer ticket sequences; surface versions */
    bool busy=false;                 /* re-entry guard: our own write-back reaches VRAM through the frontend */
    bool broken=false;               /* a GPU batch or export failed: every later request fails until reset */
    unsigned since_commit=0;         /* draws batched since the last commit */
    uint64_t stats_raw_draws=0,stats_raw_bytes=0,stats_vte2_draws=0;
    uint64_t stats_draws=0,stats_imports=0,stats_writebacks=0,stats_commits=0,stats_texture_hits=0,stats_texture_uploads=0,stats_compiles=0;
    uint64_t upload_ns=0; uint32_t upload_last[5]={};   /* OPTICS_HITCH: time in texture uploads; the last one's width, height, format, bytes, VRAM */
    std::vector<Cached> cache; uint64_t cache_lo=UINT64_MAX,cache_hi=0,cache_bytes=0,cache_adds=0;   /* oldest first; ranges for invalidation; entries ever added */
    std::unordered_map<TexKey,PtR2Texture *,TexHash> cache_index;                       /* the same entries by identity */
    std::unordered_map<TexKey,PtR2Texture *,TexHash> gart; std::deque<std::pair<TexKey,size_t>> gart_order; uint64_t gart_bytes=0,stats_gart_hits=0;   /* GART textures, oldest first */
    std::unordered_map<ProgramKey,PtR2Program *,KeyHash,KeyEq> programs;
    ProgramKey last_key; PtR2Program *last_program=nil; bool last_valid=false;   /* consecutive draws mostly share one (the hash was 3.5% of the thread) */
    PtR2ShaderIR *pass_ir[9]={};
    std::vector<PendingClear> pending;
    std::vector<Depth> depths; uint64_t depth_reads_refused=0,depth_drops=0;
    std::vector<Big> big;
    PtR2Texture *snap=nil; uint64_t snap_version=0;   /* scanout snapshot in flight */
    pthread_t adopted{};   /* the renderer's current encoding owner (CP thread or CPU thread, under the device lock) */
    Exec *adopt() { pthread_t t=pthread_self(); if(renderer && !pthread_equal(t,adopted)){ [renderer adoptOwner]; adopted=t; } return this; }
    uint64_t scan_start=0; uint32_t scan_w=0,scan_h=0,scan_line=0; bool scan_valid=false;

    ~Exec() { for(auto *p:pass_ir) free(p); }
    bool ensure(NSError **e) {   /* renderer on first use, on the CPU owner thread */
        if(renderer) return true;
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); if(!device) return false;
        /* Four batches in flight: fences and scanout snapshots commit small batches next to the draw batches. */
        PtR2Limits limits=pt_r2_default_limits(device); limits.maxBatches=std::max(limits.maxBatches,4u);
        NSError *local=nil;
        renderer=owner.on([&]{return [[PtR2Renderer alloc] initWithDevice:device limits:limits ownerQueue:owner.ownerQueue() error:&local];});
        if(!renderer){if(e)*e=local;return false;}
        adopted=pthread_self(); completed=dispatch_semaphore_create(0); dispatch_semaphore_t s=completed; PtRadeon *d=dev;
        /* Completion threads only post: the semaphore for a waiting owner, the kick for a CP waiting on a fence. */
        dispatch_block_t wake=^{dispatch_semaphore_signal(s);owner.wake();optics_frontend_wake(d);};
        BOOL ok=owner.on([&]{return [renderer setCompletionWake:wake error:&local];});
        if(e)*e=local;return ok;
    }
    PtR2Io *io() { return optics_frontend_io(dev); }
    void unwatch(PtR2IoToken &t) { if(t.id){ pt_r2_io_release(io(),t); t=PtR2IoToken{}; } }
    bool watch(PtR2IoToken &t,uint64_t start,uint64_t n) {
        PtR2IoResult r=optics_frontend_cpu_sync(dev,start,n)?PT_R2_IO_COHERENCE:pt_r2_io_watch(io(),start,(size_t)n,&t); if(r==PT_R2_IO_OK) return true;
        static unsigned shown; if(shown++<8) NSLog(@"optics-frontend: VRAM watch %#llx+%llu: %s",(unsigned long long)start,(unsigned long long)n,pt_r2_io_result(r));
        t=PtR2IoToken{}; return false;
    }
    void forget(Surface &s) {   /* leaving `owned`: its pages open (a dirty surface was exported first, or is discarded at reset) */
        unwatch(s.watch); if(s.own.id){ pt_r2_io_write_abort(io(),s.own); s.own=PtR2IoToken{}; }
    }
    void drop_cache() { for(auto &c:cache) unwatch(c.watch); cache.clear(); cache_index.clear(); cache_bytes=0; cache_lo=UINT64_MAX; cache_hi=0;
        gart.clear(); gart_order.clear(); gart_bytes=0; }
    void uncache(size_t i) { TexKey k; memcpy(k.k,cache[i].key,sizeof k.k); cache_index.erase(k); unwatch(cache[i].watch); cache_bytes-=cache[i].end-cache[i].start; cache.erase(cache.begin()+i); }
    void invalidate(uint64_t start,uint64_t end) {
        if(cache.empty() || end<=cache_lo || start>=cache_hi) return;
        for(size_t i=0;i<cache.size();) if(cache[i].start<end && start<cache[i].end) uncache(i); else i++;
        cache_lo=UINT64_MAX; cache_hi=0; for(auto &c:cache){cache_lo=std::min(cache_lo,c.start);cache_hi=std::max(cache_hi,c.end);}
    }
    /* The GPU writes the surface: on the clean->dirty edge its pages become GPU-owned. false: no ownership (fail). */
    bool touch(Surface &s) {
        if(!s.dirty) {
            unwatch(s.watch);
            PtR2IoResult r=optics_frontend_cpu_sync(dev,s.start,s.end-s.start)?PT_R2_IO_COHERENCE:pt_r2_io_write_begin(io(),s.start,(size_t)(s.end-s.start),&s.own);
            if(r!=PT_R2_IO_OK){ NSLog(@"optics-frontend: VRAM ownership of %#llx: %s",(unsigned long long)s.gpu,pt_r2_io_result(r)); s.own=PtR2IoToken{}; broken=true; return false; }
        }
        s.dirty=true; s.version=next_version++; return true;
    }
    void poll() {   /* completions: progress and success; a failed batch breaks the executor */
        if(owner.error(NULL))broken=true;
        PtR2Completion c;
        while([renderer poll:&c]) {
            if(!c.success){ if(!broken) NSLog(@"optics-frontend: GPU batch %llu failed",(unsigned long long)c.ticket.sequence); broken=true; }
            if(c.ticket.sequence>finished) finished=c.ticket.sequence;
        }
        while(!tokens.empty() && (tokens.front().first<=finished || broken)){ optics_frontend_timeline_complete(dev,tokens.front().second,!broken); tokens.erase(tokens.begin()); }
    }
    /* Timeline: the jobs the frontend issued since the last submission are in the batches through `committed`. */
    std::vector<std::pair<uint64_t,PtR2TimelineToken>> tokens;
    void submit_jobs() {
        PtR2TimelineToken k; if(!optics_frontend_timeline_submit(dev,&k)) return;
        if(finished>=committed || broken) optics_frontend_timeline_complete(dev,k,!broken); else tokens.push_back({committed,k});
    }
    /* The wake semaphore also counts lease releases, so progress is measured by completions and real time only. */
    static uint64_t now() { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
    bool drain() {   /* every committed batch completed successfully */
        uint64_t since=now(),seen=finished;
        for(;;) {
            poll(); if(broken) return false; if(finished>=committed) return true;
            if(finished>seen){ seen=finished; since=now(); }
            if(now()-since>=WAIT_LIMIT_NS){ NSLog(@"optics-frontend: GPU completed nothing for %llu s",(unsigned long long)(WAIT_LIMIT_NS/NSEC_PER_SEC)); broken=true; return false; }
            dispatch_semaphore_wait(completed,dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC));
        }
    }
    bool commit(NSError **e) {
        owner.barrier();if(owner.error(e)){broken=true;return false;}
        since_commit=0;
        NSError *local=nil;PtR2Ticket t=owner.on([&]{return [renderer commit:&local];});if(e)*e=local;
        if(!t.sequence) return false;   /* nothing batched is not an error for us */
        committed=t.sequence; stats_commits++; submit_jobs(); return true;
    }
    void wait_one() {   /* at least one more completion, everything committed done, or 100 ms */
        uint64_t before=finished,deadline=now()+100*NSEC_PER_MSEC; poll();
        while(finished==before && finished<committed && !broken && now()<deadline) {
            dispatch_semaphore_wait(completed,dispatch_time(DISPATCH_TIME_NOW,(int64_t)(deadline-now()))); poll();
        }
    }
    /* Backpressure (renderer error code 2: batch or completion budget): commit what is batched, wait for a completion,
     * retry. e is the caller's own strong variable (no ARC write-back temporary), so the call's error is what is seen.
     * Bounded by the GPU's progress: a GPU that completes nothing for WAIT_LIMIT_NS fails the executor. */
    template<typename F> auto retry(F f,NSError *__strong *e) -> decltype(f()) {
        auto r=f(); if(r || !*e || (*e).code!=2) return r;   /* the common case reads no clock */
        uint64_t since=now();
        while(!r && *e && (*e).code==2 && !broken && now()-since<WAIT_LIMIT_NS) {
            *e=nil; NSError *c=nil; uint64_t before=finished; commit(&c); wait_one();
            if(finished>before) since=now();
            r=f();
        }
        return r;
    }
    /* A dirty surface's colour back to VRAM. false: the bytes in VRAM are not current (the surface stays dirty). */
    bool write_back(Surface &s) {
        if(!s.dirty) return true;
        if(broken) return false;
        NSError *e=nil; NSData *rgba=nil;
        static int rate=-1; if(rate<0) rate=getenv("OPTICS_RATE") && atoi(getenv("OPTICS_RATE"));   /* OPTICS_RATE: where a write-back's time goes */
        uint64_t t0=rate?now():0,t1=t0,t2=t0,t3=t0,pending=committed-finished;
        PtR2Readback *rb=retry([&]{return owner.on([&]{return [renderer readback:s.target resolveControl:1 error:&e];});},&e);
        if(rb && commit(&e)) { if(rate) t1=t2=t3=now();
            if(drain()) { if(rate) t2=t3=now(); rgba=owner.on([&]{return [renderer copyReadback:rb error:&e];}); if(rate) t3=now(); } }
        if(!rgba){ NSLog(@"optics-frontend: surface write-back failed: %@",e?e.localizedDescription:@"GPU batch failed"); broken=true; return false; }
        std::vector<uint8_t> rows((size_t)s.width*s.height*s.bpp); const uint8_t *p=(const uint8_t *)rgba.bytes;
        for(size_t i=0,o=0;o<rows.size();i+=4,o+=s.bpp) colour_pack(s.fmt,s.sel,p+i,&rows[o]);
        /* The GPU-owned pages receive the export through their captured spans, then open; the clean copy is watched. */
        PtR2IoResult r=pt_r2_io_write_complete(io(),s.own,rows.data(),rows.size()); s.own=PtR2IoToken{};
        if(r!=PT_R2_IO_OK){ NSLog(@"optics-frontend: surface write-back at %#llx: %s",(unsigned long long)s.gpu,pt_r2_io_result(r)); broken=true; return false; }
        stats_writebacks++; invalidate(s.start,s.end); s.dirty=false;
        (void)watch(s.watch,s.start,s.end-s.start);   /* if this fails, materialize drops the unwatched surface */
        if(rate) { static uint64_t n,a,b,c,d,q; n++; a+=t1-t0; b+=t2-t1; c+=t3-t2; d+=now()-t3; q+=pending;
            if(!(n%32)) { NSLog(@"optics-frontend: write-backs %llu (%ux%u): per write-back, last 32: readback+commit %.2f ms, GPU wait %.2f ms, copy %.2f ms, convert+export %.2f ms; batches pending before %.1f",
                (unsigned long long)n,s.width,s.height,a/32e6,b/32e6,c/32e6,d/32e6,q/32.0); a=b=c=d=q=0; } }
        return true;
    }
    /* Pending fast clears, applied at the buffer's next use with the clear values in force then (see frontend.c). */
    bool apply_clear(Surface &s,uint32_t color,uint32_t depth_word,uint32_t depth_format) {
        if(!s.clear) return true;
        if(broken) return false;
        float depth=optics_draw_depth_clear(depth_word,depth_format);
        unsigned planes=1;   /* colour; depth planes belong to Depth storage */
        NSError *e=nil; MTLClearColor c=MTLClearColorMake((color>>16&255)/255.0,(color>>8&255)/255.0,(color&255)/255.0,(color>>24)/255.0);
        if(s.fmt!=6 && s.fmt!=10) {   /* a narrow format's clear value in its own bytes (unverified: the low ones) */
            const uint8_t b[4]={(uint8_t)color,(uint8_t)(color>>8),(uint8_t)(color>>16),(uint8_t)(color>>24)}; uint8_t q[4];
            if(optics_pixel_reference(colour_layout(s.fmt,s.sel),b,q)) c=MTLClearColorMake(q[0]/255.0,q[1]/255.0,q[2]/255.0,q[3]/255.0);
        }
        static unsigned shown; if(optics_trace() && shown++<8) NSLog(@"optics-frontend: fast clear applied to %#llx: planes %u colour %08x depth %g",(unsigned long long)s.gpu,s.clear,color,depth);
        if(!retry([&]{return owner.on([&]{return [renderer clear:s.target mask:(uint8_t)planes color:c depth:depth stencil:(uint8_t)depth_word error:&e];});},&e)){
            NSLog(@"optics-frontend: fast clear failed: %@",e?e.localizedDescription:@"?"); broken=true; return false;
        }
        if(!touch(s)) return false; if(s.clear&1) invalidate(s.start,s.end); s.clear=0; return true;
    }
    bool apply_clear(Surface &s) {
        return apply_clear(s,optics_frontend_register(dev,0x4e14),optics_frontend_register(dev,0x4f28),optics_frontend_register(dev,0x4f10));
    }
    bool load_depth(Depth &z,uint32_t first,uint32_t end) {
        if(first==end)return true;
        unsigned bpp=optics_depth_bytes(z.format);size_t n=(size_t)z.width*(end-first)*bpp;
        std::vector<uint8_t> bytes(n);busy=true;
        int ok=optics_frontend_gpu_read(dev,z.zb+(uint64_t)first*z.width*bpp,bytes.data(),n);busy=false;
        NSError *e=nil;
        if(!ok||!retry([&]{return owner.on([&]{return [renderer loadDepth:z.depth y:first height:end-first bytes:bytes.data() format:z.format error:&e];});},&e)){
            NSLog(@"optics-frontend: depth import failed: %@",e);broken=true;return false;
        }return true;
    }
    /* One lazy export per dirty version, not per CPU read or end of pass. */
    bool write_depth(Depth &z) {
        if(!z.linear_io)return true;
        if(z.clear){
            OpticsDraw state={};state.depth_clear_word=optics_frontend_register(dev,0x4f28);state.depth_clear_format=optics_frontend_register(dev,0x4f10);
            if((state.depth_clear_format&15)!=z.format){broken=true;return false;}
            const char *why=nullptr;int code=OPTICS_EXEC_FAILED;if(!apply_depth_clear(z,&state,&why,&code))return false;
        }
        if(!z.dirty)return true;
        NSError *e=nil;PtR2Readback *rb=retry([&]{return owner.on([&]{return [renderer readbackDepth:z.depth height:z.extent error:&e];});},&e);
        NSData *data=nil;if(rb&&commit(&e)&&drain())data=owner.on([&]{return [renderer copyDepthReadback:rb format:z.format error:&e];});
        if(!data||data.length!=z.end-z.start){NSLog(@"optics-frontend: depth export failed: %@",e);broken=true;return false;}
        unwatch(z.watch);
        if(pt_r2_io_cpu_write(io(),z.start,data.bytes,data.length,0)!=PT_R2_IO_OK||!watch(z.watch,z.start,z.end-z.start)){
            broken=true;return false;
        }
        invalidate(z.start,z.end);z.dirty=false;stats_writebacks++;return true;
    }
    int materialize(uint64_t start,uint64_t end,int write) {
        if(busy) return 0;
        if(write) invalidate(start,end);
        for(size_t i=0;i<depths.size();) {   /* depth is not exported (DEGRADED): reads see VRAM, writes drop the GPU copy */
            if(depths[i].start<end && start<depths[i].end) {
                if(depths[i].linear_io&&!write_depth(depths[i]))return -1;
                if(write){ unbind(depths[i].depth); unwatch(depths[i].watch); depths.erase(depths.begin()+i); depth_drops++; continue; }
                if(!depths[i].linear_io&&!depth_reads_refused++) NSLog(@"optics-frontend: CPU read of a GPU depth buffer at %#llx: depth export not implemented, VRAM bytes served",(unsigned long long)depths[i].zb);
            }
            i++;
        }
        for(size_t i=0;i<owned.size();) {
            if(owned[i].start<end && start<owned[i].end){
                static int rate=-1; static unsigned shown; if(rate<0) rate=getenv("OPTICS_RATE") && atoi(getenv("OPTICS_RATE"));   /* OPTICS_RATE: the first write-backs */
                if(rate && owned[i].dirty && shown<24 && !(stats_writebacks%64)){ shown++; NSLog(@"optics-frontend: write-back #%llu of %#llx %ux%u (%u bytes/pixel, VRAM %#llx-%#llx) for %s [%#llx,%#llx)",
                    (unsigned long long)stats_writebacks,(unsigned long long)owned[i].gpu,owned[i].width,owned[i].height,owned[i].bpp,(unsigned long long)owned[i].start,(unsigned long long)owned[i].end,write?"a write":"a read",(unsigned long long)start,(unsigned long long)end); }
                bool ok;
                @autoreleasepool { ok=apply_clear(owned[i]) && write_back(owned[i]); }
                if(!ok) return -1;   /* VRAM is not current: the surface keeps the only current version */
                if(write || !owned[i].watch.id){forget(owned[i]);owned.erase(owned.begin()+i);continue;}   /* a clean copy must stay watched */
            }
            i++;
        }
        return 0;
    }
    /* Commit policy (diagnostic switches). Commit ends the render pass; the cost of reload/store
     * is not isolated by summed GPU command-buffer intervals, which can also include queueing and waits.
     * OPTICS_COMMIT_LAZY=1: a completion fence commits only while the GPU is idle; otherwise the batch is committed when the
     * GPU finishes the previous one (poll). OPTICS_COMMIT_DRAWS=N: draws per batch before a forced commit (default 512). */
    static int lazy() { static int v=-1; if(v<0) v=getenv("OPTICS_COMMIT_LAZY") && atoi(getenv("OPTICS_COMMIT_LAZY")); return v; }
    static unsigned batch_draws() { static unsigned v; if(!v){ const char *s=getenv("OPTICS_COMMIT_DRAWS"); v=s && atoi(s)>0 ? (unsigned)atoi(s) : 512; } return v; }
    void commit_if_idle() { if(lazy() && renderer && !broken && finished>=committed){ NSError *e=nil; commit(&e); submit_jobs(); } }
    /* The frontend asks after a fence over the query's draws completed (executor.h). */
    int query(uint32_t q,uint64_t *n) { *n=renderer?owner.on([&]{return [renderer queryCount:q];}):0; return broken?-1:0; }
    int fence() {   /* never blocks (OPTICS_FENCE_WAIT=1: completes synchronously, for the instant-completion replay oracle) */
        if(broken) return OPTICS_FENCE_FAILED;
        if(!renderer) return OPTICS_FENCE_DONE;
        static int wait=-1; if(wait<0) wait=getenv("OPTICS_FENCE_WAIT") && atoi(getenv("OPTICS_FENCE_WAIT"));
        poll(); NSError *e=nil;
        /* jobs are submitted to the timeline only once every one of them is committed (or nothing is batched) */
        if(wait || !lazy() || finished>=committed){ commit(&e); submit_jobs(); }   /* lazy and GPU busy: commit_if_idle later */
        poll();
        if(wait) (void)drain();
        if(broken) return OPTICS_FENCE_FAILED;
        return finished>=committed?OPTICS_FENCE_DONE:OPTICS_FENCE_PENDING;
    }
    /* Development: OPTICS_SURFACE_DUMP=DIR, every few guest seconds the large colour targets seen so far are written as
     * half-resolution PPM files (materialized first), to see what each surface holds independently of the scanout. */
    void dump(const char *dir,unsigned tag) {
        for(auto &b:big) {
            uint64_t start; if(!optics_frontend_vram_offset(dev,b.gpu,&start)) continue;
            if(materialize(start,start+(uint64_t)b.w*4*b.h,0)) continue;
            std::vector<uint8_t> px((size_t)b.w*b.h*4); busy=true; int ok=optics_frontend_gpu_read(dev,b.gpu,px.data(),px.size()); busy=false;
            if(!ok) continue;
            char name[1024]; snprintf(name,sizeof name,"%s/surface-%02u-%llx.ppm",dir,tag,(unsigned long long)b.gpu);
            FILE *f=fopen(name,"wb"); if(!f) continue;
            fprintf(f,"P6\n%u %u\n255\n",b.w/2,b.h/2);
            for(uint32_t y=0;y+1<b.h;y+=2) for(uint32_t x=0;x+1<b.w;x+=2){const uint8_t *p=&px[((size_t)y*b.w+x)*4];uint8_t rgb[3]={p[2],p[1],p[0]};fwrite(rgb,1,3,f);}
            fclose(f);
        }
    }
    int clear(uint64_t gpu,int plane) {
        bool done=false;
        if(plane==2){ for(auto &z:depths) if(z.zb==gpu){ z.clear=1; done=true; } }   /* every storage of this depth buffer */
        else for(auto &s:owned) if(s.gpu==gpu){ s.clear|=1; done=true; }
        if(done) return 0;
        for(auto &p:pending) if(p.gpu==gpu && p.plane==plane) return 0;
        if(pending.size()>=16) return 1;
        pending.push_back({gpu,plane});
        return 0;
    }
    /* A guest colour buffer becomes GPU-owned: its bytes are uploaded into a new target (depth cleared). Returns the
     * entry in `owned` (valid until the list changes) or nullptr with the result in *code and the reason in *why. */
    Surface *import(uint64_t gpu,uint32_t W,uint32_t H,uint8_t fmt,uint8_t sel,const char **why,int *code) {
        NSError *e=nil; uint8_t bpp=(uint8_t)colour_bytes(fmt); size_t row=(size_t)W*bpp; uint64_t start; OpticsPixelLayout l=colour_layout(fmt,sel);
        if(!optics_frontend_vram_offset(dev,gpu,&start)){*code=fail(why,"colour target outside VRAM",nil);return nullptr;}
        if(materialize(start,start+row*H,1)){*code=failhard(why,"export before a colour import",nil);return nullptr;}   /* other owned views go back and are dropped */
        std::vector<uint8_t> rows(row*H);
        busy=true; int ok=optics_frontend_gpu_read(dev,gpu,rows.data(),rows.size()); busy=false;
        if(!ok){*code=fail(why,"colour target outside GPU memory",nil);return nullptr;}
        if(bpp==8)for(size_t i=0;i<(size_t)W*H*4;i++)rows[i]=rows[2*i+1];   /* 16-bit LE channels: high bytes, in place */
        PtR2Texture *current=retry([&]{return owner.on([&]{return [renderer textureWidth:W height:H bytes:rows.data() rowBytes:(size_t)W*(bpp==8?4:bpp)
            layout:l error:&e];});},&e);
        PtR2Target *target=current?owner.on([&]{return [renderer targetWidth:W height:H samples:(PtR2MsaaInput){} error:&e];}):nil;
        if(!target && current && e.code==2) {   /* resident budget: drop decoded textures and other owned surfaces, retry */
            e=nil; drop_cache();
            while(!owned.empty()){ if(!write_back(owned.front())){*code=failhard(why,"export for resident budget",nil);return nullptr;} forget(owned.front()); owned.erase(owned.begin()); }
            NSError *c=nil; commit(&c); if(!drain()){*code=failhard(why,"GPU batch",nil);return nullptr;}
            target=owner.on([&]{return [renderer targetWidth:W height:H samples:(PtR2MsaaInput){} error:&e];});
        }
        if(!target || !retry([&]{return owner.on([&]{return [renderer load:target from:current error:&e];});},&e)){*code=failhard(why,"target import",e);return nullptr;}
        uint8_t clear=0;
        for(size_t i=0;i<pending.size();) if(pending[i].plane==1 && pending[i].gpu==gpu){clear=1;pending.erase(pending.begin()+i);} else i++;
        if(owned.size()>=16){ if(!write_back(owned.front())){*code=failhard(why,"export of an evicted surface",nil);return nullptr;} forget(owned.front()); owned.erase(owned.begin()); }
        Surface s={start,start+row*H,gpu,W,H,target,false,clear,nil,0,next_version++,PtR2IoToken{},PtR2IoToken{},nil,bpp,fmt,sel};
        if(!watch(s.watch,s.start,s.end-s.start)){*code=failhard(why,"VRAM watch of an imported surface",nil);return nullptr;}
        owned.push_back(s); stats_imports++;
        static uint64_t seen[32]; static unsigned nseen; bool known=false;   /* development trace: distinct colour targets */
        for(unsigned i=0;i<nseen;i++) known|=seen[i]==gpu;
        if(optics_trace() && !known && nseen<32){seen[nseen++]=gpu;NSLog(@"optics-frontend: colour target %#llx %ux%u",(unsigned long long)gpu,W,H);}
        bool listed=false; for(auto &b:big) listed|=b.gpu==gpu;
        if(!listed && fmt==6 && W*H>=512*384 && big.size()<8) big.push_back({gpu,W,H});
        return &owned.back();
    }
    /* RB3D_AARESOLVE draw: the covered pixels are copied (1 sample) from the colour buffer into the resolve buffer, on the
     * GPU between owned targets (the destination is imported when not owned). The covered area is the draw's
     * window-space bounding box (the swap is one point sprite over the drawable), clipped to the scissor and both buffers. */
    int resolve(const OpticsDraw *d,Surface *surface,uint32_t W,const char **why) {
        NSError *e=nil;
        if(!apply_clear(*surface,d->color_clear,d->depth_clear_word,d->depth_clear_format)) return failhard(why,"fast clear",nil);
        std::vector<OpticsVertexOut> vo(d->vertices?d->vertices:1);
        if(!d->vertices || !optics_draw_vertices(d,vo.data(),why)) return d->vertices?OPTICS_EXEC_REFUSED:OPTICS_EXEC_DONE;
        float x0=1e9f,y0=1e9f,x1=-1e9f,y1=-1e9f;
        for(uint32_t v=0;v<d->vertices;v++){x0=std::min(x0,vo[v].window[0]);y0=std::min(y0,vo[v].window[1]);x1=std::max(x1,vo[v].window[0]);y1=std::max(y1,vo[v].window[1]);}
        int32_t rx0=std::max<int32_t>(d->scissor[0],(int32_t)lrintf(x0)),ry0=std::max<int32_t>(d->scissor[1],(int32_t)lrintf(y0));
        int32_t rx1=std::min<int32_t>({d->scissor[2],(int32_t)lrintf(x1),(int32_t)W,(int32_t)d->resolve_pitch});
        int32_t ry1=std::min<int32_t>({d->scissor[3],(int32_t)lrintf(y1),(int32_t)surface->height});
        if(rx0<0) rx0=0; if(ry0<0) ry0=0;
        if(rx1<=rx0 || ry1<=ry0) return OPTICS_EXEC_DONE;
        uint64_t src_gpu=surface->gpu; uint32_t DW=d->resolve_pitch; uint8_t fmt=surface->fmt;
        Surface *dst=nullptr;
        for(auto &s:owned) if(s.gpu==d->resolve_offset && s.width==DW && s.fmt==fmt && s.height>=(uint32_t)ry1){dst=&s;break;}
        int code=OPTICS_EXEC_REFUSED;
        if(!dst && !(dst=import(d->resolve_offset,DW,(uint32_t)ry1,fmt,surface->sel,why,&code))) return code;
        uint64_t dst_gpu=dst->gpu; surface=nullptr; dst=nullptr;   /* the import may have moved or evicted entries */
        for(auto &s:owned){ if(s.gpu==src_gpu && s.width==W && s.fmt==fmt) surface=&s; if(s.gpu==dst_gpu && s.width==DW && s.fmt==fmt) dst=&s; }
        if(!surface || !dst || surface==dst) return fail(why,"resolve buffers",nil);
        if(!apply_clear(*dst,d->color_clear,d->depth_clear_word,d->depth_clear_format)) return failhard(why,"fast clear",nil);
        if(!retry([&]{return owner.on([&]{return [renderer copy:surface->target x:(uint32_t)rx0 y:(uint32_t)ry0 width:(uint32_t)(rx1-rx0) height:(uint32_t)(ry1-ry0)
              to:dst->target x:(uint32_t)rx0 y:(uint32_t)ry0 error:&e];});},&e)) return failhard(why,"resolve copy",e);
        if(!touch(*dst)) return failhard(why,"VRAM ownership",nil); invalidate(dst->start,dst->end); stats_draws++;
        static unsigned shown; if(optics_trace() && shown++<6) NSLog(@"optics-frontend: AA resolve %#llx -> %#llx (pitch %u) rect %d,%d-%d,%d",(unsigned long long)src_gpu,(unsigned long long)dst_gpu,DW,rx0,ry0,rx1,ry1);
        return OPTICS_EXEC_DONE;
    }
    int draw(const OpticsDraw *d,const char **why);
    Depth *depth_for(const OpticsDraw *d,uint32_t W,uint32_t H,const char **why,int *code) {
        uint32_t pitch=optics_frontend_register(dev,0x4f24)&0x3ffc;   /* ZB_DEPTHPITCH */
        static int capacity=-1;
        if(capacity<0){const char *v=getenv("OPTICS_DEPTH_CAPACITY");capacity=v && atoi(v)!=0;}
        uint32_t layout=optics_frontend_register(dev,0x4f24);
        bool linear_io=depth_io()&&optics_depth_bytes(d->depth_clear_format&15)&&!(layout&0x1f0000);
        for(size_t i=0;depth_io()&&i<depths.size();)if(depths[i].zb==d->zb_offset&&(depths[i].layout!=layout||depths[i].format!=(d->depth_clear_format&15)||depths[i].linear_io!=linear_io)){
            if(!write_depth(depths[i])){*code=failhard(why,"depth fallback transition export",nil);return nullptr;}
            unbind(depths[i].depth);unwatch(depths[i].watch);depths.erase(depths.begin()+i);
        }else i++;
        bool use_capacity=capacity||linear_io;
        /* Unsupported layouts keep the existing GPU-only depth path. */
        if(use_capacity) {
            uint32_t width=pitch?pitch:W,format=d->depth_clear_format&15;
            if(width<W || width>8192 || H>8192){*code=fail(why,"depth pitch or extent",nil);return nullptr;}
            for(auto &z:depths) if(z.zb==d->zb_offset && z.pitch==pitch && z.width==width && z.format==format && z.linear_io==linear_io && z.layout==layout) {
                NSError *e=nil;
                if(H>z.height) {
                    uint32_t height=(H+63u)&~63u;
                    PtR2Depth *next=retry([&]{return owner.on([&]{return [renderer depthWidth:width height:height error:&e];});},&e);
                    if(!next || !retry([&]{return owner.on([&]{return [renderer copyDepth:z.depth to:next error:&e];});},&e)) {
                        *code=failhard(why,"preserving depth capacity",e);return nullptr;
                    }
                    unbind(z.depth);z.depth=next;z.height=height;
                }
                if(H>z.extent) {
                    if(linear_io&&!load_depth(z,z.extent,H)){*code=failhard(why,"depth extent import",nil);return nullptr;}
                    PtR2IoToken watch_next={};uint64_t end=z.start+(uint64_t)width*H*(linear_io?optics_depth_bytes(format):4);
                    if(!watch(watch_next,z.start,end-z.start)){*code=failhard(why,"depth extent watch",nil);return nullptr;}
                    unwatch(z.watch);z.watch=watch_next;z.end=end;z.extent=H;
                }
                return apply_depth_clear(z,d,why,code)?&z:nullptr;
            }
            W=width;
        }
        for(size_t i=0;!use_capacity && i<depths.size();i++) if(depths[i].zb==d->zb_offset && depths[i].pitch==pitch && depths[i].width==W && depths[i].height==H && depths[i].layout==layout && depths[i].format==(d->depth_clear_format&15) && depths[i].linear_io==linear_io) {
            Depth z=depths[i]; depths.erase(depths.begin()+i); depths.push_back(z); return apply_depth_clear(depths.back(),d,why,code)?&depths.back():nullptr;
        }
        uint32_t storage_height=use_capacity?(H+63u)&~63u:H;
        NSError *e=nil; PtR2Depth *storage=retry([&]{return owner.on([&]{return [renderer depthWidth:W height:storage_height error:&e];});},&e);
        if(!storage){*code=failhard(why,"depth storage",e);return nullptr;}
        uint64_t start=0;if(!optics_frontend_vram_offset(dev,d->zb_offset,&start)&&linear_io){
            *code=fail(why,"depth I/O target outside VRAM",nil);return nullptr;
        }
        if(linear_io) {
            uint64_t end=start+(uint64_t)(pitch?pitch:W)*H*optics_depth_bytes(d->depth_clear_format&15);
            for(const auto &s:owned)if(start<s.end&&s.start<end){*code=fail(why,"depth/colour storage alias needs a layout transition",nil);return nullptr;}
            for(size_t i=0;i<depths.size();)if(start<depths[i].end&&depths[i].start<end) {
                if(!write_depth(depths[i])){*code=failhard(why,"depth layout transition export",nil);return nullptr;}
                unbind(depths[i].depth);unwatch(depths[i].watch);depths.erase(depths.begin()+i);
            }else i++;
        }
        if(depths.size()>=8){ if(!write_depth(depths.front())){*code=failhard(why,"depth eviction export",nil);return nullptr;}
            unbind(depths.front().depth); unwatch(depths.front().watch); depths.erase(depths.begin()); }
        unsigned bpp=linear_io?optics_depth_bytes(d->depth_clear_format&15):4;
        Depth z={d->zb_offset,start,start+(uint64_t)(pitch?pitch:W)*H*bpp,pitch,W,storage_height,storage,0,PtR2IoToken{}};
        z.format=d->depth_clear_format&15;z.linear_io=linear_io;z.layout=layout;z.extent=H;
        if(linear_io&&!load_depth(z,0,H)){*code=failhard(why,"depth import",nil);return nullptr;}
        if(!watch(z.watch,z.start,z.end-z.start) && use_capacity){*code=failhard(why,"depth extent watch",nil);return nullptr;}
        static unsigned shown; if(optics_trace() && shown++<16) NSLog(@"optics-frontend: depth storage %#llx pitch %u %ux%u (%zu live)",(unsigned long long)z.zb,pitch,W,H,depths.size()+1);
        for(size_t i=0;i<pending.size();) if(pending[i].plane==2 && pending[i].gpu==d->zb_offset){z.clear=1;pending.erase(pending.begin()+i);} else i++;
        depths.push_back(z); return apply_depth_clear(depths.back(),d,why,code)?&depths.back():nullptr;
    }
    bool apply_depth_clear(Depth &z,const OpticsDraw *d,const char **why,int *code) {   /* ZMASK clear: values in force now */
        if(!z.clear) return true;
        unsigned planes=optics_draw_clear_mask(2,d->depth_clear_format)&6; NSError *e=nil;
        if(!retry([&]{return owner.on([&]{return [renderer clearDepth:z.depth mask:(uint8_t)planes depth:optics_draw_depth_clear(d->depth_clear_word,d->depth_clear_format)
              stencil:(uint8_t)d->depth_clear_word error:&e];});},&e)){*code=failhard(why,"depth fast clear",e);return false;}
        z.clear=0;z.dirty=true; return true;
    }
    void unbind(PtR2Depth *z) {
        for(auto &s:owned) if(s.bound==z){ NSError *e=nil; owner.on([&]{return [renderer target:s.target depth:nil error:&e];}); s.bound=nil; }
    }
    /* Native scanout: an immutable GPU copy of the owned surface that holds the scanout, published after it completed. */
    int frame(uint64_t start,uint32_t w,uint32_t h,uint32_t line,void **handle,void **texture) {
        if(broken) return OPTICS_FRAME_FAILED;
        if(!renderer) return OPTICS_FRAME_NONE;
        if(!scan_valid || scan_start!=start || scan_w!=w || scan_h!=h || scan_line!=line) {
            /* The batch retains its old snapshot until completion; it must not be labelled as the new mode. */
            snap=nil; published_version=0; scan_valid=true;
            scan_start=start; scan_w=w; scan_h=h; scan_line=line;
        }
        if(snap) {
            NSError *e=nil; PtR2Frame *f=owner.on([&]{return [renderer frame:snap error:&e];});
            if(!f && e.code==2){ poll(); return broken?OPTICS_FRAME_FAILED:OPTICS_FRAME_PENDING; }   /* copy not completed yet */
            snap=nil;
            if(!f){ NSLog(@"optics-frontend: scanout frame failed: %@",e.localizedDescription); broken=true; return OPTICS_FRAME_FAILED; }
            published_version=snap_version; *texture=(__bridge void *)f.texture; *handle=(__bridge_retained void *)f; return OPTICS_FRAME_READY;
        }
        Surface *s=nullptr;
        for(auto &x:owned) if(x.start==start && x.fmt==6 && (!x.sel || x.sel==0x1b) && (uint64_t)x.width*4==line && x.height>=h && x.width>=w){s=&x;break;}   /* the frame shader reads B, G, R, A */
        if(!s) return OPTICS_FRAME_NONE;   /* not GPU-owned (or another geometry): the frontend copies VRAM after materialize */
        if(s->version==published_version) return OPTICS_FRAME_SAME;
        if(!apply_clear(*s)) return OPTICS_FRAME_FAILED;
        NSError *e=nil; uint64_t version=s->version; PtR2Target *target=s->target;
        snap=retry([&]{return owner.on([&]{return [renderer snapshot:target resolveControl:1 error:&e];});},&e);
        if(!snap){ NSLog(@"optics-frontend: scanout snapshot failed: %@",e.localizedDescription); broken=true; return OPTICS_FRAME_FAILED; }
        snap_version=version; commit(&e); return OPTICS_FRAME_PENDING;
    }
    int reset() {   /* device reset: VRAM gets every GPU result, then the old run's resources and epoch end */
        int r=0;
        for(auto &s:owned){ if(!(apply_clear(s) && write_back(s))) r=-1; forget(s); }
        owned.clear(); for(auto &z:depths){if(!write_depth(z))r=-1;unwatch(z.watch);} depths.clear(); drop_cache(); pending.clear(); snap=nil; programs.clear(); last_valid=false; last_program=nil; tokens.clear();
        published_version=0; scan_valid=false;
        if(renderer) { NSError *e=nil; if(!owner.on([&]{return [renderer reset:&e];})){ NSLog(@"optics-frontend: renderer reset: %@",e.localizedDescription); r=-1; } }
        owner.clearError();committed=finished=0; since_commit=0; broken=false; return r;
    }
    void close() {
        owner.closeAdmission();owner.barrier();
        if(!renderer) return;
        NSError *e=nil; commit(&e); (void)drain();
        NSDictionary *g=owner.on([&]{return [renderer diagnostics];});
        NSLog(@"optics-frontend: executor draws %llu imports %llu writebacks %llu commits %llu, textures %llu uploaded %llu reused (%llu GART by content); renderer batches %@, GPU %.3f s, "
              "submit->callback mean %@ ns max %@ ns, passes %@, programs %@ (MSL libraries %@; compiling %.1f s, of it MSL %.1f s), backpressure %@; depth storages %zu live, %llu dropped by CPU writes, %llu CPU reads of GPU depth served from VRAM",
              (unsigned long long)stats_draws,(unsigned long long)stats_imports,(unsigned long long)stats_writebacks,(unsigned long long)stats_commits,
              (unsigned long long)stats_texture_uploads,(unsigned long long)stats_texture_hits,(unsigned long long)stats_gart_hits,g[@"epochCompletedBatches"],[g[@"epochGPUSeconds"] doubleValue],
              g[@"epochMeanSubmitToCallbackNS"],g[@"epochMaxSubmitToCallbackNS"],g[@"renderPasses"],g[@"programsCompiled"],g[@"librariesCompiled"],[g[@"compileNS"] doubleValue]/1e9,[g[@"libraryNS"] doubleValue]/1e9,g[@"backpressure"],
              depths.size(),(unsigned long long)depth_drops,(unsigned long long)depth_reads_refused);
        NSLog(@"optics-frontend: full GPU VTE draws %llu",(unsigned long long)stats_vte2_draws);
        NSLog(@"optics-frontend: batch latency: submit->GPU start mean %@ ns, GPU end->callback mean %@ ns",g[@"epochMeanSubmitToStartNS"],g[@"epochMeanEndToCallbackNS"]);
        NSLog(@"optics-frontend: GPU raw vertex draws %llu, input snapshot bytes %llu",(unsigned long long)stats_raw_draws,(unsigned long long)stats_raw_bytes);
        for(auto &s:owned) forget(s);
        owned.clear(); for(auto &z:depths) unwatch(z.watch); depths.clear(); drop_cache(); pending.clear(); snap=nil; programs.clear(); last_valid=false; last_program=nil; tokens.clear();
        owner.on([&]{return [renderer close];}); renderer=nil;
    }
};

int Exec::draw(const OpticsDraw *d,const char **why) {
    @autoreleasepool {
        NSError *e=nil;
        if(broken) return failhard(why,"executor after a GPU failure",nil);
        if(!ensure(&e)) return fail(why,"Metal renderer unavailable",e);
        if(!d->indices || (d->cull==3 && cull_mode())) return OPTICS_EXEC_DONE;   /* both faces culled: nothing to draw */
        uint32_t W=d->width,H=d->height; uint64_t start; uint8_t cfmt=d->cb_format?d->cb_format:6;   /* hand-built draws leave cb_format 0 */
        if(!W || !H || W>8192 || H>8192) return fail(why,"target size",nil);
        if(!optics_frontend_vram_offset(dev,d->cb_offset,&start)) return fail(why,"colour target outside VRAM",nil);
        /* 8 varyings (the builder's limit) fit both vertex forms: 8 user varyings in PtVary, the pass-through
         * route's position + 8 raw attributes (16 slots). Doom 3's interaction draws use all 8. */
        if(d->varyings>OPTICS_DRAW_VARYINGS) return fail(why,"more than 8 varyings",nil);
        /* 1. The GPU-owned surface for this colour buffer, or a fresh import of the guest bytes. */
        auto find=[&]()->Surface *{ for(auto &s:owned) if(s.gpu==d->cb_offset && s.width==W && s.fmt==cfmt && s.height>=H) return &s; return nullptr; };
        Surface *surface=find();
        /* A point sprite over the drawable (Tiger's GL draws one so, VTE scales without offsets) has the scissor's height,
         * a row past Doom 3's 640x480 buffer; its corners end the surface instead, as draw.c ends a viewport-transformed
         * draw's at the viewport (a taller surface covered the next VRAM object). Only when no surface fits the scissor, so
         * there is no CPU vertex pass per sprite draw; Half-Life's frame rate showed no effect beyond its run-to-run spread. */
        if(!surface && d->sprite && d->vertices && d->vertices<=64) {
            std::vector<OpticsVertexOut> vo(d->vertices); const char *r2=nullptr;
            if(optics_draw_vertices(d,vo.data(),&r2)) { float m=0; for(auto &v:vo) m=std::max(m,v.window[1]);
                int32_t b=(int32_t)ceilf(m-0.5f); if(b>d->scissor[1] && (uint32_t)b<H){ H=(uint32_t)b; surface=find(); } }
        }
        int code=OPTICS_EXEC_REFUSED;
        {   /* development, OPTICS_SURFACE_LOG=1: the draw that makes a colour surface (its height comes from it) */
            static int on=-1; static unsigned shown; if(on<0){const char *v=getenv("OPTICS_SURFACE_LOG"); on=v&&atoi(v)!=0;}
            if(on && !surface && shown<64){ shown++; NSLog(@"optics-surface: %#x %ux%u from scissor %d,%d-%d,%d vte %#x vp %g %g %g %g resolve %u sprite %u vertices %u",
                d->cb_offset,W,H,d->scissor[0],d->scissor[1],d->scissor[2],d->scissor[3],d->vte,d->viewport[0],d->viewport[1],d->viewport[2],d->viewport[3],d->resolve,d->sprite,d->vertices); }
        }
        uint8_t sel=d->color_mask?d->cb_select:0;   /* a draw that writes no colour may carry another context's output selects */
        if(!surface && !(surface=import(d->cb_offset,W,H,cfmt,sel,why,&code))) return code;
        if(d->color_mask) surface->sel=sel;
        if(d->resolve) return resolve(d,surface,W,why);
        /* Depth/stencil storage of a draw that tests or writes them (ZB_DEPTHOFFSET with both off may be another
         * context's): bound to this target; a draw without depth keeps the binding (its depth state touches nothing). */
        if(d->depth_enable || d->stencil_enable) {
            Depth *z=depth_for(d,W,surface->height,why,&code); if(!z) return code;
            if(surface->bound!=z->depth) {
                if(!owner.on([&]{return [renderer target:surface->target depth:z->depth error:&e];})) return failhard(why,"depth binding",e);
                surface->bound=z->depth;
            }
        }
        /* 2. Textures, decoded on the GPU from their guest bytes (owned sources are written back first). */
        unsigned top=0; for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) if(d->texture_mask>>u&1) top=u+1;
        NSMutableArray *textures=top?[NSMutableArray arrayWithCapacity:top]:nil;   /* PtR2Texture, NSNull placeholders until filled; none: no array per draw */
        for(unsigned u=0;u<top;u++) {
            const OpticsTexture *t=&d->texture[u]; PtR2Levels lv; size_t n=texture_levels(t,&lv); uint64_t to;
            if(!(d->texture_mask>>u&1)) { [textures addObject:[NSNull null]]; continue; }
            if(optics_frontend_vram_offset(dev,t->offset,&to)) {
                /* A GPU-owned ARGB8888 (ARGB16161616) surface sampled whole as W8Z8Y8X8 (W16Z16Y16X16): a swizzled view of its target, no readback.
                 * Its top-left part (the window server samples a window's visible width of its padded backing
                 * store: 892 of 896, 60 of 64): a GPU copy of that rectangle, also without a readback. */
                Surface *src=nullptr;
                for(auto &s:owned) if(!t->cube && s.gpu==t->offset && ((s.fmt==6 && t->format==0xc) || (s.fmt==10 && t->format==0xe)) && t->pitch==s.width && t->width<=s.width && t->height<=s.height) {src=&s;break;}
                if(src && src->target!=surface->target) {
                    uint8_t c[4]; uint32_t k=1u<<31;   /* texel component X..W = byte 0..3 = its output select C0..C3 */
                    for(int q=0;q<4;q++){c[q]=t->select[q]<4?(uint8_t)colour_channel(src->sel,t->select[q]):t->select[q];k|=(uint32_t)c[q]<<(8*q);}
                    if(!apply_clear(*src,d->color_clear,d->depth_clear_word,d->depth_clear_format)) return failhard(why,"fast clear",nil);
                    if(t->width==src->width && t->height==src->height) {
                        if(!src->view || src->view_key!=k){ src->view=owner.on([&]{return [renderer view:src->target component:c error:&e];}); src->view_key=k; }
                        if(src->view){ [textures addObject:src->view]; stats_texture_hits++; continue; }
                    } else {
                        uint64_t rk=(uint64_t)k<<32|t->width<<16|t->height;
                        if(!src->region || src->region_key!=rk || src->region_version!=src->version) {
                            Surface *s=src; src->region=retry([&]{return owner.on([&]{return [renderer copyView:s->target width:t->width height:t->height component:c error:&e];});},&e);
                            src->region_key=rk; src->region_version=src->version;
                        }
                        if(src->region){ [textures addObject:src->region]; stats_texture_hits++; continue; }
                    }
                    e=nil;
                }
                static std::vector<std::pair<uint64_t,uint64_t>> seen;   /* development trace: render-to-texture, first use per pair */
                if(optics_trace()) for(auto &s:owned) if(s.start<to+n && to<s.end && seen.size()<64 && std::find(seen.begin(),seen.end(),std::make_pair(s.gpu,(uint64_t)d->cb_offset))==seen.end()) {
                    seen.push_back({s.gpu,(uint64_t)d->cb_offset});
                    NSLog(@"optics-frontend: texture %#x %ux%u pitch %u fmt %#x sel %u%u%u%u from owned %#x (%ux%u) -> target %#x blend %08x mask %x",t->offset,t->width,t->height,t->pitch,
                        t->format,t->select[0],t->select[1],t->select[2],t->select[3],(unsigned)s.gpu,s.width,s.height,d->cb_offset,d->blend,d->color_mask);
                }
                if(materialize(to,to+n,0)) return failhard(why,"export of a sampled surface",nil);
                surface=nullptr; for(auto &s:owned) if(s.gpu==d->cb_offset && s.width==W && s.fmt==cfmt) surface=&s;
                if(!surface) return fail(why,"colour target was the sampled texture (feedback loop)",nil);
            }
            if(optics_trace() && (t->width*t->height>=512*384 || (d->cb_offset!=0 && W==1024 && H>=768))) {   /* development trace: large textures, and every
                                                                                         texture drawn into a full-screen non-scanout target, first per pair */
                static std::vector<std::pair<uint64_t,uint64_t>> seen_big;
                if(seen_big.size()<64 && std::find(seen_big.begin(),seen_big.end(),std::make_pair((uint64_t)t->offset,(uint64_t)d->cb_offset))==seen_big.end()) {
                    seen_big.push_back({t->offset,d->cb_offset});
                    NSLog(@"optics-frontend: large texture %#x %ux%u pitch %u fmt %#x -> target %#x (%ux%u) blend %08x",t->offset,t->width,t->height,t->pitch,t->format,d->cb_offset,W,H,d->blend);
                }
            }
            uint32_t key[10]={(uint32_t)t->offset,t->width,t->height,t->pitch,t->format,
                (uint32_t)t->select[0]|t->select[1]<<8|t->select[2]<<16|(uint32_t)t->select[3]<<24,(uint32_t)t->linear|(uint32_t)t->levels<<8,
                (uint32_t)t->layout|(uint32_t)t->swap<<8|(uint32_t)t->cube<<16};
            uint64_t vram_off; int in_vram=optics_frontend_vram_offset(dev,t->offset,&vram_off);
            PtR2Texture *x=nil;
            TexKey tk; memcpy(tk.k,key,sizeof key);
            if(in_vram){ auto hit=cache_index.find(tk); if(hit!=cache_index.end()) x=hit->second; }
            if(x) stats_texture_hits++;
            else {
                uint64_t u0=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                std::vector<uint8_t> texels(n);
                busy=true; int ok=optics_frontend_gpu_read(dev,t->offset,texels.data(),n); busy=false;
                if(!ok) return fail(why,"texture outside GPU memory",nil);
                if(!in_vram) {   /* the same GART bytes as a kept upload: reuse it */
                    uint64_t h=content_hash(texels.data(),n); tk.k[8]=(uint32_t)h; tk.k[9]=(uint32_t)(h>>32);
                    auto hit=gart.find(tk); if(hit!=gart.end()){ x=hit->second; stats_texture_hits++; stats_gart_hits++; [textures addObject:x]; continue; }
                }
                optics_texture_unswap(t,texels.data(),n);
                {   /* development, OPTICS_DXT_DUMP=1: raw DXT block bytes (to check a decode against the guest's data) */
                    static int on=-1; static unsigned shown; if(on<0){const char *v=getenv("OPTICS_DXT_DUMP"); on=v&&atoi(v)!=0;}
                    if(on && t->layout>=OPTICS_PIXEL_DXT1 && shown<12 && n>=48){ shown++; NSMutableString *h=[NSMutableString string];
                        for(unsigned i=0;i<48;i++)[h appendFormat:@"%02x%s",texels[i],i%8==7?" ":""];
                        NSLog(@"optics-dxt: %#x fmt %#x %ux%u pitch %u levels %u swap %u sel %u%u%u%u: %@",t->offset,t->format,t->width,t->height,t->pitch,t->levels,t->swap,
                            t->select[0],t->select[1],t->select[2],t->select[3],h); }
                }
                size_t m=n;
                if(t->format==0xe||t->format==0x1){   /* W16Z16Y16X16 and X16 LE: high bytes, in place; every offset and row halves */
                    for(size_t i=0;i<n/2;i++)texels[i]=texels[2*i+1];
                    m=n/2; for(unsigned k=0;k<lv.levels;k++){lv.offset[k]/=2;lv.row[k]/=2;lv.face[k]/=2;}
                }
                OpticsPixelLayout l={t->layout,{t->select[0],t->select[1],t->select[2],t->select[3]}};
                x=retry([&]{return owner.on([&]{return [renderer textureWidth:t->width height:t->height faces:t->cube?6u:1u levels:&lv bytes:texels.data() length:m layout:l error:&e];});},&e);
                if(!x) return failhard(why,"texture upload",e);
                stats_texture_uploads++; upload_ns+=clock_gettime_nsec_np(CLOCK_UPTIME_RAW)-u0;
                upload_last[0]=t->width; upload_last[1]=t->height; upload_last[2]=t->format; upload_last[3]=(uint32_t)n; upload_last[4]=(uint32_t)in_vram;
                if(in_vram) {   /* kept while its source pages are watched: a CPU write (trap or fault) drops it */
                    while(!cache.empty() && (cache.size()>=CACHE_ENTRIES || cache_bytes+n>CACHE_LIMIT)) uncache(0);
                    Cached c; c.start=vram_off; c.end=vram_off+n; memcpy(c.key,key,sizeof key); c.texture=x; c.watch=PtR2IoToken{};
                    if(watch(c.watch,c.start,n)){ cache.push_back(c); cache_adds++; cache_index[tk]=x; cache_bytes+=n; cache_lo=std::min(cache_lo,c.start); cache_hi=std::max(cache_hi,c.end); }
                } else {   /* budgeted by decoded RGBA8 size */
                    size_t rgba=0; for(unsigned k=0;k<lv.levels;k++) rgba+=(size_t)std::max(1u,t->width>>k)*std::max(1u,t->height>>k)*4*(t->cube?6:1);
                    while(!gart_order.empty() && (gart_order.size()>=GART_ENTRIES || gart_bytes+rgba>GART_LIMIT)){ gart.erase(gart_order.front().first); gart_bytes-=gart_order.front().second; gart_order.pop_front(); }
                    gart[tk]=x; gart_order.push_back({tk,rgba}); gart_bytes+=rgba;
                }
            }
            [textures addObject:x];
        }
        for(NSUInteger i=0;i<textures.count;i++) if([textures[i] isKindOfClass:NSNull.class]) {   /* placeholder keeps unit indices */
            for(id t in textures) if(![t isKindOfClass:NSNull.class]) {textures[i]=t;break;}
        }
        if(!apply_clear(*surface,d->color_clear,d->depth_clear_word,d->depth_clear_format)) return failhard(why,"fast clear",nil);
        {   /* development: OPTICS_DRAW_PROBE=N logs N sampled draws into large targets (state and the first vertices), one in
             * OPTICS_DRAW_PROBE_EVERY (7919) */
            static long probe=-1,every=7919,format=-1,query=0; static unsigned long count;
            if(probe<0){const char *v=getenv("OPTICS_DRAW_PROBE");probe=v?atol(v):0;v=getenv("OPTICS_DRAW_PROBE_EVERY");if(v&&atol(v)>0)every=atol(v);
                v=getenv("OPTICS_DRAW_PROBE_FORMAT");if(v)format=strtol(v,NULL,0);   /* only draws sampling this TX_FORMAT */
                v=getenv("OPTICS_DRAW_PROBE_QUERY");query=v&&atoi(v)!=0;}            /* only draws inside an occlusion query */
            bool wanted=format<0; for(unsigned u=0;u<OPTICS_DRAW_TEXTURES && !wanted;u++) wanted=(d->texture_mask>>u&1) && d->texture[u].format==format;
            if(query && !d->query) wanted=false;
            if(probe>0 && W>=512 && wanted && !(++count%every)) { probe--;
                std::vector<OpticsVertexOut> vo(d->vertices?d->vertices:1); const char *r2=nullptr; int ok=optics_draw_vertices(d,vo.data(),&r2);
                NSMutableString *t=[NSMutableString stringWithFormat:@"optics-frontend: probe #%lu target %#x %ux%u scissor %d,%d-%d,%d vte %#x vp %g %g %g %g %g %g depth %u/%u/%u stencil %u blend %08x mask %x tex %#x",
                    count,d->cb_offset,W,surface->height,d->scissor[0],d->scissor[1],d->scissor[2],d->scissor[3],d->vte,d->viewport[0],d->viewport[1],d->viewport[2],d->viewport[3],d->viewport[4],d->viewport[5],
                    d->depth_enable,d->depth_compare,d->depth_write,d->stencil_enable,d->blend,d->color_mask,d->texture_mask];
                [t appendFormat:@" stencil ref %u mask %#x/%#x front %u%u%u%u back %u%u%u%u",d->stencil_ref,d->stencil_mask,d->stencil_writemask,
                    d->stencil_op[0],d->stencil_op[1],d->stencil_op[2],d->stencil_op[3],d->stencil_back[0],d->stencil_back[1],d->stencil_back[2],d->stencil_back[3]];
                static std::vector<uint8_t> texels[OPTICS_DRAW_TEXTURES];
                for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) if(d->texture_mask>>u&1) {
                    const OpticsTexture *x=&d->texture[u]; std::vector<uint8_t> &bytes=texels[u]; bytes.assign((size_t)OPTICS_PIXEL_UNITS(x->layout,x->pitch)*OPTICS_PIXEL_UNITS(x->layout,x->height)*x->bpp*(x->cube?6:1),0); uint64_t nz=0,sum=0;
                    busy=true; int got=optics_frontend_gpu_read(dev,x->offset,bytes.data(),bytes.size()); busy=false;
                    for(uint8_t b:bytes){nz+=b!=0;sum+=b;}
                    [t appendFormat:@" [%u: %#x %ux%u%@ fmt %#x swap %u sel %u%u%u%u smp %#x %@ nz %.2f mean %.0f]",u,x->offset,x->width,x->height,x->cube?@" cube":@"",x->format,x->swap,x->select[0],x->select[1],x->select[2],x->select[3],
                        x->sampler,got?@"read":@"UNREADABLE",bytes.empty()?0.0:(double)nz/bytes.size(),bytes.empty()?0.0:(double)sum/bytes.size()];
                }
                [t appendFormat:@" alpha %u/%u/%.2f",d->alpha_test,d->alpha_func,d->alpha_ref];
                {   uint8_t a[16]={0}; uint64_t vo0; int in=optics_frontend_vram_offset(dev,d->diag_array0,&vo0); busy=true; int got=d->diag_array0&&optics_frontend_gpu_read(dev,d->diag_array0,a,16); busy=false;
                    [t appendFormat:@" vap_cntl_status %#x array0 %#x (%@) bytes %@",d->diag_vap_cntl_status,d->diag_array0,in?@"VRAM":@"not VRAM",got?[NSString stringWithFormat:@"%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
                        a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11]]:@"-"]; }
                if(ok && d->vertices) {   /* the fragment program on the CPU interpreter at vertex 0 (nearest texel, wrap) */
                    struct Ctx { const OpticsDraw *d; } ctx={d};
                    auto sample=[](void *o,unsigned unit,unsigned op,const float c[4],float out[4])->bool {
                        const OpticsDraw *dd=((Ctx *)o)->d; if(unit>=OPTICS_DRAW_TEXTURES || texels[unit].empty()) return false;
                        const OpticsTexture *x=&dd->texture[unit]; float s0=c[0],t0=c[1]; if(op==3 && c[3]!=0){s0/=c[3];t0/=c[3];}
                        size_t face=0;
                        if(x->cube) {   /* major axis, then (sc/ma+1)/2, (tc/ma+1)/2 of that face (the GL/D3D table) */
                            float ax=fabsf(c[0]),ay=fabsf(c[1]),az=fabsf(c[2]),ma,sc,tc; unsigned f;
                            if(ax>=ay && ax>=az){ma=ax;f=c[0]>=0?0:1;sc=c[0]>=0?-c[2]:c[2];tc=-c[1];}
                            else if(ay>=az){ma=ay;f=c[1]>=0?2:3;sc=c[0];tc=c[1]>=0?c[2]:-c[2];}
                            else {ma=az;f=c[2]>=0?4:5;sc=c[2]>=0?c[0]:-c[0];tc=-c[1];}
                            if(ma==0) return false; s0=(sc/ma+1)/2; t0=(tc/ma+1)/2; face=(size_t)f*OPTICS_PIXEL_UNITS(x->layout,x->pitch)*OPTICS_PIXEL_UNITS(x->layout,x->height)*x->bpp;
                        }
                        long ix=(long)floorf(s0*x->width),iy=(long)floorf(t0*x->height); ix%=(long)x->width; iy%=(long)x->height; if(ix<0)ix+=x->width; if(iy<0)iy+=x->height;
                        uint8_t rgba[4]; OpticsPixelLayout l={x->layout,{x->select[0],x->select[1],x->select[2],x->select[3]}};
                        optics_pixel_reference_texel(l,texels[unit].data()+face,x->width,x->height,(size_t)OPTICS_PIXEL_UNITS(x->layout,x->pitch)*x->bpp,ix,iy,rgba); for(int k=0;k<4;k++) out[k]=rgba[k]/255.0f; return true;
                    };
                    PtR2ShaderIO *io=(PtR2ShaderIO *)calloc(1,sizeof(PtR2ShaderIO)); PtR2ShaderError er={};
                    if(io){ memcpy(io->constant,d->us_constant,sizeof d->us_constant);
                        for(unsigned k=0;k<d->varyings;k++) memcpy(io->input[d->us_input[k]],vo[0].varying[k],16); }
                    if(io && pt_r2_shader_execute(d->us,&d->fi,io,sample,&ctx,&er)==PT_R2_SHADER_OK) [t appendFormat:@" cpu-colour (%.3f %.3f %.3f %.3f)%@",
                        io->output[d->color_output][0],io->output[d->color_output][1],io->output[d->color_output][2],io->output[d->color_output][3],io->killed?@" killed":@""];
                    else [t appendFormat:@" cpu-colour failed: %s",er.reason];
                    free(io);
                }
                float first[OPTICS_DRAW_INPUTS][4]={};if(d->vertices)optics_draw_input(d,0,first);
                [t appendFormat:@" varyings %u verts %u idx %u pvs %llu us %llu in0 (%g %g %g %g)",d->varyings,d->vertices,d->indices,(unsigned long long)d->pvs_id,(unsigned long long)d->us_id,
                    first[0][0],first[0][1],first[0][2],first[0][3]];
                for(unsigned k=1;k<OPTICS_DRAW_INPUTS;k++) if(first[k][0]||first[k][1]||first[k][2]||first[k][3]!=1) [t appendFormat:@" in%u (%g %g %g %g)",k,first[k][0],first[k][1],first[k][2],first[k][3]];
                [t appendFormat:@" out_fmt %#x/%#x query %u zb %#x cull %u",d->diag_out_fmt[0],d->diag_out_fmt[1],d->query,d->zb_offset,d->cull];
                for(unsigned k=0;k<d->varyings;k++) [t appendFormat:@" rs%u out %u comp %u%u%u%u us %u",k,d->pvs_output[k],d->component[k][0],d->component[k][1],d->component[k][2],d->component[k][3],d->us_input[k]];
                for(unsigned v=0;ok && v<3 && v<d->vertices;v++) [t appendFormat:@" win%u (%g %g %g %g) v0 (%g %g %g %g)",v,vo[v].window[0],vo[v].window[1],vo[v].window[2],vo[v].window[3],
                    vo[v].varying[0][0],vo[v].varying[0][1],vo[v].varying[0][2],vo[v].varying[0][3]];
                NSLog(@"%@",t);
            }
        }
        /* 3. Vertex stage. The guest's PVS program runs in the Metal vertex function, with the SE_VTE/SE_VPORT transform and
         *    the RS selection as its epilogue, when the draw allows it: no point sprite, standard VTE (scale and offset on,
         *    divide by w). Otherwise on the CPU (optics_draw_vertices) through an owned pass-through program. Both forms give
         *    Metal the same window depth (z in 0..1 of the target). */
        unsigned nv=d->varyings,ni=std::max(1u,d->pvs_inputs);
        static int full_vte=-1;if(full_vte<0){const char *v=getenv("OPTICS_GPU_VTE");full_vte=v&&atoi(v)!=0;}
        bool gpu=!d->sprite&&ni<=16&&(full_vte||((d->vte&0x33f)==0x3f&&d->pvs_id!=1));
        if(optics_trace()) {   /* development statistics: draws per colour target, every 2^18 draws */
            static std::vector<std::pair<uint32_t,uint64_t>> per; static uint64_t total; bool hit=false;
            for(auto &p:per) if(p.first==d->cb_offset){p.second++;hit=true;break;}
            if(!hit && per.size()<64) per.push_back({d->cb_offset,1});
            if(!(++total&0x3ffff)) { NSMutableString *m=[NSMutableString stringWithString:@"optics-frontend: draws per target:"];
                for(auto &p:per) if(p.second) [m appendFormat:@" %#x:%llu",p.first,(unsigned long long)p.second]; NSLog(@"%@",m); for(auto &p:per) p.second=0; }
        }
        if(optics_trace()) {   /* development statistics: which vertex stage, and why not the GPU one */
            static uint64_t n[6]; static uint32_t last_vte; n[gpu?0:d->sprite?1:d->pvs_id==1?2:(d->vte&0x33f)!=0x3f?3:4]++; if((d->vte&0x33f)!=0x3f) last_vte=d->vte;
            if(!(++n[5]&0x3ffff)) NSLog(@"optics-frontend: vertex stage: Metal %llu, CPU %llu (sprite %llu, PVS bypass %llu, VTE %llu last %#x, inputs %llu)",
                n[0],n[1]+n[2]+n[3]+n[4],n[1],n[2],n[3],last_vte,n[4]);
        }
        PtR2Route r; memset(&r,0,sizeof r); r.colorOutput=(uint8_t)d->color_output; r.varyingCount=(uint8_t)nv; r.sampleCount=1;
        r.colorMask=(uint8_t)d->color_mask; r.blendState=d->blend;
        for(unsigned k=0;k<nv;k++) r.usInput[k]=d->us_input[k];
        std::vector<float> vb; bool rawFetch=false; const void *vertexData=nullptr; float epilogue[12]={0}; PtR2ShaderInterface vi; const PtR2ShaderIR *pvs; PtR2Program *program=nil;
        for(;;) {   /* a PVS program that does not prepare on Metal falls back to the CPU form (remembered per key) */
        if(gpu) {
            bool raw=d->raw_vertex&&(ni<=d->inputs||d->raw_general);rawFetch=raw;
            if(raw)vertexData=d->raw_vertex;
            else if(d->raw_vertex){vb.resize((size_t)d->vertices*ni*4);for(uint32_t v=0;v<d->vertices;v++){float input[OPTICS_DRAW_INPUTS][4];optics_draw_input(d,v,input);memcpy(&vb[(size_t)v*ni*4],input,ni*16);}vertexData=vb.data();}
            else if(ni==OPTICS_DRAW_INPUTS) vertexData=d->vertex;
            else { vb.resize((size_t)d->vertices*ni*4); for(uint32_t v=0;v<d->vertices;v++) memcpy(&vb[(size_t)v*ni*4],d->vertex[v],ni*16); vertexData=vb.data(); }
            vi=d->vi; vi.input_count=ni; pvs=d->pvs;
            r.positionOutput=(uint8_t)d->position_output; r.vte=1; r.selectVaryings=1; r.stride=raw?d->raw_stride:(uint16_t)(ni*16);
            for(unsigned k=0;k<nv;k++){r.pvsOutput[k]=d->pvs_output[k];memcpy(r.select[k],d->component[k],4);}
            for(unsigned j=0;j<ni;j++)r.attribute[j]=raw?(PtR2Attribute){d->raw_attribute[j].offset,d->raw_attribute[j].components,d->raw_attribute[j].format}:(PtR2Attribute){(uint16_t)(16*j),4,0};
            for(unsigned j=0;j<ni;j++)r.vapSelect[j]=raw&&d->raw_general?d->raw_select[j]:0;
            float w2=2.0f/W,h2=2.0f/surface->height; const float *vp=d->viewport;   /* x,y: window = ndc*scale+offset, Metal clip form */
            epilogue[0]=vp[0]*w2; epilogue[1]=vp[1]*w2-1.0f; epilogue[2]=-vp[2]*h2; epilogue[3]=1.0f-vp[3]*h2;
            epilogue[4]=vp[4]; epilogue[5]=vp[5];
            if(full_vte){r.vte=2;for(unsigned c=0;c<6;c++)epilogue[c]=vp[c];epilogue[6]=W;epilogue[7]=surface->height;epilogue[8]=(float)((d->vte&0x33f)|(d->depth_enable?0x400:0));}
        } else {
            rawFetch=false;
            std::vector<OpticsVertexOut> vo(d->vertices?d->vertices:1);
            if(!optics_draw_vertices(d,vo.data(),why)) return OPTICS_EXEC_REFUSED;
            vb.resize((size_t)d->vertices*(nv+1)*4);
            for(uint32_t v=0;v<d->vertices;v++) {
                float *o=&vb[(size_t)v*(nv+1)*4],w=vo[v].window[3];
                o[0]=(2.0f*vo[v].window[0]/W-1.0f)*w; o[1]=(1.0f-2.0f*vo[v].window[1]/surface->height)*w; o[3]=w;
                o[2]=(d->depth_enable?2.0f*vo[v].window[2]-1.0f:0.0f)*w;   /* the pass-through route maps z to (z+w)/2 */
                for(unsigned k=0;k<nv;k++) for(int c=0;c<4;c++) o[4*(k+1)+c]=vo[v].varying[k][c];
            }
            vertexData=vb.data();
            memset(&vi,0,sizeof vi); vi.abi=PT_R2_SHADER_ABI; vi.input_count=nv+1; vi.output_count=nv+1; vi.constant_count=1;
            r.positionOutput=0; r.stride=(uint16_t)((nv+1)*16);
            for(unsigned k=0;k<nv;k++) r.pvsOutput[k]=(uint8_t)(k+1);
            for(unsigned j=0;j<=nv;j++) r.attribute[j]=(PtR2Attribute){(uint16_t)(16*j),4,0};
            if(!pass_ir[nv] && (pass_ir[nv]=(PtR2ShaderIR *)malloc(sizeof(PtR2ShaderIR))) && !optics_draw_passthrough(nv+1,pass_ir[nv])){free(pass_ir[nv]);pass_ir[nv]=nullptr;}
            if(!pass_ir[nv]) return fail(why,"pass-through program",nil);
            pvs=pass_ir[nv];
        }
        ProgramKey key; memset(&key,0,sizeof key); key.us=d->us_id; key.pvs=gpu?d->pvs_id:0; key.outputs=vi.output_count; key.inputs=vi.input_count; key.route=r;
        for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) if(d->texture_mask>>u&1 && d->texture[u].cube) key.cube|=1u<<u;
        if(last_valid && KeyEq()(key,last_key)) program=last_program;
        else {
            auto found=programs.find(key);
            if(found!=programs.end()) program=found->second;
            else {
                program=owner.on([&]{return [renderer preparePVS:pvs vertexInterface:&vi fragment:d->us interface:&d->fi route:r error:&e];}); stats_compiles++;
                if(programs.size()>=2048) programs.clear();   /* bounded; draws in flight keep theirs */
                programs.emplace(key,program);
                static unsigned shown; if(!program && gpu && (++shown<=16 || !(shown&1023))) {
                    /* which key parts vary (Doom 3 once reached the renderer's program limit) */
                    std::set<uint64_t> u,v; std::set<uint32_t> o,in,c; std::set<std::vector<uint8_t>> rt;
                    for(auto &k:programs){u.insert(k.first.us);v.insert(k.first.pvs);o.insert(k.first.outputs);in.insert(k.first.inputs);c.insert(k.first.cube);
                        const uint8_t *p=(const uint8_t *)&k.first.route;rt.insert(std::vector<uint8_t>(p,p+sizeof(PtR2Route)));}
                    NSLog(@"optics-frontend: PVS on Metal not prepared (%u so far), CPU vertex stage for this program: %@; keys %zu: US %zu, PVS %zu, outputs %zu, inputs %zu, cube %zu, routes %zu",
                        shown,e?e.localizedDescription:@"?",programs.size(),u.size(),v.size(),o.size(),in.size(),c.size(),rt.size());
                }
            }
            last_key=key; last_program=program; last_valid=true;
        }
        if(!program && gpu){ gpu=false; memset(&r.pvsOutput,0,sizeof r.pvsOutput); r.vte=0; r.selectVaryings=0; memset(r.select,0,sizeof r.select);
            memset(r.attribute,0,sizeof r.attribute);memset(r.vapSelect,0,sizeof r.vapSelect); e=nil; continue; }
        if(!program) return fail(why,"program preparation",e);
        break;
        }
        PtR2Raster raster={}; raster.x=0; raster.y=0; raster.width=W; raster.height=surface->height; raster.znear=0; raster.zfar=1;
        raster.scissorX=(uint32_t)d->scissor[0]; raster.scissorY=(uint32_t)d->scissor[1];
        raster.scissorWidth=(uint32_t)(d->scissor[2]-d->scissor[0]); raster.scissorHeight=(uint32_t)(std::min<int32_t>(d->scissor[3],(int32_t)surface->height)-d->scissor[1]);
        raster.depthCompare=d->depth_enable?(uint8_t)d->depth_compare:7; raster.depthWrite=(uint8_t)d->depth_write;
        {   /* Culling as SU_CULL_MODE asks (Doom 3 culls front or back faces). The R300's window y runs down like Metal's
             * framebuffer, and the epilogue's two y flips cancel: the winding is kept (with the opposite winding, diagnostic
             * OPTICS_CULL=2, Doom 3's walls go missing; OPTICS_CULL=0 turns culling off, which costs 6% more GPU time). */
            int mode=cull_mode(); raster.cull=mode&&d->cull<3?d->cull:0; raster.frontCCW=(uint8_t)((d->front_cw==0)^(mode==2));
            raster.depthBias=d->depth_bias; raster.depthSlope=d->depth_slope;
        }
        raster.stencilEnable=(uint8_t)d->stencil_enable; raster.stencilReference=d->stencil_ref;
        raster.stencilReadMask=d->stencil_mask; raster.stencilWriteMask=d->stencil_writemask;
        raster.stencilFront=(PtR2StencilFace){d->stencil_op[0],d->stencil_op[1],d->stencil_op[2],d->stencil_op[3]};
        raster.stencilBack=(PtR2StencilFace){d->stencil_back[0],d->stencil_back[1],d->stencil_back[2],d->stencil_back[3]};
        int linear=0; for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) if(d->texture_mask>>u&1) linear|=d->texture[u].linear;
        memcpy(raster.vertexEpilogue,epilogue,sizeof epilogue);
        raster.samplers=1; for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) raster.sampler[u]=d->texture[u].sampler;
        raster.alphaTest=d->alpha_test; raster.alphaCompare=d->alpha_func; raster.alphaReference=d->alpha_ref;
        raster.lines=d->lines; raster.query=d->query; for(unsigned c=0;c<4;c++) raster.blendColor[c]=(d->blend_color>>(c==3?24:16-8*c)&255)/255.0f;   /* ARGB8888 -> RGBA */
        static const float zero[4]={0,0,0,0};
        if(owner.enabled()&&!touch(*surface))return failhard(why,"reserved colour destination",nil);
        if(!retry([&]{return owner.draw(renderer,program,surface->target,vertexData,d->vertices,r.stride?r.stride:sizeof(PtR2Vertex),d->index,d->indices,
              gpu?&d->pvs_constant[0][0]:zero,gpu?std::max(1u,d->pvs_constants)*16:sizeof zero,&d->us_constant[0][0],sizeof d->us_constant,
              (NSArray<PtR2Texture *> *)textures,linear,raster,&e);},&e)) return failhard(why,"draw",e);
        if(surface->bound&&(d->depth_write||d->stencil_enable))for(auto &z:depths)if(z.depth==surface->bound&&z.linear_io)z.dirty=true;
        if(!touch(*surface)) return failhard(why,"VRAM ownership",nil); stats_draws++;if(gpu&&r.vte==2)stats_vte2_draws++;if(rawFetch){stats_raw_draws++;stats_raw_bytes+=(uint64_t)d->vertices*d->raw_stride;}
        if(surface->cached_seen!=cache_adds){ invalidate(surface->start,surface->end); surface->cached_seen=cache_adds; }   /* nothing cached since: nothing to drop */
        if(++since_commit>=batch_draws()){ NSError *c=nil; commit(&c); poll(); }
        return OPTICS_EXEC_DONE;
    }
}

/* executor.h table: thin adapters onto the per-device instance. */
void *x_create(PtRadeon *d) { Exec *x=new Exec; x->dev=d; return x; }
/* Every call holds the device lock (or comes from the only owner); adopt() makes its thread the renderer's owner. Every
 * call that can autorelease has its own pool: the CP thread has no outer one. */
static Exec *at(void *o) { return ((Exec *)o)->adopt(); }
void x_destroy(void *o,PtRadeon *) { Exec *x=(Exec *)o; if(!x) return; @autoreleasepool { at(x)->close(); } delete x; }
int x_reset(void *o,PtRadeon *) { int r; @autoreleasepool { r=at(o)->reset(); } return r; }
/* Stutter diagnosis: OPTICS_HITCH=1 logs gaps of 40 ms and more
 * between draws (the guest sent none: game, guest driver or emulated devices) and draws of 15 ms and more here (Metal
 * program preparation, texture uploads), with the time since the first draw. 400 lines. */
int x_draw(void *o,PtRadeon *dev,const OpticsDraw *d,const char **why) {   /* draw has its own pool */
    static int on=-1; if(on<0){const char *v=getenv("OPTICS_HITCH");on=v&&atoi(v)!=0;}
    Exec *x=at(o); if(!on) return x->draw(d,why);
    static uint64_t first,last_end,last_acc,last_bytes; static unsigned shown; uint64_t acc,bytes;
    uint64_t c0=x->stats_compiles,u0=x->stats_texture_uploads,n0=x->upload_ns,t0=clock_gettime_nsec_np(CLOCK_UPTIME_RAW); if(!first) first=t0;
    optics_frontend_cpu_vram(dev,&acc,&bytes);
    if(last_end && t0-last_end>=40000000ull && shown<400){ shown++; NSLog(@"optics-frontend: hitch gap %.0f ms before the draw at %.3f s (no draw from the guest; CPU VRAM accesses %llu, %.1f MB)",
        (t0-last_end)/1e6,(t0-first)/1e9,(unsigned long long)(acc-last_acc),(bytes-last_bytes)/1e6); }
    int r=x->draw(d,why); uint64_t t1=clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    if(t1-t0>=15000000ull && shown<400){ shown++; NSLog(@"optics-frontend: hitch slow draw %.0f ms at %.3f s: programs prepared %llu, texture uploads %llu (%.0f ms; last %ux%u fmt %#x %u bytes %@)",(t1-t0)/1e6,(t0-first)/1e9,
        (unsigned long long)(x->stats_compiles-c0),(unsigned long long)(x->stats_texture_uploads-u0),(x->upload_ns-n0)/1e6,x->upload_last[0],x->upload_last[1],x->upload_last[2],x->upload_last[3],x->upload_last[4]?@"VRAM":@"GART"); }
    optics_frontend_cpu_vram(dev,&last_acc,&last_bytes); last_end=t1; return r;
}
int x_materialize(void *o,PtRadeon *,uint64_t start,uint64_t end,int write) { int r; @autoreleasepool { r=at(o)->materialize(start,end,write); } return r; }
int x_clear(void *o,PtRadeon *,uint64_t gpu,int plane) { int r; @autoreleasepool { r=at(o)->clear(gpu,plane); } return r; }
int x_fence(void *o,PtRadeon *) { int r; @autoreleasepool { r=at(o)->fence(); } return r; }
int x_query(void *o,PtRadeon *,uint32_t q,uint64_t *n) { int r; @autoreleasepool { r=at(o)->query(q,n); } return r; }
int x_poll(void *o,PtRadeon *) { Exec *x=at(o); @autoreleasepool { x->poll(); x->commit_if_idle(); } return x->broken?-1:0; }
int x_frame(void *o,PtRadeon *,uint64_t start,uint32_t w,uint32_t h,uint32_t line,void **handle,void **texture) {
    int r; @autoreleasepool { r=at(o)->frame(start,w,h,line,handle,texture); } return r;
}
void x_frame_release(void *handle) { if(handle) CFRelease(handle); }   /* the retained PtR2Frame, any thread */
void x_dump(void *o,PtRadeon *,const char *dir,unsigned tag) { @autoreleasepool { at(o)->dump(dir,tag); } }
void x_status(void *o,PtRadeon *,OpticsStatus *s) { Exec *x=(Exec *)o; if(x && x->renderer) @autoreleasepool { at(x)->owner.on([&]{optics_status_renderer(s,x->renderer);}); } }
}
static const OpticsExecutor table={x_create,x_destroy,x_reset,x_draw,x_materialize,x_clear,x_fence,x_poll,x_frame,x_frame_release,x_dump,x_status,x_query};
const OpticsExecutor *optics_metal_executor(void) { return &table; }
__attribute__((constructor)) static void install(void) { optics_frontend_set_executor(&table); }
