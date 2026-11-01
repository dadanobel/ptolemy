/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ptolemy's Radeon 9700 (R300) device frontend: the include/radeon.h contract. CPU/I/O owner; the CP runs on that owner when WPTR moves, and resumes from the device
 * service when a completion fence it waits on is done. The GPU executor (executor.h) is a per-device instance.
 * With config.cp_thread (the product) the CP runs on its own thread instead, as the hardware CP runs beside the CPU:
 * a WPTR write only wakes it. One device lock serializes it with every entry point (CPU, service, DMA coherence); the CP
 * thread lets a waiting caller in between two packets. RPTR, scratch and RBBM_STATUS then report its real progress.
 *
 * Register facts: AMD R3xx/R100 register references, Linux radeon_reg.h/r300_reg.h, and the register writes of Tiger's
 * ATIRadeon9700 driver recorded in MMIO traces. Where only a trace supports a meaning, the code says so.
 *
 * Implemented: PCI-mirror/ID registers, MM_INDEX/DATA, PLL index/data storage, Bochs-VBE boot mode set and the
 * VGA DAC used by the current OpenBIOS fork, CRTC scanout, memory controller windows (FB, PCI GART at 0x0AB0,
 * AGP through the UniNorth GATT), CP ring with endian handling, indirect buffers (CP_IB_BASE/BUFSZ written from the
 * stream), RPTR and scratch write-back, the 2D memory-to-memory copy and solid fill, 3D draws through the executor.
 * Completion: scratch/SW-interrupt effects are deferred behind their accepted GPU prefix, while CP can continue
 * (RBBM_STATUS reports busy until retirement); GPU-produced colour bytes are exported before CPU/2D/fetch access and a
 * failed export stops the device instead of serving stale bytes. The scanout is published to the UI as immutable
 * frames (native GPU snapshots, or CPU copies of VRAM rows). Device writes to guest RAM are reported to the CPU owner
 * (ram_written). CPU VRAM accesses go to the CPU view of the framebuffer's aperture (fast VRAM: VRAM in the
 * CPU's byte order, granted and taken back per 16 KiB granule); the other aperture and the interpreter trap.
 * Profiles: DEGRADED by default (an unexecuted draw/packet is counted and the CP continues); OPTICS_STRICT=1 stops the CP. */
#include "frontend.h"
#include <time.h>
#include "r300_state.h"
#include "ring.h"
#include "command_stream.h"
#include "foundation/pm4.h"
#include "draw.h"
#include "io.h"
#include "guard.h"
#include "timeline.h"
#include <sys/mman.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    MM_INDEX=0x0000,MM_DATA=0x0004,CLOCK_CNTL_INDEX=0x0008,CLOCK_CNTL_DATA=0x000c,GEN_INT_CNTL=0x0040,GEN_INT_STATUS=0x0044,
    CRTC_STATUS=0x005c,
    CRTC_GEN_CNTL=0x0050,CONFIG_MEMSIZE=0x00f8,MC_FB_LOCATION=0x0148,MC_AGP_LOCATION=0x014c,AGP_BASE=0x0170,
    AIC_CNTL=0x01d0,AIC_LO_ADDR=0x01dc,AIC_HI_ADDR=0x01e0,
    CRTC_H_TOTAL_DISP=0x0200,CRTC_V_TOTAL_DISP=0x0208,CRTC_VLINE_CRNT_VLINE=0x0210,
    CRTC_OFFSET=0x0224,CRTC_OFFSET_CNTL=0x0228,CRTC_PITCH=0x022c,
    SURFACE_CNTL=0x0b00,             /* NONSURF_AP0/AP1 byte swapping of CPU aperture accesses: bits 20/21, 22/23 */
    PCIGART_PT_BASE=0x0ab0,          /* trace: the driver's PCI GART table base; entries are LE page addresses */
    CP_RB_BASE=0x0700,CP_RB_CNTL=0x0704,CP_RB_RPTR_ADDR=0x070c,CP_RB_RPTR=0x0710,CP_RB_WPTR=0x0714,
    CP_IB_BASE=0x0738,CP_IB_BUFSZ=0x073c,SCRATCH_UMSK=0x0770,SCRATCH_ADDR=0x0774,
    RBBM_STATUS=0x0e40,PCI_MIRROR=0x0f00,
    DST_OFFSET=0x1404,DST_PITCH=0x1408,SRC_PITCH_OFFSET=0x1428,DST_PITCH_OFFSET=0x142c,SRC_Y_X=0x1434,DST_Y_X=0x1438,
    DST_HEIGHT_WIDTH=0x143c,DP_GUI_MASTER_CNTL=0x146c,DP_BRUSH_FRGD_CLR=0x147c,SRC_OFFSET=0x15ac,SRC_PITCH=0x15b0,
    SRC_ENDIAN=0x15d4,               /* trace + reference comment: 2D source endian swap, 0 none / 1 16-bit / 2 32-bit */
    SCRATCH_REG0=0x15e0,DP_WRITE_MASK=0x16cc,DEFAULT_PITCH_OFFSET=0x16e0,DEFAULT_PITCH=0x16e4,SC_TOP_LEFT=0x16ec,
    SC_BOTTOM_RIGHT=0x16f0,WAIT_UNTIL=0x1720,
    REG_BYTES=0x10000,FRAME_SLOTS=3,RING_BUDGET=4096,IB_DEPTH=4,IB_WORDS=1u<<20,LOG_LIMIT=32,
};
enum { RB_NO_UPDATE=1u<<27 };
/* Interrupts (radeon_reg.h): GEN_INT_CNTL masks, GEN_INT_STATUS pending bits (write 1 to acknowledge): CRTC_VBLANK bit 0,
 * CRTC2_VBLANK bit 9, SW_INT bit 25 (SW_INT_FIRE bit 26 of a status write raises it). CRTC_STATUS: bit 0 in vertical
 * blank now, bit 1 VBLANK_SAVE latched at each blank (write 1 to clear). */
enum { INT_VBLANK=1u<<0, INT_VBLANK2=1u<<9, INT_SW=1u<<25, INT_SW_FIRE=1u<<26, CRTC_VBLANK_NOW=1u<<0, CRTC_VBLANK_SAVE=1u<<1 };

/* Scanout publication: the CPU owner fills a slot nobody holds, then publishes it (seq); the UI leases the newest
 * published slot until its presentation completes. The lock guards slot state only, never pixel copies. A slot holds
 * either copied VRAM rows (bytes) or a retained native GPU frame (native/texture). */
typedef struct { void *handle; uint8_t *bytes; size_t capacity; void *native,*texture; PtRadeonFrame f; unsigned refs; uint64_t seq; } Slot;
enum { DEFER_MAX=64 };
typedef struct { uint8_t *p; size_t n; int ram; } Span;
typedef struct {
    PtR2TimelineToken fence; uint32_t a,v; unsigned spans;
    Span destination[4];   /* At most four bytes; retain the mapping and write-back enable at acceptance. */
} DeferredCompletion;
struct PtRadeon {
    PtRadeonConfig config;
    uint8_t *vram; size_t vram_bytes;
    uint8_t pci[256]; PtAgp agp;
    uint32_t reg[REG_BYTES/4], pll[64];
    uint16_t vbe[16]; unsigned vbe_index; uint8_t dac[256][4]; unsigned dac_write,dac_read,dac_component;
    uint32_t dst_offset,dst_pitch,src_offset,src_pitch; unsigned boot_swap; int surface_written,in_executor,direction_written;
    PtR2Ring ring; int ring_ready,ring_stalled,frame_pending; _Atomic int cp_wait; uint32_t ring_words;   /* cp_wait: read by completion threads */
    OpticsCommandStream *stream;
    OpticsR300State *r300;
    void *exec;                                   /* executor instance (create/destroy/reset with this device) */
    /* VRAM page ownership (io: GPU-owned destinations, watched GPU copies, versions) and its 16 KiB groups.
     * direct: the CPU view of aperture fb_ap is mapped (fast VRAM, below; OPTICS_DIRECT_VRAM=0 traps every access). */
    PtR2Io *io; PtR2GuardMap *guards; int direct;
    /* Fast VRAM: the CPU view of aperture fb_ap. view holds VRAM in the CPU's byte order (its lanes and
     * SURFACE regions applied) and is what the guest maps (direct); per 16 KiB granule view_state 0 = VRAM is current
     * and the view is protected (any access faults), 1 = the view is a current read-only copy, 2 = the CPU writes the
     * view (VRAM is stale until a device access converts it back). Every device use of VRAM bytes syncs first. */
    uint8_t *view,*view_state; size_t view_count;
    /* The aperture of the framebuffer and of the view: 0 for 32 bits, 1 for 8 and 16. Tiger's driver sets
     * SURFACE_CNTL to 0x00200001 after every mode switch whatever the depth: aperture 0 swaps 32-bit words, aperture 1
     * stays linear. Through aperture 0, 8-bit pixels would land mirrored in groups of four. */
    unsigned fb_ap;
    /* GPU completion ledger (timeline.c): one sequence per executed GPU job (issued), executor batches submitted and
     * completed against it; deferred guest completions (scratch / SW interrupt writes) wait on its fences, in order. */
    PtR2Timeline *tl; uint64_t issued; DeferredCompletion defer[DEFER_MAX]; unsigned defer_head,defer_count;
    int held;   /* the CP holds a completion packet (BUSY): counted once per packet as a fence wait */
    uint64_t guest_ns, frame_seq, epoch, published_ns, vblank_ns, vram_writes, published_writes;
    uint32_t int_status, crtc_status; int irq_level;
    pthread_mutex_t frame_lock; pthread_t owner; Slot slot[FRAME_SLOTS]; uint32_t published_mode[5];
    OpticsFrontendStats stats; unsigned logged;
    /* OPTICS_RATE=1 (development): per host second, CP time, draw build and executor, GPU work; CP thread: lock hand-overs */
    uint64_t rate_last,rate_cp,rate_build,rate_exec,rate_prev[4],rate_yields,rate_io[2]; int rate_depth;
    uint64_t rate_slow,rate_slow_lock,rate_slow_own; uint32_t rate_slow_at;   /* OPTICS_RATE: CPU VRAM accesses over 20 us: lock wait, materialize */
    uint64_t zpass; unsigned zpass_draws,zpass_empty; int zpass_open; uint32_t zpass_query;   /* occlusion query: open since ZB_ZPASS_DATA, its id */
    /* config.cp_thread: lock guards all of the above; the CP thread holds it while it runs and lets
     * go between packets while want (callers spinning for it) is nonzero. cp_kicked: a WPTR write it has not seen yet;
     * cp_running: inside cp_run; cp_sleeping: waiting on cp_event; cp_progress: packets accepted plus completions published. */
    pthread_mutex_t lock; pthread_t cp; PtEvent cp_event; _Atomic unsigned want;
    int cp_async,cp_quit,cp_kicked,cp_running,cp_sleeping; uint64_t cp_progress;
    /* Immediate-mode primitive through the VAP vertex registers (vtx_write): VAP_VF_CNTL as written, the current
     * attributes (0x2300..0x24ab), the 16-byte groups written since it began, and the vertices emitted so far. */
    uint32_t imm_cntl,imm_attr[(0x24ac-0x2300)/4]; uint32_t imm_groups; int imm_open; uint32_t *imm_data; size_t imm_words,imm_cap;
};

static OpticsFrameStorage storage;
static OpticsExecutor exec_ops;
void optics_frontend_set_executor(const OpticsExecutor *e) { exec_ops=e?*e:(OpticsExecutor){0}; }
/* Only while the CP waits on a fence: the kick makes a running vCPU leave to the device service promptly, the event
 * wakes an idle one (MSR.POW). No guest interrupt is raised. A CP thread is woken directly. */
void optics_frontend_wake(PtRadeon *d) {   /* any thread */
    if(!d || !atomic_load(&d->cp_wait)) return;
    if(d->cp_async){ pt_event_signal(&d->cp_event); return; }
    if(d->config.kick) atomic_store(d->config.kick,1u);
    if(d->config.wake) pt_event_signal(d->config.wake);
}
/* Device lock (cp_thread only; without it every call below is a no-op). Holds are short: one CP packet or one access,
 * so a caller spins first (the CP thread lets go within microseconds) and blocks only behind a long one (a GPU wait). */
static void relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield");
#elif defined(__x86_64__)
    __builtin_ia32_pause();
#endif
}
static void take(PtRadeon *d) {
    for(unsigned i=0;i<4096;i++){ if(!pthread_mutex_trylock(&d->lock)) return; relax(); }
    pthread_mutex_lock(&d->lock);
}
static void enter(PtRadeon *d) {   /* a device entry point, on any thread but the CP thread */
    if(!d->cp_async) return;
    atomic_fetch_add_explicit(&d->want,1,memory_order_relaxed); take(d); atomic_fetch_sub_explicit(&d->want,1,memory_order_relaxed);
}
static void leave(PtRadeon *d) { if(d->cp_async) pthread_mutex_unlock(&d->lock); }
/* CP thread between two packets: a waiting caller goes first (it decrements want once it holds the lock, so the CP does
 * not take it back before). 1: the device was reset or is closing meanwhile, the caller's CP state is stale. */
static int cp_yield(PtRadeon *d) {
    if(!d->cp_async || !atomic_load_explicit(&d->want,memory_order_relaxed)) return 0;
    uint64_t epoch=d->epoch; d->rate_yields++; pthread_mutex_unlock(&d->lock);
    for(unsigned i=1;atomic_load_explicit(&d->want,memory_order_relaxed);i++) if(i%1024) relax(); else sched_yield();
    take(d); return d->epoch!=epoch || d->cp_quit;
}
/* OPTICS_TRACE=1: development traces (first register accesses, CRTC/2D/HOST_DATA, surfaces, per-target statistics). */
static uint64_t mono_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec; }
static int rate_on(void) { static int on=-1; if(on<0) on=getenv("OPTICS_RATE") && atoi(getenv("OPTICS_RATE")); return on; }
int optics_trace(void) { static int on=-1; if(on<0) on=getenv("OPTICS_TRACE") && atoi(getenv("OPTICS_TRACE")); return on; }
/* OPTICS_REG_LOG=FILE (development): while FILE.on exists (polled every 50 ms), one line per write to the SURFACE and 2D
 * registers (0x0b00..0x0b8c, 0x1400..0x16ff without scratch), per blit with its resolved operands and per 3D draw with
 * RB3D_COLOROFFSET0/PITCH0 and TX_OFFSET/FORMAT0..2 of each enabled unit, per 3D register write and CPU read (0x2000..0x4fff),
 * and per refused packet3 with its words;
 * host monotonic seconds; at most 400000 lines.
 * SURFACE and memory-controller/GART writes (MC_FB/AGP_LOCATION, AGP_BASE, AIC, PCIGART_PT_BASE; rare) are logged from the start. Returns the stream with the time stamp written, or NULL. */
static FILE *reg_log(int always) {
    static FILE *f; static int checked,on; static char flag[1024]; static uint64_t next; static unsigned long lines;
    if(!checked){ const char *e=getenv("OPTICS_REG_LOG"); checked=1; if(e && *e && strlen(e)<1000 && (f=fopen(e,"w"))) snprintf(flag,sizeof flag,"%s.on",e); }
    if(!f) return NULL;
    uint64_t t=mono_ns();
    if(t>=next){ int was=on; next=t+50000000ull; on=access(flag,F_OK)==0; if(was && !on) fflush(f); }
    if((!on && !always) || lines>=400000) { if(lines==400000){ lines++; fflush(f); } return NULL; }
    lines++; fprintf(f,"%.6f ",t/1e9); return f;
}
static void failed(PtRadeon *d,const char *why) {
    if(!d->stats.failed) fprintf(stderr,"optics-frontend: device failed (%s): command processor stopped until reset\n",why);
    d->stats.failed=1;
}
enum { GRANULE=16384 };
static unsigned aperture_xor(const PtRadeon *d,int ap1);
static unsigned cpu_xor(const PtRadeon *d,int ap1,uint32_t off);
static int region_in(const PtRadeon *d,uint64_t lo,uint64_t hi) {   /* a SURFACE region meets [lo,hi) */
    for(int i=0;i<8;i++){ uint32_t a=d->reg[(0x0b04+16*i)/4],b=d->reg[(0x0b08+16*i)/4]; if(b>a && a<hi && b>=lo) return 1; }
    return 0;
}
/* Byte a of the view is VRAM byte a^x, x the lane XOR of aperture fb_ap at a (0, 1 swap16, 3 swap32), in units of 4 bytes. */
static void lanes(uint8_t *view,uint8_t *vram,size_t n,unsigned x,int to_view) {
    uint8_t *dst=to_view?view:vram; const uint8_t *src=to_view?vram:view;
    if(!x){ memcpy(dst,src,n); return; }
    for(size_t i=0;i<n;i+=4){ uint32_t v; memcpy(&v,src+i,4); v=x==3?__builtin_bswap32(v):(v>>8&0x00ff00ffu)|(v<<8&0xff00ff00u); memcpy(dst+i,&v,4); }
}
static void view_copy(PtRadeon *d,size_t g,int to_view) {
    uint64_t lo=(uint64_t)g*GRANULE;
    if(!region_in(d,lo,lo+GRANULE)){ lanes(d->view+lo,d->vram+lo,GRANULE,aperture_xor(d,(int)d->fb_ap),to_view); return; }
    for(uint64_t a=lo;a<lo+GRANULE;a+=4) lanes(d->view+a,d->vram+a,4,cpu_xor(d,(int)d->fb_ap,(uint32_t)a),to_view);
}
/* Before a device use of VRAM [off,off+n): each granule the CPU holds comes back - its view protected first, then the
 * CPU's bytes converted to VRAM order with the bookkeeping of a CPU write (versions of GPU copies, republication). */
static int view_sync(PtRadeon *d,uint64_t off,uint64_t n) {
    if(!d->view_count || !n || off>=d->vram_bytes) return 0;
    if(n>d->vram_bytes-off) n=d->vram_bytes-off;
    for(size_t g=off/GRANULE,last=(off+n-1)/GRANULE;g<=last;g++) {
        uint8_t st=d->view_state[g]; if(!st) continue;
        if(d->config.vram_protect(d->config.map_opaque,(uint64_t)g*GRANULE,GRANULE,0)){ failed(d,"CPU view protection"); return -1; }
        d->view_state[g]=0; d->view_count--;
        if(st!=2) continue;
        view_copy(d,g,0); d->vram_writes++; d->stats.guard_groups++;
        if(pt_r2_io_before_physical(d->io,0,(uint64_t)g*GRANULE,GRANULE,1)!=PT_R2_IO_OK){ failed(d,"CPU view write-back"); return -1; }
    }
    return 0;
}
/* Before CPU, 2D, fetch or scanout access to VRAM bytes the GPU may own: the executor writes its results back.
 * 0 = VRAM is current. A failure is never returned as clean data. */
static int own_check(PtRadeon *d,uint64_t off,uint64_t n,int write) {
    if(view_sync(d,off,n)) return -1;
    if(write) d->vram_writes++;
    if(!exec_ops.materialize || !n) return 0;
    if(!exec_ops.materialize(d->exec,d,off,off+n,write)) return 0;
    d->stats.materialize_failures++; failed(d,"GPU result export"); return -1;
}
void optics_frontend_set_frame_storage(const OpticsFrameStorage *s) { storage=s?*s:(OpticsFrameStorage){0}; }
static OpticsCpuFault cpu_fault;   /* HVF route for guarded VRAM faults (cpu_guard.c in the product library) */
void optics_frontend_set_cpu_fault(OpticsCpuFault f) { cpu_fault=f; }
PtR2Io *optics_frontend_io(PtRadeon *d) { return d?d->io:NULL; }
int optics_frontend_cpu_access(PtRadeon *d,uint64_t off,uint64_t n,int write) { return own_check(d,off,n,write); }
int optics_frontend_guard_state(PtRadeon *d,uint64_t off,PtR2IoGuard *child,PtR2IoGuard *group) {
    return d && pt_r2_guard_get(d->guards,0,off,child,group);
}
void optics_frontend_guard_count(PtRadeon *d,int kind) {
    if(d) { if(kind==0) d->stats.guard_faults++; else if(kind==1) d->stats.guard_emulated++; else d->stats.guard_groups++; }
}
/* io -> guard groups -> stage-2. Guest RAM is never guarded by this device: such a request fails (io fails closed).
 * Unmapped VRAM needs no protection (every CPU access traps through own_check). */
static int guard_apply(void *ctx,int ram,uint64_t group,size_t granule,PtR2IoGuard mode) {
    PtRadeon *d=ctx; if(ram) return 0;
    (void)granule;   /* the view's own protection covers every guard: a granule the device uses is closed to the CPU */
    if(d->direct && mode!=PT_R2_IO_OPEN && group<d->vram_bytes && d->view_state[group/GRANULE]) {   /* a user that did not sync */
        size_t g=group/GRANULE; if(d->config.vram_protect(d->config.map_opaque,(uint64_t)g*GRANULE,GRANULE,0)) return 0;
        if(d->view_state[g]==2){ view_copy(d,g,0); d->vram_writes++; }
        d->view_state[g]=0; d->view_count--; d->stats.guard_emulated++;
    }
    return 1;
}
int optics_frontend_cpu_sync(PtRadeon *d,uint64_t off,uint64_t n) { return d?view_sync(d,off,n):-1; }
/* The CPU's stage-2 fault on the view (cpu_guard.c): GPU results exported (a write also drops GPU copies), the granule
 * converted to CPU order and opened - read-only for a read - and the access runs again. 1 = re-run, -1 = fatal. */
int optics_frontend_cpu_grant(PtRadeon *d,uint64_t off,int write) {
    if(!d || off>=d->vram_bytes) return -1;
    enter(d);
    size_t g=off/GRANULE; uint64_t lo=(uint64_t)g*GRANULE; uint8_t st=d->direct?d->view_state[g]:0; int r=1;
    if(!d->direct) r=-1;
    else if(st!=2 && (st!=1 || write)) {
        if(own_check(d,lo,GRANULE,write)) r=-1;   /* also takes a read copy back first (1 -> 0) */
        else {
            view_copy(d,g,1);
            if(d->config.vram_protect(d->config.map_opaque,lo,GRANULE,write?3u:1u)) r=-1;
            else { d->view_count++; d->view_state[g]=(uint8_t)(write?2:1); d->stats.guard_faults++; }
        }
    }
    leave(d); return r;
}
static void io_written(void *ctx,int ram,uint64_t physical,size_t n) {   /* bytes written by an export */
    PtRadeon *d=ctx;
    if(ram){ if(d->config.ram_written) d->config.ram_written(d->config.map_opaque,physical,n); }
    else d->vram_writes++;
}
static void view_unmap(PtRadeon *d) {
    d->config.vram_guard(d->config.map_opaque,NULL,NULL); d->config.direct_map(d->config.map_opaque,NULL,0,0); d->direct=0;
}
/* Fast VRAM: the guest maps the CPU view of the framebuffer's aperture (fb_ap) whenever the board offers the mapping,
 * the protection and the fault route (diagnostic switch OPTICS_DIRECT_VRAM=0: every access traps). Lanes, SURFACE regions
 * and the CP thread need no exception: the view is in the CPU's byte order and the device lock covers the fault route. Only the opt-in render-owner path
 * and OPTICS_DEPTH_IO=2 keep every access trapped. Mapped all closed. */
static void direct_update(PtRadeon *d) {
    static int on=-1; if(on<0){ const char *v=getenv("OPTICS_DIRECT_VRAM"); on=!v || atoi(v)!=0; }
    static int trapped=-1;if(trapped<0){const char *split=getenv("OPTICS_RENDER_OWNER"),*depth=getenv("OPTICS_DEPTH_IO");
        trapped=(split&&atoi(split)!=0)||(depth&&atoi(depth)==2);}   /* OPTICS_DEPTH_IO=2: depth I/O with every access trapped (diagnostic) */
    int want=on && !trapped && cpu_fault && d->config.direct_map && d->config.vram_protect && d->config.vram_guard;
    if(want==d->direct) return;
    d->stats.direct_switches++;
    if(want) {
        if(!d->view) {
            uint8_t *v=mmap(NULL,d->vram_bytes,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0); uint8_t *st=calloc(d->vram_bytes/GRANULE,1);
            if(v==MAP_FAILED || !st){ if(v!=MAP_FAILED) munmap(v,d->vram_bytes); free(st); fprintf(stderr,"optics-frontend: no memory for the CPU view, VRAM accesses trap\n"); return; }
            d->view=v; d->view_state=st;
        }
        d->config.direct_map(d->config.map_opaque,d->view,d->vram_bytes,2+(int)d->fb_ap);
        if(d->config.vram_protect(d->config.map_opaque,0,d->vram_bytes,0)) {
            d->config.direct_map(d->config.map_opaque,NULL,0,0); fprintf(stderr,"optics-frontend: CPU view: protection failed, VRAM accesses trap\n"); return;
        }
        d->config.vram_guard(d->config.map_opaque,cpu_fault,d); d->direct=1;
        static unsigned shown; if(shown++<4) fprintf(stderr,"optics-frontend: fast VRAM: the CPU view of aperture %u is mapped\n",d->fb_ap);
        return;
    }
    view_sync(d,0,d->vram_bytes); view_unmap(d);
}
/* CPU owner: the VRAM BAR moved (the board withdraws its mapping): the CPU's bytes come back, the view is mapped anew. */
void pt_radeon_remap(PtRadeon *d) {
    if(!d) return;
    enter(d);
    if(d->direct){ view_sync(d,0,d->vram_bytes); view_unmap(d); direct_update(d); }
    leave(d);
}

static uint32_t rd32le(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void wr32le(uint8_t *p,uint32_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static uint32_t swap32(uint32_t v) { return v>>24|(v>>8&0xff00)|(v<<8&0xff0000)|v<<24; }
static void note(PtRadeon *d,const char *fmt,uint32_t a,uint32_t b) {
    if(d->logged++<LOG_LIMIT){fprintf(stderr,"optics-frontend: ");fprintf(stderr,fmt,a,b);fputc('\n',stderr);}
}

/* ---- Memory controller: GPU address -> host bytes. FB first, then PCI GART, then AGP via the UniNorth GATT. ---- */
static int window(uint32_t location,uint64_t *base,uint64_t *length) { return optics_r300_memory_window(location,base,length); }
static uint8_t *ram_at(PtRadeon *d,uint64_t pa,size_t n) {
    PtRam r=d->config.ram; return pa<=r.size && n<=r.size-pa ? r.bytes+pa : NULL;
}
static int translate(PtRadeon *d,uint64_t gpu,Span *s) {
    uint64_t base,len;
    if(window(d->reg[MC_FB_LOCATION/4],&base,&len) && gpu>=base && gpu-base<len) {
        uint64_t off=gpu-base; if(off>=d->vram_bytes) return 0;
        *s=(Span){d->vram+off,(size_t)(len-off<d->vram_bytes-off?len-off:d->vram_bytes-off),0}; return 1;
    }
    uint32_t lo=d->reg[AIC_LO_ADDR/4],hi=d->reg[AIC_HI_ADDR/4];
    if((d->reg[AIC_CNTL/4]&1) && gpu>=lo && gpu<=hi) {
        const uint8_t *e=ram_at(d,(uint64_t)d->reg[PCIGART_PT_BASE/4]+((gpu-lo)>>12)*4,4); if(!e) return 0;
        uint64_t pa=(rd32le(e)&~0xfffu)|(gpu&0xfff); uint8_t *p=ram_at(d,pa,1); if(!p) return 0;
        *s=(Span){p,(size_t)((uint64_t)hi-gpu+1<4096-(gpu&0xfff)?(uint64_t)hi-gpu+1:4096-(gpu&0xfff)),1}; return 1;
    }
    if(window(d->reg[MC_AGP_LOCATION/4],&base,&len) && gpu>=base && gpu-base<len) {
        uint64_t bus=gpu-base+d->reg[AGP_BASE/4], aperture=d->agp.aperture;
        if(bus<aperture) return 0;
        const uint8_t *e=ram_at(d,(uint64_t)(d->agp.gart&~0xfffu)+((bus-aperture)>>12)*4,4); if(!e) return 0;
        uint32_t entry=rd32le(e); if(!(entry&1)) return 0;
        uint64_t pa=(entry&~0xfffu)|(bus&0xfff); uint8_t *p=ram_at(d,pa,1); if(!p) return 0;
        *s=(Span){p,(size_t)(len-(gpu-base)<4096-(bus&0xfff)?len-(gpu-base):4096-(bus&0xfff)),1}; return 1;
    }
    return 0;
}
/* Copy across page/window boundaries; lane_xor 0 raw, 2 swap16, 3 swap32 applied per aligned unit by the caller. */
static int gpu_copy(PtRadeon *d,uint64_t gpu,void *buf,size_t n,int write) {
    if((!buf&&n) || n>UINT64_MAX-gpu) return 0;
    /* A missing later page/window must not leave a partial device write. */
    if(write) for(size_t done=0;done<n;) { Span span; if(!translate(d,gpu+done,&span) || !span.n) {
        d->stats.translate_faults++; d->stats.last_fault_address=(uint32_t)(gpu+done); return 0;
    } size_t part=span.n<n-done?span.n:n-done; done+=part; }
    uint8_t *b=buf;
    while(n) {
        Span s; if(!translate(d,gpu,&s)){d->stats.translate_faults++;d->stats.last_fault_address=(uint32_t)gpu;return 0;}
        size_t k=s.n<n?s.n:n;
        if(!s.ram && (d->in_executor?view_sync(d,(uint64_t)(s.p-d->vram),k):own_check(d,(uint64_t)(s.p-d->vram),k,write))) return 0;
        if(write) memcpy(s.p,b,k); else memcpy(b,s.p,k);
        /* A device write to guest RAM: the CPU owner's reservation/coherence protocol hears about it. */
        if(write && s.ram && d->config.ram_written) d->config.ram_written(d->config.map_opaque,(uint64_t)(s.p-d->config.ram.bytes),k);
        gpu+=k; b+=k; n-=k;
    }
    return 1;
}
static void cp_swap(const PtRadeon *d,uint8_t *p,size_t n) {   /* CP_RB_CNTL BUF_SWAP: 0 none, 1 16-bit, 2 32-bit */
    unsigned mode=d->reg[CP_RB_CNTL/4]>>16&3;
    for(size_t i=0;i+4<=n;i+=4) {
        uint8_t a=p[i],b=p[i+1],c=p[i+2],e=p[i+3];
        if(mode==1){p[i]=b;p[i+1]=a;p[i+2]=e;p[i+3]=c;}
        else if(mode==2){p[i]=e;p[i+1]=c;p[i+2]=b;p[i+3]=a;}
        else if(mode==3){p[i]=c;p[i+1]=e;p[i+2]=a;p[i+3]=b;}
    }
}
/* Device write into guest RAM through the GPU address space (little-endian dword, as the CP writes it). */
static OpticsWritebackHook wb_hook; static void *wb_opaque;
void optics_frontend_set_writeback_hook(OpticsWritebackHook h,void *opaque) { wb_hook=h; wb_opaque=opaque; }
static void writeback(PtRadeon *d,uint32_t gpu,uint32_t value) {
    /* A CP thread's earlier device writes become visible to a guest polling this value in RAM no later than it. */
    atomic_thread_fence(memory_order_release);
    uint8_t b[4]; wr32le(b,value); Span s;
    if(!gpu_copy(d,gpu,b,4,1)) return;
    d->stats.writebacks++;
    if(wb_hook && translate(d,gpu,&s)) wb_hook(wb_opaque,s.ram?(uint64_t)(s.p-d->config.ram.bytes):UINT64_MAX,value);
}

int optics_frontend_gpu_read(PtRadeon *d,uint64_t gpu,void *buf,size_t n) { return gpu_copy(d,gpu,buf,n,0); }
int optics_frontend_gpu_write(PtRadeon *d,uint64_t gpu,const void *buf,size_t n) { return gpu_copy(d,gpu,(void *)buf,n,1); }
/* CPU aperture byte lanes: 32-bit swap XORs the byte address with 3, 16-bit swap with 1. */
static unsigned aperture_xor(const PtRadeon *d,int ap1) {
    if(!d->surface_written) return ap1?0:d->boot_swap;
    uint32_t s=d->reg[SURFACE_CNTL/4]>>(ap1?22:20)&3; return s&2?3:s&1?1:0;
}

/* SURFACE0..7 regions (LOWER_BOUND 0x0b04+16n and UPPER_BOUND 0x0b08+16n, inclusive VRAM offsets; INFO
 * 0x0b0c+16n): a CPU aperture access inside a region takes the region's AP0/AP1 swap (INFO bits 20/21, 22/23), not
 * SURFACE_CNTL's NONSURF one. Tiger's driver programs no-swap regions around its uploads; Doom 3's vertex arrays
 * in VRAM came out byte-swapped without this. The region's tiling translation is not modelled: VRAM stays linear for the
 * CPU and the GPU alike. */
static int cpu_region(const PtRadeon *d,uint32_t off) {
    for(int i=0;i<8;i++){ uint32_t lo=d->reg[(0x0b04+16*i)/4],hi=d->reg[(0x0b08+16*i)/4]; if(hi>lo && off>=lo && off<=hi) return i; }
    return -1;
}
static unsigned cpu_xor(const PtRadeon *d,int ap1,uint32_t off) {
    int i=cpu_region(d,off); if(i<0) return aperture_xor(d,ap1);
    uint32_t s=d->reg[(0x0b0c+16*i)/4]>>(ap1?22:20)&3; return s&2?3:s&1?1:0;
}

/* ---- Scanout ---- */
static unsigned crtc_bpp(uint32_t gen) {
    switch(gen>>8&7){case 2:return 8;case 3:case 4:return 16;case 5:return 24;case 6:return 32;default:return 0;}
}
/* Boot display mode: linear at VRAM 0 (pitch_pixels 0 = tight). The host sets it for Ptolemy's Open Firmware display
 * (ptolemy-radeon.fs ABI 1: XRGB32, CPU view big-endian through aperture 0, GPU storage native ARGB8888) at create and
 * reset; Bochs VBE register writes set it again through vbe_mode. */
static void boot_mode(PtRadeon *d,unsigned w,unsigned h,unsigned bpp,unsigned pitch_pixels) {
    view_sync(d,0,d->vram_bytes);   /* the swap below changes */
    unsigned format=bpp==8?2:bpp==15?3:bpp==16?4:bpp==24?5:6;
    d->reg[CRTC_GEN_CNTL/4]=(1u<<25)|(1u<<24)|(format<<8);      /* CRTC_EN, CRTC_EXT_DISP_EN, pixel width */
    d->reg[CRTC_H_TOTAL_DISP/4]=((w/8)-1)<<16; d->reg[CRTC_V_TOTAL_DISP/4]=(h-1)<<16;
    if(!pitch_pixels) pitch_pixels=w;
    d->reg[CRTC_PITCH/4]=(pitch_pixels+7)/8; d->reg[CRTC_OFFSET/4]=0;
    /* Boot console programs the aperture swap for its depth, as Apple's ATI FCode does before drawing; the driver later
     * writes the same setting. Without it, boot pixels would be in the wrong byte order for the native scanout. */
    d->boot_swap=bpp==32?3:bpp==16||bpp==15?1:0; d->surface_written=0;   /* in effect until the driver writes SURFACE_CNTL */
    if(d->direct && d->fb_ap) view_unmap(d);
    d->fb_ap=0; direct_update(d);
}
static void vbe_mode(PtRadeon *d) { if((d->vbe[4]&1) && d->vbe[1] && d->vbe[2]) boot_mode(d,d->vbe[1],d->vbe[2],d->vbe[3],d->vbe[6]); }
/* The display NDRV's mode switch and colour table (guest/ndrv/display.c, hypercall ops 3 and 4). The CRTC
 * gets what a driver programs for the mode: geometry, pixel width (8 indexed, 16 = 1555 as Mac OS uses it, 32), pitch
 * rounded up to 64 pixels (Tiger's compositor blits with that pitch), scanout at VRAM 0. The framebuffer's aperture gets the
 * byte swap of the depth, so CPU pixels land in the order the scanout reads: aperture 0 for 32 bits, aperture 1 for 8 and
 * 16, which Tiger's driver leaves linear (fb_ap); *fb_offset is its offset in the BAR, the base the NDRV reports. */
int pt_radeon_display_mode(PtRadeon *d,unsigned w,unsigned h,unsigned depth,uint32_t *fb_offset) {
    if(!d) return EINVAL;
    unsigned pitch=depth==32 && w==d->config.width && h==d->config.height ? w : (w+63)/64*64;   /* the boot mode keeps its pitch */
    if((depth!=8 && depth!=16 && depth!=32) || w<320 || w>2560 || w%8 || h<200 || h>1600 ||
       (uint64_t)pitch*(depth/8)*h>d->vram_bytes) return EINVAL;
    enter(d);
    if(view_sync(d,0,d->vram_bytes)){ leave(d); return EIO; }   /* the CPU's bytes did not come back: no new lanes or mode */
    d->reg[CRTC_GEN_CNTL/4]=(d->reg[CRTC_GEN_CNTL/4]&~(7u<<8))|(1u<<25)|(1u<<24)|((depth==8?2u:depth==16?3u:6u)<<8);
    d->reg[CRTC_H_TOTAL_DISP/4]=((w/8)-1)<<16; d->reg[CRTC_V_TOTAL_DISP/4]=(h-1)<<16;
    d->reg[CRTC_PITCH/4]=pitch/8; d->reg[CRTC_OFFSET/4]=0;
    unsigned ap=depth!=32, at=ap?22:20;   /* 8 and 16 bits through aperture 1 (fb_ap) */
    d->reg[SURFACE_CNTL/4]=(d->reg[SURFACE_CNTL/4]&~(3u<<at))|(depth==32?2u:depth==16?1u:0u)<<at; d->surface_written=1;
    if(d->direct && d->fb_ap!=ap) view_unmap(d);
    d->fb_ap=ap; direct_update(d); leave(d);
    if(fb_offset) *fb_offset=ap?(uint32_t)d->vram_bytes:0;
    static unsigned shown; if(shown++<32) fprintf(stderr,"optics-frontend: display mode %ux%u, %u bits, pitch %u\n",w,h,depth,pitch);
    return 0;
}
int pt_radeon_palette(PtRadeon *d,unsigned start,unsigned count,const uint32_t *rgb) {   /* rgb: 0x00RRGGBB */
    if(!d || !rgb || start>255 || count>256-start) return EINVAL;
    enter(d);
    for(unsigned i=0;i<count;i++) { uint8_t *p=d->dac[start+i]; p[0]=(uint8_t)(rgb[i]>>16); p[1]=(uint8_t)(rgb[i]>>8); p[2]=(uint8_t)rgb[i]; p[3]=255; }
    d->vram_writes++; leave(d); return 0;   /* a new colour table republishes the frame */
}
static int scanout_mode(const PtRadeon *d,uint32_t *w,uint32_t *h,uint32_t *bpp,uint32_t *line,uint32_t *offset) {
    uint32_t gen=d->reg[CRTC_GEN_CNTL/4]; *bpp=crtc_bpp(gen);
    if(!(gen&(1u<<25)) || !*bpp) return 0;   /* VRAM holds GPU-native bytes; the frame says which order */
    *w=((d->reg[CRTC_H_TOTAL_DISP/4]>>16&0x1ff)+1)*8; *h=(d->reg[CRTC_V_TOTAL_DISP/4]>>16&0xfff)+1;
    *line=(d->reg[CRTC_PITCH/4]&0x7ff)*8*(*bpp/8); *offset=d->reg[CRTC_OFFSET/4]&0x7ffffff;
    return *line && (uint64_t)*w*(*bpp/8)<=*line && (uint64_t)*offset+(uint64_t)*line*(*h)<=d->vram_bytes;
}

/* ---- 2D engine: memory-to-memory copy and solid fill, CPU reference execution on VRAM/GART bytes. ---- */
static unsigned dst_bytes(unsigned datatype) { return datatype==2||datatype==7?1:datatype==3||datatype==4?2:datatype==6?4:0; }
static int strict(void);
static int exact_2d(void){static int on=-1;if(on<0){const char *v=getenv("OPTICS_2D_CONTRACT");on=v&&atoi(v)!=0;}return on;}
static void blit_refused(PtRadeon *d,const char *reason) {
    d->stats.blits_refused++;d->stats.last_refused_gmc=d->reg[DP_GUI_MASTER_CNTL/4];
    { FILE *f=reg_log(0); if(f) fprintf(f,"2D refused (%s) gmc %08x hw %08x dp_cntl %08x\n",reason,d->reg[DP_GUI_MASTER_CNTL/4],d->reg[DST_HEIGHT_WIDTH/4],d->reg[0x16c0/4]); }
    if(strict())failed(d,reason);
}
static void blit(PtRadeon *d,int cp_rectangle) {
    uint32_t gmc=d->reg[DP_GUI_MASTER_CNTL/4],rop=gmc>>16&0xff,src_source=gmc>>24&7,brush=gmc>>4&15;
    unsigned bpp=dst_bytes(gmc>>8&15),swap=d->reg[SRC_ENDIAN/4]&3;
    uint32_t hw=d->reg[DST_HEIGHT_WIDTH/4],dyx=d->reg[DST_Y_X/4],syx=d->reg[SRC_Y_X/4];
    uint32_t h=hw>>16&0x3fff,w=hw&0x3fff,dy=dyx>>16&0x3fff,dx=dyx&0x3fff,sy=syx>>16&0x3fff,sx=syx&0x3fff;
    int encoded=cp_rectangle||exact_2d();
    uint32_t def=d->reg[DEFAULT_PITCH_OFFSET/4],default_offset=(def&0x3fffffu)<<10,default_pitch=(def>>22&255)*64;
    uint32_t doff=(gmc&2)?d->dst_offset:encoded?default_offset:def,dpitch=(gmc&2)?d->dst_pitch:encoded?default_pitch:d->reg[DEFAULT_PITCH/4];
    uint32_t soff=(gmc&1)?d->src_offset:encoded?default_offset:def,spitch=(gmc&1)?d->src_pitch:encoded?default_pitch:d->reg[DEFAULT_PITCH/4];
    /* CP BITBLT rectangles are logical video pixels, independent of the host-upload swap latch. */
    if(cp_rectangle)swap=0;
    int copy=rop==0xcc && src_source==2, fill=(rop==0xf0 && brush==13) || rop==0x00 || rop==0xff;
    if(!bpp || !w || !h || !(copy||fill) || (gmc&0xc) || (swap && bpp!=4 && !(swap==1 && bpp==2)) ||
       (d->reg[DP_WRITE_MASK/4]!=0xffffffffu && bpp==4)) {
        blit_refused(d,"unsupported 2D operation");note(d,"2D refused: GMC %08x HW %08x",gmc,hw);return;
    }
    uint32_t direction=d->reg[0x16c0/4];int down=direction&2,explicit_direction=exact_2d()&&d->direction_written&&!cp_rectangle;
    if(explicit_direction) {
        if(((gmc&2)&&d->reg[DST_PITCH_OFFSET/4]>>30)||
           (copy&&(gmc&1)&&d->reg[SRC_PITCH_OFFSET/4]>>30)) {blit_refused(d,"2D tiled layout is not implemented");return;}
        if(!(direction&1)) {
            if(dx+1<w||(copy&&sx+1<w)){blit_refused(d,"2D reverse X rectangle underflow");return;}
            dx-=w-1;if(copy)sx-=w-1;
        }
        if(!down&&(dy+1<h||(copy&&sy+1<h))){blit_refused(d,"2D reverse Y rectangle underflow");return;}
    }
    if(optics_trace() && (uint64_t)w*h>=64*64 && (!copy || soff<0x10000000u)) {   /* development trace: 2D fills and VRAM-sourced copies, first per pair */
        static uint32_t seen[160][2]; static unsigned n; unsigned k=0; while(k<n && (seen[k][0]!=soff || seen[k][1]!=doff)) k++;
        if(k==n && n<160) { seen[n][0]=soff; seen[n][1]=doff; n++;
            fprintf(stderr,"optics-frontend: 2D %s %#x(%u,%u) -> %#x(%u,%u) %ux%u pitch %u/%u bpp %u\n",copy?"copy":"fill",soff,sx,sy,doff,dx,dy,w,h,spitch,dpitch,bpp); }
    }
    { FILE *f=reg_log(0); if(f) fprintf(f,"2D %s %#x(%u,%u) -> %#x(%u,%u) %ux%u pitch %u/%u bpp %u gmc %08x dp_cntl %08x%s\n",copy?"copy":"fill",soff,sx,sy,doff,dx,dy,w,h,spitch,dpitch,bpp,gmc,d->reg[0x16c0/4],cp_rectangle?" cp":""); }
    size_t row=(size_t)w*bpp,allocation=cp_rectangle?row*h:row;
    if(allocation>(64u<<20)){blit_refused(d,"2D rectangle snapshot exceeds budget");return;}
    uint8_t *tmp=malloc(allocation); if(!tmp){blit_refused(d,"2D row allocation failed");return;}
    if(cp_rectangle)for(uint32_t y=0;y<h;y++)if(!gpu_copy(d,(uint64_t)soff+(uint64_t)(sy+y)*spitch+(uint64_t)sx*bpp,tmp+y*row,row,0)){
        free(tmp);blit_refused(d,"CP rectangle source mapping failed");return;
    }
    uint32_t color=rop==0xff?0xffffffffu:rop==0?0:d->reg[DP_BRUSH_FRGD_CLR/4];
    /* Overlap-safe order: copy rows bottom-up when the destination starts later in the same surface. */
    int up=copy && doff==soff && (dy>sy || (dy==sy && dx>sx));
    for(uint32_t k=0;k<h;k++) {
        uint32_t i=up?h-1-k:k;
        uint32_t dst_y=explicit_direction?(down?dy+k:dy-k):dy+i;
        uint64_t dst=(uint64_t)doff+(uint64_t)dst_y*dpitch+(uint64_t)dx*bpp;
        if(copy) {
            uint32_t src_y=explicit_direction?(down?sy+k:sy-k):sy+i;
            uint64_t src=(uint64_t)soff+(uint64_t)src_y*spitch+(uint64_t)sx*bpp;
            if(!cp_rectangle&&!gpu_copy(d,src,tmp,row,0)){free(tmp);blit_refused(d,"2D source mapping failed");return;}
            if(swap==2) for(size_t j=0;j+4<=row;j+=4){uint32_t v=swap32(rd32le(tmp+j));wr32le(tmp+j,v);}
            if(swap==1) for(size_t j=0;j+2<=row;j+=2){uint8_t t=tmp[j];tmp[j]=tmp[j+1];tmp[j+1]=t;}
        } else for(uint32_t j=0;j<w;j++) {   /* brush colour is a register value; VRAM pixels are big-endian */
            if(exact_2d())for(unsigned byte=0;byte<bpp;byte++)tmp[bpp*j+byte]=(uint8_t)(color>>(8*byte));
            else if(bpp==4){tmp[4*j]=(uint8_t)(color>>24);tmp[4*j+1]=(uint8_t)(color>>16);tmp[4*j+2]=(uint8_t)(color>>8);tmp[4*j+3]=(uint8_t)color;}
            else if(bpp==2){tmp[2*j]=(uint8_t)(color>>8);tmp[2*j+1]=(uint8_t)color;}
            else tmp[j]=(uint8_t)color;
        }
        if(!gpu_copy(d,dst,tmp+(cp_rectangle?i*row:0),row,1)){free(tmp);blit_refused(d,"2D destination mapping failed");return;}
        d->stats.blit_bytes+=row;
    }
    free(tmp); d->stats.blits++;
}

/* ---- Registers ---- */
static int cp_launch_ib(PtRadeon *d);
static int optics_ring_read(void *,uint32_t,uint32_t,void *);
static int optics_ring_execute(void *,const PtR2Pm4Packet *,uint64_t);
static void rb_setup(PtRadeon *d) {
    unsigned exponent=d->reg[CP_RB_CNTL/4]&0x3f;
    if(d->ring_ready) pt_r2_ring_destroy(&d->ring);
    d->ring_ready=0; d->ring_stalled=0; d->ring_words=0;
    d->reg[CP_RB_RPTR/4]=0;
    if(exponent>19) return;
    uint32_t words=1u<<(exponent+1);
    if(!pt_r2_ring_init(&d->ring,words,0,optics_ring_read,optics_ring_execute,d)){d->ring_ready=1;d->ring_words=words;}
    d->reg[CP_RB_RPTR/4]=0;
}
static void cp_writeback_rptr(PtRadeon *d) {
    if(!(d->reg[CP_RB_CNTL/4]&RB_NO_UPDATE) && d->reg[CP_RB_RPTR_ADDR/4]) writeback(d,d->reg[CP_RB_RPTR_ADDR/4]&~3u,d->reg[CP_RB_RPTR/4]);
}
static void irq_update(PtRadeon *d) {
    int level=(d->int_status & d->reg[GEN_INT_CNTL/4] & (INT_VBLANK|INT_VBLANK2|INT_SW))!=0;
    if(level!=d->irq_level){ d->irq_level=level; if(d->config.irq) d->config.irq(d->config.irq_opaque,level); }
}
static void cp_run(PtRadeon *d,int kicked);
static int cp_pending(const PtRadeon *d);
static int cp_busy(const PtRadeon *d);
/* Occlusion queries (glGetQueryObjectivARB -> the ATI driver's gldGetQueryInfo; Halo polls them every frame).
 * The driver begins with ZB_ZPASS_DATA = 0 and ends with, per pixel pipe i, SU_REG_DEST = 1 << i and ZB_ZPASS_ADDR = the
 * address of that pipe's result word, which it set to -1 and polls until every word changed (then SU_REG_DEST = 0xF);
 * it reads the words with lwz and sums them. The draws in between carry the query's id; the executor counts the samples
 * that pass depth and stencil (Metal visibility results). Pipe 0's word is written at once when the GPU is already done,
 * otherwise as a deferred completion; the other pipes' words are 0 at once. The words lie in GART pages that the driver
 * recycles once it sees a later SCRATCH stamp, so while such a write is pending every later completion (SCRATCH, software
 * interrupt) is deferred behind it instead of published early (admit_completion): a word written after its page was
 * recycled left Halo waiting forever. Without an executor query hook (tests) the count is the draws' on-screen area,
 * with no depth test (Halo's engine glows then showed through its ship's hull). */
enum { SU_REG_DEST=0x42c8, ZB_ZPASS_DATA=0x4f58, ZB_ZPASS_ADDR=0x4f5c };
static int defer_query(PtRadeon *d,uint32_t address,uint32_t query);
/* Diagnostic switch OPTICS_QUERY_GPU=0: the on-screen area as the count, written at once, instead of the executor's count. */
static int query_gpu(void) { static int v=-1; if(v<0){const char *e=getenv("OPTICS_QUERY_GPU");v=!e||atoi(e)!=0;} return v && exec_ops.query; }
static void zpass_draw(PtRadeon *d,const OpticsDraw *dr) {
    if(!d->zpass_open || d->zpass_draws++>=256 || !dr->vertices) return;
    OpticsVertexOut *vo=malloc(sizeof *vo*dr->vertices); const char *why=NULL;
    if(vo && optics_draw_vertices(dr,vo,&why)) {
        float x0=1e30f,y0=1e30f,x1=-1e30f,y1=-1e30f;
        for(uint32_t i=0;i<dr->vertices;i++) { const float *w=vo[i].window; if(!(w[3]>0)) continue;   /* behind the eye */
            if(w[0]<x0)x0=w[0]; if(w[0]>x1)x1=w[0]; if(w[1]<y0)y0=w[1]; if(w[1]>y1)y1=w[1]; }
        if(x0<dr->scissor[0])x0=(float)dr->scissor[0]; if(y0<dr->scissor[1])y0=(float)dr->scissor[1];
        if(x1>dr->scissor[2])x1=(float)dr->scissor[2]; if(y1>dr->scissor[3])y1=(float)dr->scissor[3];
        if(x1>x0 && y1>y0) d->zpass+=(uint64_t)((x1-x0)*(y1-y0)+0.5f);
    }
    free(vo);
}
static void query_counted(PtRadeon *d,uint32_t id,uint64_t n) {
    static unsigned shown,total,seen; total++; seen+=n!=0;
    if(shown<6 || !(total&65535)){ shown++; fprintf(stderr,"optics-frontend: occlusion query %u: %llu samples passed (%u of %u queries saw samples, %u had no draw)\n",id,(unsigned long long)n,seen,total,d->zpass_empty); }
}
static void zpass_end(PtRadeon *d,uint32_t address) {
    uint32_t pipes=d->reg[SU_REG_DEST/4]&15; int first=!pipes||(pipes&1); uint64_t c=first?d->zpass:0;
    d->zpass_open=0; if(first) d->zpass_empty+=!d->zpass_draws;
    if(first && query_gpu()) {   /* from the CP the GPU is done here (execute holds the packet until then); by MMIO maybe not */
        int r=exec_ops.fence?exec_ops.fence(d->exec,d):OPTICS_FENCE_DONE;
        if(r==OPTICS_FENCE_FAILED){ failed(d,"GPU work before an occlusion query failed"); return; }
        if(r==OPTICS_FENCE_DONE && !d->defer_count){ c=0; if(exec_ops.query(d->exec,d,d->zpass_query,&c)) c=0; query_counted(d,d->zpass_query,c); }
        else if(!defer_query(d,address,d->zpass_query)) return;
    }
    uint32_t n=(uint32_t)(c<0xfffffffeu?c:0xfffffffeu);
    writeback(d,address&~3u,__builtin_bswap32(n));   /* Unverified: the GPU writes these words in the order lwz reads */
    static unsigned shown; if(shown<4 && first && !query_gpu()){ shown++; fprintf(stderr,"optics-frontend: occlusion query: %u pixels (on-screen area, depth not tested) to %#x\n",n,address); }
}
/* One register write, from MMIO or from a type-0 packet. 3D-space words also feed the owned state shadow. */
static int run_draw(PtRadeon *d,uint8_t opcode,const uint32_t *payload,uint32_t words);
/* Immediate-mode primitives through the VAP vertex registers: Tiger's driver draws polygons in glPolygonMode line and
 * point modes so (with edge flags). VAP_VF_CNTL written as a register begins one (its primitive type); VAP_VTX_*
 * 0x2300..0x23ff hold the current attributes in 16-byte groups (POS_0 at 0x2300, COLOR_0 at 0x2320: what it writes);
 * POS_0's W (0x230c) emits a vertex of the groups written since the begin, in address order, VAP_VTX_SIZE dwords as an
 * IMMD_2 packet lays them out; VAP_VTX_END_OF_PKT (0x24ac) draws them as that packet would. Unverified beyond Tiger's
 * polygon modes: the layout for other attribute sets. Edge flags (VAP_VTX_EDGE_FLAG 0x245c) are not modelled. */
static void vtx_write(PtRadeon *d,uint32_t a,uint32_t v,int from_cp) {
    if(a==0x2084){ d->imm_cntl=v; d->imm_open=1; d->imm_groups=0; d->imm_words=0; return; }
    if(!d->imm_open) return;
    uint32_t size=d->reg[0x20b4/4]&0x7f;   /* VAP_VTX_SIZE */
    if(a<0x24ac) {
        d->imm_attr[(a-0x2300)/4]=v; if(a<0x2400) d->imm_groups|=1u<<((a-0x2300)/16);
        if(a!=0x230c) return;
        if(!size || d->imm_words+size>(size_t)65535*size){ d->imm_open=0; return; }
        if(d->imm_words+size>d->imm_cap){ size_t cap=d->imm_cap?d->imm_cap*2:1024; while(cap<d->imm_words+size) cap*=2;
            uint32_t *p=realloc(d->imm_data,cap*4); if(!p){ d->imm_open=0; return; } d->imm_data=p; d->imm_cap=cap; }
        uint32_t n=0; for(unsigned g=0;g<16 && n<size;g++) if(d->imm_groups>>g&1) for(unsigned c=0;c<4 && n<size;c++) d->imm_data[d->imm_words+n++]=d->imm_attr[4*g+c];
        while(n<size) d->imm_data[d->imm_words+n++]=0;
        d->imm_words+=size; return;
    }
    d->imm_open=0; uint32_t count=size?(uint32_t)(d->imm_words/size):0;   /* 0x24ac: END_OF_PKT */
    if(!count) return;
    if(!from_cp){ static unsigned shown; if(shown++<4) fprintf(stderr,"optics-frontend: immediate primitive written through MMIO, not drawn\n"); return; }
    uint32_t *payload=malloc((1+(size_t)count*size)*4); if(!payload) return;
    payload[0]=(d->imm_cntl&15)|3u<<4|count<<16;   /* VF_CNTL: its primitive, the vertices in the packet */
    memcpy(payload+1,d->imm_data,(size_t)count*size*4);
    (void)run_draw(d,0x35,payload,1+count*size); free(payload);
}
static void reg_write(PtRadeon *d,uint32_t a,uint32_t v,int from_cp) {
    if(a>=REG_BYTES || (a&3)) {d->stats.unknown_writes++;return;}
    if(a>=PCI_MIRROR && a<PCI_MIRROR+256) return;                     /* read-only mirror */
    if(optics_trace() && (a<0x2000 || a>=0x5000)) {   /* development trace: first write of each non-3D register (value, MMIO or CP) */
        static uint8_t seen[0x10000/4/8]; unsigned i=a/4;
        if(!(seen[i/8]>>(i&7)&1)){ seen[i/8]|=(uint8_t)(1u<<(i&7)); fprintf(stderr,"optics-frontend: first write %#06x = %08x%s\n",a,v,from_cp?" (CP)":""); }
    }
    if((a>=0x0b00 && a<0x0b90) || (a>=0x1400 && a<0x1700 && (a<SCRATCH_REG0 || a>=SCRATCH_REG0+32)) ||
       a==MC_FB_LOCATION || a==MC_AGP_LOCATION || a==AGP_BASE || (a>=AIC_CNTL && a<AIC_CNTL+0x18) || a==PCIGART_PT_BASE) { FILE *f=reg_log(a<0x1400); if(f) fprintf(f,"w %#06x %08x%s\n",a,v,from_cp?" cp":""); }
    if(optics_trace() && a>=0x17c0 && a<0x1800) {   /* development trace: HOST_DATA (CPU-fed 2D) writes, which this 2D engine does not execute */
        static uint64_t n; n++; if(n<=16 || !(n&0xfffff)) fprintf(stderr,"optics-frontend: HOST_DATA write #%llu at %#x (GMC %08x)\n",(unsigned long long)n,a,d->reg[DP_GUI_MASTER_CNTL/4]);
    }
    if(optics_trace() && ((a>=0x200 && a<0x240) || (a>=0x300 && a<0x400) || a==0x50 || a==0x54) && d->reg[a/4]!=v) {   /* development trace: CRTC */
        static unsigned n; if(n++<96) fprintf(stderr,"optics-frontend: display %#x %#x -> %#x%s\n",a,d->reg[a/4],v,from_cp?" (CP)":"");
    }
    if(d->view_count && d->reg[a/4]!=v) {   /* lanes change: the CPU's bytes there go back with the old lanes first */
        if(a==SURFACE_CNTL) view_sync(d,0,d->vram_bytes);
        else if(a>=0x0b04 && a<0x0b84 && (a-0x0b04)%16<12) {   /* SURFACE region bounds/info: the old and the new range */
            unsigned i=(a-0x0b04)/16; uint32_t lo=d->reg[(0x0b04+16*i)/4],hi=d->reg[(0x0b08+16*i)/4];
            if(hi>lo) view_sync(d,lo,(uint64_t)hi-lo+1);
            uint32_t nlo=a==0x0b04+16*i?v:lo,nhi=a==0x0b08+16*i?v:hi; if(nhi>nlo) view_sync(d,nlo,(uint64_t)nhi-nlo+1);
        }
    }
    d->reg[a/4]=v;
    switch(a) {
    case MM_DATA: if(d->reg[MM_INDEX/4]<REG_BYTES && d->reg[MM_INDEX/4]!=MM_DATA) reg_write(d,d->reg[MM_INDEX/4]&~3u,v,from_cp); break;
    case CLOCK_CNTL_DATA: if(d->reg[CLOCK_CNTL_INDEX/4]&0x80) d->pll[d->reg[CLOCK_CNTL_INDEX/4]&63]=v; break;
    case CP_RB_BASE: case CP_RB_CNTL: rb_setup(d); break;
    case CP_RB_RPTR_ADDR: cp_writeback_rptr(d); break;
    case GEN_INT_CNTL: irq_update(d); break;
    case GEN_INT_STATUS: d->int_status&=~(v&(INT_VBLANK|INT_VBLANK2|INT_SW)); if(v&INT_SW_FIRE) d->int_status|=INT_SW; irq_update(d); break;
    case CRTC_STATUS: d->crtc_status&=~(v&CRTC_VBLANK_SAVE); break;
    case CP_RB_WPTR: if(from_cp) break;
        if(!d->cp_async){ cp_run(d,1); break; }
        d->cp_kicked=1; if(d->cp_sleeping) pt_event_signal(&d->cp_event); break;   /* a running CP sees cp_kicked before it sleeps */
    case CP_IB_BUFSZ: cp_launch_ib(d); break;
    case DST_OFFSET: d->dst_offset=v; break;
    case DST_PITCH: d->dst_pitch=v; break;
    case SRC_OFFSET: d->src_offset=v; break;
    case SRC_PITCH: d->src_pitch=v; break;
    case DST_PITCH_OFFSET: d->dst_offset=(v&0x3fffff)<<10; d->dst_pitch=(v>>22&0xff)*64; break;   /* Unverified: R100 alias */
    case SRC_PITCH_OFFSET: d->src_offset=(v&0x3fffff)<<10; d->src_pitch=(v>>22&0xff)*64; break;
    case DST_HEIGHT_WIDTH: blit(d,0); break;
    case 0x16c0: d->direction_written=1;break;
    case SURFACE_CNTL: d->surface_written=1; direct_update(d); break;
    default:
        if(a>=0x0b04 && a<0x0b84 && v){ static unsigned shown; if(shown++<32) fprintf(stderr,"optics-frontend: SURFACE%u region register %#x = %08x\n",(a-0x0b04)/16,a,v); }
        if(a>=0x0b04 && a<0x0b84) direct_update(d);
        if(a>=SCRATCH_REG0 && a<SCRATCH_REG0+32) {
            unsigned i=(a-SCRATCH_REG0)/4;
            if(d->reg[SCRATCH_UMSK/4]>>i&1) writeback(d,d->reg[SCRATCH_ADDR/4]+4*i,v);
        } else if(a>=0x2000 && a<0x5000) {
            { FILE *f=reg_log(0); if(f) fprintf(f,"3d %04x %08x%s\n",a,v,from_cp?"":" mmio"); }   /* OPTICS_REG_LOG: every 3D register write */
            if(a==0x2084 || (a>=0x2300 && a<=0x24ac)) vtx_write(d,a,v,from_cp);
            if(a==ZB_ZPASS_DATA){ d->zpass=v; d->zpass_draws=0; d->zpass_open=1; if(!++d->zpass_query) d->zpass_query=1; }
            else if(a==ZB_ZPASS_ADDR) zpass_end(d,v);
            if(d->r300) (void)optics_r300_write(d->r300,a,v);
        }
    }
}
static uint32_t reg_read_value(PtRadeon *d,uint32_t a);
static uint32_t reg_read(PtRadeon *d,uint32_t a) {   /* development trace: first read of each register, with the value */
    uint32_t v=reg_read_value(d,a);
    if(a>=0x2000 && a<0x5000){ FILE *f=reg_log(0); if(f) fprintf(f,"r %04x %08x\n",a,v); }   /* OPTICS_REG_LOG: CPU reads of 3D registers */
    if(optics_trace() && a<REG_BYTES) { static uint8_t seen[0x10000/4/8]; unsigned i=a/4;
        if(!(seen[i/8]>>(i&7)&1)){ seen[i/8]|=(uint8_t)(1u<<(i&7)); fprintf(stderr,"optics-frontend: first read %#06x = %08x\n",a,v); } }
    return v;
}
static uint32_t reg_read_value(PtRadeon *d,uint32_t a) {
    if(a>=REG_BYTES) return 0;
    if(a==PCI_MIRROR+0x5c) return rd32le(d->pci+0x5c)&~4u;   /* AGP status: 1x/2x only (an observed value the driver accepts) */
    if(a>=PCI_MIRROR && a<PCI_MIRROR+256) return rd32le(d->pci+(a-PCI_MIRROR));
    switch(a&~3u) {
    case MM_DATA: return d->reg[MM_INDEX/4]<REG_BYTES && d->reg[MM_INDEX/4]!=MM_DATA ? reg_read_value(d,d->reg[MM_INDEX/4]&~3u) : 0;
    case CLOCK_CNTL_DATA: return d->pll[d->reg[CLOCK_CNTL_INDEX/4]&63];
    case CONFIG_MEMSIZE: return (uint32_t)(d->vram_bytes-(4u<<20)); /* Unverified: 4 MiB less than the VRAM, an observed value the driver accepts */
    case RBBM_STATUS:   /* GUI_ACTIVE (bit 31, radeon_reg.h RBBM_ACTIVE) until the CP and every earlier GPU command are done */
        if(!d->cp_async && cp_pending(d)) cp_run(d,0);   /* a CP thread reports its progress instead */
        d->stats.rbbm_reads++;
        return cp_busy(d) || (exec_ops.fence && exec_ops.fence(d->exec,d)!=OPTICS_FENCE_DONE) ? 0x80000040u : 0x40u;   /* 64 FIFO entries */
    case WAIT_UNTIL: return 0;
    case CRTC_VLINE_CRNT_VLINE: {
        uint32_t lines=(d->reg[CRTC_V_TOTAL_DISP/4]>>16&0xfff)+1+38; uint64_t period=16666667;
        return (uint32_t)((d->guest_ns%period)*lines/period)<<16;
    }
    case GEN_INT_STATUS: return d->int_status;
    case CRTC_STATUS: {   /* in blank while the CRTC_VLINE model is past the visible lines */
        uint32_t visible=(d->reg[CRTC_V_TOTAL_DISP/4]>>16&0xfff)+1,lines=visible+38; uint64_t period=16666667;
        return d->crtc_status|((d->guest_ns%period)*lines/period>=visible?CRTC_VBLANK_NOW:0);
    }
    case CP_RB_RPTR: return d->reg[CP_RB_RPTR/4];
    default: return d->reg[(a&~3u)/4];
    }
}

/* ---- Command processor ---- */
static int optics_ring_read(void *o,uint32_t first,uint32_t words,void *le) {
    PtRadeon *d=o; if(!gpu_copy(d,(uint64_t)d->reg[CP_RB_BASE/4]+4ull*first,le,4ull*words,0)) return -1;
    cp_swap(d,le,4ull*words); return 0;
}
static PtR2IoResult ib_read(void *o,uint64_t a,void *le,size_t n) {
    PtRadeon *d=o; if(!gpu_copy(d,a,le,n,0)) return PT_R2_IO_MAPPING;
    cp_swap(d,le,n); return PT_R2_IO_OK;
}
static int fetch_gpu(void *o,uint64_t gpu,void *buf,size_t n) { return gpu_copy(o,gpu,buf,n,0); }
/* Registers whose CP write tells the guest that earlier GPU work is complete: the scratch registers (and their
 * memory write-back) and the software interrupt. Completion effects are queued once, behind an accepted GPU prefix;
 * only capacity/backpressure holds the whole packet unexecuted. Later GPU work is ordered on the same Metal queue.
 * Other WAIT_UNTIL conditions are not modelled by this GPU-idle completion contract. */
static int completion_register(uint32_t a,uint32_t v) {
    return (a>=SCRATCH_REG0 && a<SCRATCH_REG0+32) || (a==GEN_INT_STATUS && (v&INT_SW_FIRE));
}
/* A later CP acknowledge must not overtake the SW interrupt it acknowledges. */
static int ordered_completion(uint32_t a,uint32_t v) {
    return completion_register(a,v) || a==GEN_INT_STATUS;
}
static int strict(void) { static int on=-1; if(on<0) on=getenv("OPTICS_STRICT") && atoi(getenv("OPTICS_STRICT")); return on; }
/* R3xx-R5xx Acceleration Guide 1.5, sections 6.2.2 and 6.2.2.9.
 * This profile uses the renderer's canonical linear VRAM for macro-only surfaces;
 * physical tile reinterpretation, microtiles, clipping and other ROPs are refused. */
/* 0x1B (BITBLT_MULTI without CNTL) carries rectangles only and uses the 2D state in force, as the preceding CNTL
 * packet left it: DP_GUI_MASTER_CNTL and the pitch/offset it selected (one or two follow each 0x9B). */
static int bitblt_multi(PtRadeon *d,const PtR2Pm4Packet *p) {
    int cntl=p->opcode&0x80;uint32_t g,src,dst;unsigned k=cntl?1:0;
    if(!cntl)g=d->reg[DP_GUI_MASTER_CNTL/4];else if(!pt_r2_pm4_word(p,0,&g))return 0;
    if((g>>12&3)!=3||(g>>4&15)!=15||(g&0x8800000cu)||(g>>16&255)!=0xcc||(g>>24&7)!=2)return 0;
    src=dst=d->reg[DEFAULT_PITCH_OFFSET/4];
    if(g&1){if(!cntl)src=d->reg[SRC_PITCH_OFFSET/4];else if(!pt_r2_pm4_word(p,k++,&src))return 0;}
    if(g&2){if(!cntl)dst=d->reg[DST_PITCH_OFFSET/4];else if(!pt_r2_pm4_word(p,k++,&dst))return 0;}
    unsigned bytes=dst_bytes(g>>8&15);if(!bytes||(src|dst)&0x80000000u||k>=p->words||(p->words-k)%3)return 0;
    uint32_t so=(src&0x3fffffu)<<10,sp=(src>>22&255)*64,doff=(dst&0x3fffffu)<<10,dp=(dst>>22&255)*64;
    if(!sp||!dp)return 0;
    for(unsigned j=k;j<p->words;j+=3) {
        uint32_t s,t,wh;pt_r2_pm4_word(p,j,&s);pt_r2_pm4_word(p,j+1,&t);pt_r2_pm4_word(p,j+2,&wh);
        unsigned sx=s>>16,sy=s&65535,dx=t>>16,dy=t&65535,w=wh>>16,h=wh&65535;
        if(!w||!h||sx>8191||sy>8191||dx>8191||dy>8191||w>16383||h>16383||
           (uint64_t)(sx+w)*bytes>sp||(uint64_t)(dx+w)*bytes>dp||(uint64_t)w*h*bytes>(64u<<20))return 0;
        /* All spans must be VRAM in the canonical profile; no GART remap can redirect a copy. */
        uint64_t s0,t0,s1,t1;
        if(!optics_frontend_vram_offset(d,(uint64_t)so+(uint64_t)sy*sp+sx*bytes,&s0)||
           !optics_frontend_vram_offset(d,(uint64_t)doff+(uint64_t)dy*dp+dx*bytes,&t0)||
           !optics_frontend_vram_offset(d,(uint64_t)so+(uint64_t)(sy+h-1)*sp+(sx+w)*bytes-1,&s1)||
           !optics_frontend_vram_offset(d,(uint64_t)doff+(uint64_t)(dy+h-1)*dp+(dx+w)*bytes-1,&t1))return 0;
    }
    if(cntl){reg_write(d,DP_GUI_MASTER_CNTL,g,1);
        if(g&1)reg_write(d,SRC_PITCH_OFFSET,src,1);if(g&2)reg_write(d,DST_PITCH_OFFSET,dst,1);
        if(g&(1u<<30))reg_write(d,DP_WRITE_MASK,0xffffffffu,1);}
    for(unsigned j=k;j<p->words;j+=3) {
        uint32_t s,t,wh;pt_r2_pm4_word(p,j,&s);pt_r2_pm4_word(p,j+1,&t);pt_r2_pm4_word(p,j+2,&wh);
        d->reg[SRC_Y_X/4]=s>>16|s<<16;d->reg[DST_Y_X/4]=t>>16|t<<16;d->reg[DST_HEIGHT_WIDTH/4]=wh>>16|wh<<16;
        uint64_t refused=d->stats.blits_refused;blit(d,1);if(d->stats.blits_refused!=refused)return 0;
    }return 1;
}
/* Deferred guest completion: a completion write whose earlier GPU work still runs is recorded with a timeline fence
 * covering every job issued so far, and published (reg_write: register, scratch write-back, SW interrupt) in order once
 * that work completed successfully; a failed batch never publishes and fails the device. The CP meanwhile continues:
 * later GPU work is ordered after it on the same queue, its inputs are snapshots taken at decode, and every CPU, 2D,
 * fetch or scanout consumer of GPU-produced bytes materializes them first. RPTR is acceptance, not completion. */
static void publish_completions(PtRadeon *d) {
    if(!d->defer_count) return;
    if(exec_ops.poll && exec_ops.poll(d->exec,d)){ failed(d,"GPU completion poll"); return; }
    while(d->defer_count) {
        unsigned i=d->defer_head; PtR2TimelineResult r=pt_r2_fence_poll(d->tl,d->defer[i].fence);
        if(r==PT_R2_PENDING) break;
        (void)pt_r2_fence_release(d->tl,d->defer[i].fence); d->defer_head=(i+1)%DEFER_MAX; d->defer_count--; d->cp_progress++;
        if(r!=PT_R2_READY){ failed(d,"GPU work before a completion fence failed"); d->defer_count=0; break; }
        DeferredCompletion *c=&d->defer[i]; int query=c->a==ZB_ZPASS_ADDR; uint32_t value=c->v;
        if(query) {   /* the executor's count for query c->v, in the order the driver's lwz reads (unverified) */
            uint64_t n=0; if(exec_ops.query(d->exec,d,c->v,&n)) n=0;
            value=__builtin_bswap32((uint32_t)(n<0xfffffffeu?n:0xfffffffeu));
            query_counted(d,c->v,n);
        }
        if(query || (c->a>=SCRATCH_REG0 && c->a<SCRATCH_REG0+32)) {
            /* Validate ownership before the first byte changes, including a split mapping. */
            for(unsigned k=0;k<c->spans;k++) if(!c->destination[k].ram &&
                own_check(d,(uint64_t)(c->destination[k].p-d->vram),c->destination[k].n,1)) return;
            uint8_t bytes[4]; wr32le(bytes,value); size_t at=0; atomic_thread_fence(memory_order_release);
            for(unsigned k=0;k<c->spans;k++) {
                Span s=c->destination[k]; memcpy(s.p,bytes+at,s.n); at+=s.n;
                if(s.ram && d->config.ram_written) d->config.ram_written(d->config.map_opaque,(uint64_t)(s.p-d->config.ram.bytes),s.n);
            }
            if(c->spans) {
                d->stats.writebacks++;
                if(wb_hook) wb_hook(wb_opaque,c->destination[0].ram?
                    (uint64_t)(c->destination[0].p-d->config.ram.bytes):UINT64_MAX,value);
            }
            if(!query) d->reg[c->a/4]=c->v;
        } else reg_write(d,c->a,c->v,1);
    }
    d->cp_wait=d->defer_count!=0 || d->held;
}
static void defer_completion(PtRadeon *d,uint32_t a,uint32_t v) {   /* capacity checked by admit_completion */
    unsigned i=(d->defer_head+d->defer_count)%DEFER_MAX;
    DeferredCompletion c={.a=a,.v=v};
    if(a>=SCRATCH_REG0 && a<SCRATCH_REG0+32 && (d->reg[SCRATCH_UMSK/4]>>((a-SCRATCH_REG0)/4)&1)) {
        uint64_t gpu=(uint64_t)d->reg[SCRATCH_ADDR/4]+a-SCRATCH_REG0;
        for(size_t left=4;left;) {
            Span s; if(!translate(d,gpu,&s) || !s.n){ failed(d,"completion write-back mapping"); return; }
            if(s.n>left) s.n=left; c.destination[c.spans++]=s; gpu+=s.n; left-=s.n;
        }
    }
    if(pt_r2_fence_create(d->tl,d->issued,NULL,0,&c.fence)!=PT_R2_READY){ failed(d,"completion ledger"); return; }
    d->defer[i]=c; d->defer_count++; d->stats.completions_deferred++; d->cp_wait=1;
}
/* An occlusion query's result word, published once the fence over its draws completed. 0 = queued; nonzero: the caller
 * writes the estimate now (queue full, no timeline, unmapped address). */
static int defer_query(PtRadeon *d,uint32_t address,uint32_t query) {
    if(!d->tl || d->defer_count>=DEFER_MAX) return 1;
    DeferredCompletion c={.a=ZB_ZPASS_ADDR,.v=query}; uint64_t gpu=address&~3u;
    for(size_t left=4;left;) { Span s; if(!translate(d,gpu,&s) || !s.n) return 1; if(s.n>left) s.n=left; c.destination[c.spans++]=s; gpu+=s.n; left-=s.n; }
    if(exec_ops.fence && exec_ops.fence(d->exec,d)==OPTICS_FENCE_FAILED){ failed(d,"GPU work before an occlusion query failed"); return 0; }   /* commits them */
    if(pt_r2_fence_create(d->tl,d->issued,NULL,0,&c.fence)!=PT_R2_READY) return 1;
    unsigned i=(d->defer_head+d->defer_count)%DEFER_MAX; d->defer[i]=c; d->defer_count++; d->cp_wait=1;
    return 0;
}
static int early_completion(void) {
    static int early=-1;
    if(early<0){ const char *v=getenv("OPTICS_EARLY_FENCE"); early=!v || atoi(v)!=0; if(!early) fprintf(stderr,"optics-frontend: OPTICS_EARLY_FENCE=0: completion waits for the GPU\n"); }
    return early;
}
/* ring/IB execute codes: 0 admitted (publish now), 3 admitted with deferred completion writes, 1 busy, 2 failed */
static int admit_completion(PtRadeon *d,unsigned writes) {
    /* Completion is published when the CP reaches it (as AMD's CP does for SCRATCH: "the CP got here"),
     * not when Metal finished. Sound by the contract above: the GPU work before it is already decoded with snapshot
     * inputs, ordered on one queue, and every consumer of GPU-produced bytes materializes them (commit + wait) first.
     * That holds while the GPU writes guest RAM (GART) only synchronously: a target there is written back before its draw
     * returns (frontend_metal.mm import_ram), since no CPU read of guest RAM traps. The executor then commits per batch
     * (OPTICS_COMMIT_DRAWS), at scanout and at materialize, not per guest fence (Xbench Spinning Squares: one fence and one
     * Metal commit per draw kept four batches in flight, about 24 fps). Diagnostic switches: OPTICS_EARLY_FENCE=0 restores
     * the deferred completion; OPTICS_FENCE_BLOCK=1 holds the CP at the packet until the GPU completed. */
    static int block=-1; int early=early_completion();
    if(block<0){ block=getenv("OPTICS_FENCE_BLOCK") && atoi(getenv("OPTICS_FENCE_BLOCK")); if(block) fprintf(stderr,"optics-frontend: OPTICS_FENCE_BLOCK diagnostic: the CP waits at completion packets\n"); }
    if(!exec_ops.fence || (early && !d->defer_count)) return 0;   /* behind a deferred write (a query word): keep the order */
    /* Arm before submission/poll: a fast callback must not fall between observing PENDING and enabling its wake. */
    int was_held=d->held; d->held=1; d->cp_wait=1;
    int r=exec_ops.fence(d->exec,d);   /* commits the covered prefix */
    if(r==OPTICS_FENCE_FAILED){ failed(d,"GPU work before a completion fence failed"); return 2; }
    publish_completions(d); if(d->stats.failed) return 2;
    if(r==OPTICS_FENCE_DONE && !d->defer_count){ d->held=0; d->cp_wait=0; return 0; }
    if(writes>DEFER_MAX){ failed(d,"completion packet exceeds deferred queue capacity"); return 2; }
    if(!block && d->tl && DEFER_MAX-d->defer_count>=writes){ d->held=0; return 3; }   /* defer_completion keeps cp_wait */
    if(!was_held) d->stats.fence_waits++;
    d->held=1; d->cp_wait=1; return 1;   /* the whole packet stays in the ring/IB, unexecuted */
}
/* One draw: built from the packet payload (IMMD_2/VBUF_2/INDX_2 words) against the register state and run by the
 * executor. 0, or 2 to stop the CP (a GPU failure; OPTICS_STRICT=1 also for a refused draw). */
static int run_draw(PtRadeon *d,uint8_t opcode,const uint32_t *payload,uint32_t words) {
    OpticsDraw draw; const char *why="no draw executor installed";
    /* Vertex arrays: VAP_VTX_NUM_ARRAYS, then per pair VAP_VTX_AOS_ATTR (comps|stride<<8, two halves) and two addresses. */
    OpticsAos aos={0}; aos.count=d->reg[0x20c0/4]&31; if(aos.count>16) aos.count=16;
    for(uint32_t k=0;k<aos.count;k++){uint32_t attr=d->reg[(0x20c4+12*(k/2))/4]>>(16*(k&1));
        aos.array[k].components=attr&0x7f; aos.array[k].stride=attr>>8&0x7f; aos.array[k].offset=d->reg[(0x20c8+12*(k/2)+4*(k&1))/4];
        uint64_t vo; if(optics_frontend_vram_offset(d,aos.array[k].offset,&vo)) aos.local|=1u<<k;}
    uint64_t t0=rate_on()?mono_ns():0;
    int built=payload && optics_draw_build(&draw,d->reg,d->r300,opcode,payload,words,&aos,fetch_gpu,d,&why);
    uint64_t t1=t0?mono_ns():0; d->rate_build+=t1-t0;
    draw.query=d->zpass_open?d->zpass_query:0;
    d->in_executor=1; int result=OPTICS_EXEC_REFUSED;
    if(built && exec_ops.draw && draw.poly_front!=draw.poly_back && !draw.lines && !draw.sprite) {
        /* glPolygonMode with different modes per face: the front faces in theirs (back faces culled), then the back
         * faces in theirs. Approximation: a face in point mode beside another mode is drawn as lines. */
        OpticsDraw *back=malloc(sizeof *back);
        if(back) { memcpy(back,&draw,sizeof draw);   /* shares the arrays: the original alone is freed */
            draw.cull|=2; draw.fill=draw.poly_front!=2; back->cull|=1; back->fill=back->poly_back!=2;
            if(!draw.poly_front || !draw.poly_back) draw.approx|=OPTICS_APPROX_FIXED;
            result=exec_ops.draw(d->exec,d,&draw,&why);
            if(result==OPTICS_EXEC_DONE) result=exec_ops.draw(d->exec,d,back,&why);
            free(back); }
    } else if(built && exec_ops.draw) result=exec_ops.draw(d->exec,d,&draw,&why);
    d->in_executor=0;
    if(t1) d->rate_exec+=mono_ns()-t1;
    if(result==OPTICS_EXEC_DONE){ (void)optics_frontend_job(d); if(!query_gpu()) zpass_draw(d,&draw); else d->zpass_draws+=d->zpass_open;
        d->stats.approx_mip+=draw.approx&OPTICS_APPROX_MIP?1:0; d->stats.approx_border+=draw.approx&OPTICS_APPROX_BORDER?1:0;
        d->stats.approx_aniso+=draw.approx&OPTICS_APPROX_ANISO?1:0; d->stats.approx_tiled+=draw.approx&OPTICS_APPROX_TILED?1:0;
        d->stats.approx_signed+=draw.approx&OPTICS_APPROX_SIGNED?1:0; }
    if(payload) optics_draw_free(&draw);   /* the builder zeroes first: a failed build is freed too */
    if(result==OPTICS_EXEC_FAILED){ failed(d,why); return 2; }
    int done=result==OPTICS_EXEC_DONE;
    if(done) d->stats.draws++;
    else {
        static unsigned shown; if(shown++<LOG_LIMIT) fprintf(stderr,"optics-frontend: draw refused (opcode %02x): %s\n",opcode,why);
        d->stats.draws_refused++;
    }
    if(done) why="executed";
    { FILE *f=reg_log(0); if(f) { uint32_t en=d->reg[0x4104/4]&0xffff;
        fprintf(f,"draw %02x %s approx %x cb %08x pitch %08x tx_enable %04x",opcode,why,built?draw.approx:0,d->reg[0x4e28/4],d->reg[0x4e38/4],en);
        for(unsigned u=0;u<16;u++) if(en>>u&1) fprintf(f," | t%u off %08x f0 %08x f1 %08x f2 %08x",u,d->reg[(0x4540+4*u)/4],d->reg[(0x4480+4*u)/4],d->reg[(0x44c0+4*u)/4],d->reg[(0x4500+4*u)/4]);
        fputc('\n',f); } }
    static FILE *log; static int checked; static const char *reasons[64]; static uint64_t counts[64];
    if(!checked){checked=1;const char *e=getenv("OPTICS_DRAW_LOG");if(e)log=fopen(e,"w");}
    if(log) {   /* OPTICS_DRAW_LOG=FILE: counts per refusal reason (development), rewritten every 4096 draws */
        unsigned k=0; while(k<64 && reasons[k] && strcmp(reasons[k],why)) k++;
        int fresh=k<64 && !reasons[k]; if(k<64){ if(!reasons[k]) reasons[k]=why; counts[k]++; }
        if(fresh || ((d->stats.draws+d->stats.draws_refused)&255)==0) {
            rewind(log); for(unsigned i=0;i<64 && reasons[i];i++) fprintf(log,"%12llu %s\n",(unsigned long long)counts[i],reasons[i]);
            fprintf(log,"blits %llu refused %llu (last GMC %08x); packets refused %llu (last opcode %02x)\n",(unsigned long long)d->stats.blits,
                (unsigned long long)d->stats.blits_refused,d->stats.last_refused_gmc,(unsigned long long)d->stats.packets_refused,d->stats.last_refused_opcode);
            fflush(log);
        }
    }
    if(done) return 0;
    if(strict()) return 2;   /* OPTICS_STRICT=1: an unexecuted draw stops the CP instead of the DEGRADED default */
    d->stats.last_refused_opcode=opcode;
    return 0;
}
static int execute(PtRadeon *d,const PtR2Pm4Packet *p) {
    if(d->stats.failed) return 2;
    if(p->type==0) {
        unsigned writes=0; int defer=0;   /* early completion admits every packet at once: nothing to count (one pass, not two) */
        if(exec_ops.fence && (!early_completion() || d->defer_count)) for(uint32_t i=0;i<p->words;i++){uint32_t a,v; if(pt_r2_pm4_write(p,i,&a,&v) && ordered_completion(a,v)) writes++;}
        if(writes){ int r=admit_completion(d,writes); if(r==1 || r==2) return r; defer=r==3; }
        d->stats.packets++;
        for(uint32_t i=0;i<p->words;i++){uint32_t a,v;if(pt_r2_pm4_write(p,i,&a,&v)){
            if(defer && ordered_completion(a,v)) defer_completion(d,a,v); else reg_write(d,a,v,1);
            d->stats.type0_words++;}}
        return d->stats.failed?2:0;
    }
    d->stats.packets++;
    if(p->type==2 || (p->type==3 && p->opcode==0x10)) return 0;       /* filler, NOP */
    if(p->type==3&&(p->opcode==0x9b||p->opcode==0x1b)){
        /* Diagnostic switch OPTICS_BITBLT_MULTI=0 refuses 0x9B/0x1B. */
        static int on=-1;if(on<0){const char *v=getenv("OPTICS_BITBLT_MULTI");on=!v||atoi(v)!=0;}
        if(on&&bitblt_multi(d,p))return 0;
    }
    OpticsStateEffect effect=OPTICS_EFFECT_NONE;
    if(d->r300) (void)optics_r300_packet(d->r300,p,&effect);
    /* HyperZ / colour-compression fast clears (r300_reg.h: 3D_CLEAR_ZMASK 0x32, 3D_CLEAR_HIZ 0x37, 3D_CLEAR_CMASK 0x38;
     * payload: first dword, dword count, value). This GPU model keeps no compression masks, so a mask cleared to 0 is
     * executed as what it stands for: every tile of the depth buffer at ZB_DEPTHOFFSET (colour buffer RB3D_COLOROFFSET0)
     * reads the clear value. The hardware reads ZB_DEPTHCLEARVALUE / RB3D_COLOR_CLEAR_VALUE when a cleared tile is
     * used, not when the mask is written, so the executor applies the clear lazily with the values in force at that
     * use. HiZ is a conservative acceleration structure: its clear has no visible effect here. Unverified: the mask covers
     * the whole buffer (count = its 8x8 tiles at 4 bits each, as observed for 1088x768). */
    if(p->type==3 && (p->opcode==0x32 || p->opcode==0x37 || p->opcode==0x38) && p->words==3) {
        uint32_t w[3]; for(uint32_t i=0;i<3;i++) pt_r2_pm4_word(p,i,&w[i]);
        if(p->opcode==0x37) return 0;
        if(!w[0] && !w[2] && exec_ops.clear && !exec_ops.clear(d->exec,d,(p->opcode==0x32?d->reg[0x4f20/4]:d->reg[0x4e28/4])&~31u,p->opcode==0x32?2:1))
            return 0;
    }
    if(p->type==3 && p->opcode==0x2f) {   /* 3D_LOAD_VBPNTR writes the VAP_VTX_NUM_ARRAYS / AOS registers */
        uint32_t w[1+3*8]={0},n=p->words<25?p->words:25; for(uint32_t i=0;i<n;i++) pt_r2_pm4_word(p,i,&w[i]);
        reg_write(d,0x20c0,w[0],1); for(uint32_t i=1;i<n;i++) reg_write(d,0x20c4+4*(i-1),w[i],1);
        return 0;
    }
    if(effect==OPTICS_EFFECT_DRAW) {
        uint32_t *payload=malloc((p->words?p->words:1)*4u);
        if(payload) for(uint32_t i=0;i<p->words;i++) pt_r2_pm4_word(p,i,&payload[i]);
        int r=run_draw(d,p->opcode,payload,p->words); free(payload); return r;
    }
    d->stats.packets_refused++; d->stats.last_refused_opcode=p->opcode;
    {   static unsigned shown; uint32_t w[3]={0,0,0}; for(uint32_t i=0;i<3 && i<p->words;i++) pt_r2_pm4_word(p,i,&w[i]);
        if(shown++<48) fprintf(stderr,"optics-frontend: packet3 %02x refused (%u words: %08x %08x %08x): not executed by the own frontend\n",p->opcode,p->words,w[0],w[1],w[2]);
        { FILE *f=reg_log(0); if(f) { fprintf(f,"p3 %02x refused %u words:",p->opcode,p->words);   /* OPTICS_REG_LOG: every word, up to 16 */
            for(uint32_t i=0;i<p->words && i<16;i++){ uint32_t v=0; pt_r2_pm4_word(p,i,&v); fprintf(f," %08x",v); } fputc('\n',f); } } }
    /* DEGRADED default: the CP progresses past the refusal, which is counted and logged, never reported as executed. */
    return strict() ? 2 : 0;
}
static int optics_ring_execute(void *o,const PtR2Pm4Packet *p,uint64_t epoch) { (void)epoch; int r=execute(o,p); if(!r) ((PtRadeon *)o)->cp_progress++; return r; }
static int stream_execute(void *o,const PtR2Pm4Packet *p,uint64_t epoch) { (void)epoch; int r=execute(o,p); if(!r) ((PtRadeon *)o)->cp_progress++; return r; }
static int cp_launch_ib(PtRadeon *d) {
    uint32_t words=d->reg[CP_IB_BUFSZ/4]&0xfffff; if(!words) return 0;
    if(!d->stream) d->stream=optics_stream_create(IB_DEPTH,IB_WORDS,ib_read,stream_execute,d);
    PtR2IoResult r=d->stream?optics_stream_push(d->stream,d->reg[CP_IB_BASE/4]&~3u,words):PT_R2_IO_CAPACITY;
    if(r!=PT_R2_IO_OK){d->stats.packets_refused++;note(d,"IB at %08x (%u words) refused",d->reg[CP_IB_BASE/4],words);return -1;}
    d->stats.ib_launches++; return 0;
}
/* kicked: a WPTR write (the RPTR write-back follows every kick, as before); a resumed CP writes RPTR back only when it moved. */
static void cp_run_body(PtRadeon *d,int kicked);
static void cp_run(PtRadeon *d,int kicked) {
    if(!rate_on() || d->rate_depth){ cp_run_body(d,kicked); return; }
    d->rate_depth=1; uint64_t t=mono_ns(); cp_run_body(d,kicked); d->rate_cp+=mono_ns()-t; d->rate_depth=0;
}
static void rate_tick(PtRadeon *d) {
    uint64_t now=mono_ns(); if(!d->rate_last){ d->rate_last=now; return; }
    if(now-d->rate_last<1000000000ull) return;
    OpticsStatus g={0}; if(exec_ops.status) exec_ops.status(d->exec,d,&g);
    uint64_t cur[4]={d->stats.draws,d->stats.completions_deferred,g.metric[OPTICS_BATCHES],g.metric[OPTICS_GPU_NS]},dv[4];
    for(int i=0;i<4;i++){ dv[i]=cur[i]-d->rate_prev[i]; d->rate_prev[i]=cur[i]; }
    double s=(now-d->rate_last)/1e9;
    PtR2IoStats io; pt_r2_io_stats(d->io,&io);
    fprintf(stderr,"optics-rate: %.2f s: draws %llu, CP %.1f ms (build %.1f, executor %.1f), deferred %llu, waits %llu, batches %llu, GPU %.1f ms; "
        "CPU VRAM reads %llu writes %llu (over 20 us: %llu, lock %.1f ms, materialize %.1f ms, last at %#x), CP hand-overs %llu\n",s,
        (unsigned long long)dv[0],d->rate_cp/1e6,d->rate_build/1e6,d->rate_exec/1e6,(unsigned long long)dv[1],(unsigned long long)d->stats.fence_waits,(unsigned long long)dv[2],dv[3]/1e6,
        (unsigned long long)(io.cpu_reads-d->rate_io[0]),(unsigned long long)(io.cpu_writes-d->rate_io[1]),(unsigned long long)d->rate_slow,
        d->rate_slow_lock/1e6,d->rate_slow_own/1e6,d->rate_slow_at,(unsigned long long)d->rate_yields);
    d->rate_last=now; d->rate_cp=d->rate_build=d->rate_exec=d->rate_yields=0; d->rate_slow=d->rate_slow_lock=d->rate_slow_own=0; d->rate_io[0]=io.cpu_reads; d->rate_io[1]=io.cpu_writes;
}
static void cp_run_body(PtRadeon *d,int kicked) {
    uint32_t rptr=d->ring.rptr;
    publish_completions(d);
    if(!d->ring_ready || d->ring_stalled || d->stats.failed) return;
    if(pt_r2_ring_wptr(&d->ring,d->reg[CP_RB_WPTR/4]&(d->ring_words-1))){d->ring_stalled=1;note(d,"WPTR %u beyond free ring space (rptr %u)",d->reg[CP_RB_WPTR/4],d->ring.rptr);return;}
    /* A CP thread takes one IB packet per step, so a waiting caller gets the device between two packets. */
    unsigned slice=d->cp_async?1:1u<<20;
    for(unsigned guard=0;guard<RING_BUDGET;guard++) {
        if(cp_yield(d)) return;   /* reset or close meanwhile: nothing of the old run is touched again */
        if(!d->ring_ready) break;   /* the ring was reprogrammed meanwhile */
        if(d->stream) {   /* an indirect buffer in progress completes before the next root packet */
            unsigned n; OpticsStreamResult r=optics_stream_pump(d->stream,slice,&n);
            if(r==OPTICS_STREAM_BUSY) break;   /* a completion fence is pending: resumes at the same packet */
            if(r==OPTICS_STREAM_BUDGET) continue;
            if(r!=OPTICS_STREAM_EMPTY) {
                note(d,"IB stream stopped (%u), %u packets",r,n); optics_stream_reset(d->stream);
                if(d->stats.failed || strict()){ d->ring_stalled=1; break; }   /* DEGRADED default: the root ring continues */
            }
        }
        uint32_t n; PtR2RingResult r=pt_r2_ring_pump(&d->ring,1,&n);   /* one root packet, then its IBs depth-first */
        d->reg[CP_RB_RPTR/4]=d->ring.rptr;
        if(n) continue;
        if(r==PT_R2_RING_EMPTY || r==PT_R2_RING_MORE || r==PT_R2_RING_BUSY) break;
        d->ring_stalled=1; note(d,"ring stopped (%u) at rptr %u",r,d->ring.rptr); break;
    }
    if(kicked || d->ring.rptr!=rptr) cp_writeback_rptr(d);
}
/* Work for the CP: a WPTR write it has not seen, a held/deferred completion, ring words or an unfinished IB. */
static int cp_pending(const PtRadeon *d) {
    return d->cp_kicked || d->cp_wait || (d->ring_ready && !d->ring_stalled && (d->ring.rptr!=d->ring.wptr || optics_stream_pending(d->stream)));
}
static int cp_busy(const PtRadeon *d) { return d->cp_running || cp_pending(d); }   /* RBBM_STATUS GUI_ACTIVE */
/* The CP thread: runs while there is work and it progresses; sleeps on cp_event otherwise (an idle ring, a partial
 * packet, a completion fence), woken by a WPTR write, a GPU completion (optics_frontend_wake) or the service tick. */
static void *cp_main(void *o) {
    PtRadeon *d=o;
#ifdef __APPLE__
    pthread_setname_np("org.ptolemy.radeon-cp");
#endif
    take(d);
    while(!d->cp_quit) {
        uint64_t g=pt_event_generation(&d->cp_event),before=d->cp_progress;
        if(cp_pending(d)){ int kicked=d->cp_kicked; d->cp_kicked=0; d->cp_running=1; cp_run(d,kicked); d->cp_running=0; }
        if(d->cp_quit || (d->cp_progress!=before && cp_pending(d))) continue;
        d->cp_sleeping=1; pthread_mutex_unlock(&d->lock);
        pt_event_wait(&d->cp_event,g,UINT64_MAX);
        take(d); d->cp_sleeping=0;
    }
    pthread_mutex_unlock(&d->lock);
    return NULL;
}

/* ---- include/radeon.h ---- */
int pt_radeon_create(PtRadeon **out,const PtRadeonConfig *c) {
    if(!out || !c || !c->ram.bytes) return EINVAL;
    unsigned mb=c->vram_mb?c->vram_mb:128; if(mb!=128 && mb!=256) return EINVAL;
    PtRadeon *d=calloc(1,sizeof *d); if(!d) return ENOMEM;
    d->config=*c; d->vram_bytes=(size_t)mb<<20;
    d->vram=mmap(NULL,d->vram_bytes,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);   /* zeroed, page (16 KiB) aligned */
    if(d->vram==MAP_FAILED) d->vram=NULL;
    d->r300=optics_r300_state_create(1);   /* explicit virtual zero reset of 3D state (provenance VIRTUAL_RESET), not a physical claim */
    d->guards=pt_r2_guard_create(16384,1u<<16,guard_apply,d);
    PtR2IoConfig ic={.address={.fb_length=d->vram_bytes,.generation=1,.vram=d->vram,.vram_bytes=d->vram_bytes},
        .pages=1u<<16,.transfers=4096,.snapshot_bytes=64u<<20,.protect=pt_r2_guard_set,.context=d,.written=io_written,.protect_context=d->guards};
    d->io=d->vram && d->guards ? pt_r2_io_create(&ic) : NULL;
    d->tl=pt_r2_timeline_create(64,1,DEFER_MAX);
    if(!d->vram || !d->r300 || !d->io || !d->tl){
        pt_r2_timeline_destroy(d->tl); pt_r2_io_destroy(d->io); pt_r2_guard_destroy(d->guards); if(d->vram) munmap(d->vram,d->vram_bytes);
        optics_r300_state_destroy(d->r300); free(d); return ENOMEM;
    }
    d->reg[MC_FB_LOCATION/4]=(uint32_t)(((d->vram_bytes>>16)-1)<<16);     /* until the driver programs the MC */
    d->reg[DP_WRITE_MASK/4]=0xffffffffu; d->epoch=1;
    (void)optics_r300_accept_register(d->r300,0x22dc,0,OPTICS_VIRTUAL_RESET);   /* R300 has no PVS flow-control register (R5xx) */
    if(pthread_mutex_init(&d->frame_lock,NULL)){pt_r2_io_destroy(d->io);pt_r2_guard_destroy(d->guards);optics_r300_state_destroy(d->r300);munmap(d->vram,d->vram_bytes);free(d);return ENOMEM;}
    d->owner=pthread_self(); d->exec=exec_ops.create?exec_ops.create(d):NULL;
    if(c->cp_thread && !pthread_mutex_init(&d->lock,NULL)){ pt_event_init(&d->cp_event); d->cp_async=1; }   /* direct VRAM stays off */
    if(c->width && c->height) boot_mode(d,c->width,c->height,32,0);
    if(d->cp_async) {
        pthread_attr_t attr; int e=pthread_attr_init(&attr);
#ifdef __APPLE__   /* at least the vCPU's class: the GPU's CP must not be left to the efficiency cores */
        if(!e){ qos_class_t q=qos_class_self(); pthread_attr_set_qos_class_np(&attr,q>QOS_CLASS_USER_INITIATED?q:QOS_CLASS_USER_INITIATED,0); }
#endif
        if(!e){ e=pthread_create(&d->cp,&attr,cp_main,d); pthread_attr_destroy(&attr); }
        if(e){ fprintf(stderr,"optics-frontend: no CP thread, the CP runs on the CPU owner\n");
            d->cp_async=0; pt_event_destroy(&d->cp_event); pthread_mutex_destroy(&d->lock); direct_update(d); }
    }
    *out=d; return 0;
}
void pt_radeon_config(PtRadeon *d,const uint8_t pci[256],const PtAgp *agp) { if(d){enter(d);memcpy(d->pci,pci,256);if(agp)d->agp=*agp;leave(d);} }
int pt_radeon_mmio(PtRadeon *d,uint32_t off,uint8_t *b,unsigned n,int wr) {
    if(!d || !b || (n!=1 && n!=2 && n!=4) || off>=REG_BYTES || off+n>REG_BYTES || (off&3)+n>4) return EINVAL;
    uint32_t a=off&~3u,shift=(off&3)*8;
    enter(d);
    if(wr) {
        uint32_t v=n==4?rd32le(b):n==2?(uint32_t)(b[0]|b[1]<<8):b[0];
        if(n!=4){uint32_t mask=(n==2?0xffffu:0xffu)<<shift;v=(d->reg[a/4]&~mask)|(v<<shift&mask);}
        reg_write(d,a,v,0);
    } else {
        uint32_t v=reg_read(d,a)>>shift;
        for(unsigned i=0;i<n;i++) b[i]=(uint8_t)(v>>8*i);
    }
    leave(d); return 0;
}
int pt_radeon_vram(PtRadeon *d,uint32_t off,uint8_t *b,unsigned n,int wr) {
    if(!d || !b) return EFAULT;
    int ap1=off>=d->vram_bytes; if(ap1) off-=(uint32_t)d->vram_bytes;   /* second aperture aliases VRAM with its own swap */
    if(off>=d->vram_bytes || n>d->vram_bytes-off) return EFAULT;
    uint64_t t0=rate_on()?mono_ns():0,t1=0;
    enter(d); if(t0) t1=mono_ns();
    if(ap1==(int)d->fb_ap && d->view_count && off%GRANULE+n<=GRANULE) {   /* a granule the CPU holds: its view (interpreter) */
        uint8_t st=d->view_state[off/GRANULE];
        if(st==2 || (st==1 && !wr)){ if(wr) memcpy(d->view+off,b,n); else memcpy(b,d->view+off,n); leave(d); return 0; }
    }
    if(own_check(d,off,n,wr)){ leave(d); return EIO; }   /* GPU-produced bytes could not be exported: never serve stale ones */
    if(t0){ uint64_t t2=mono_ns(); if(t2-t0>20000){ d->rate_slow++; d->rate_slow_lock+=t1-t0; d->rate_slow_own+=t2-t1; d->rate_slow_at=off; } }
    /* The checked copy: no GPU writer may remain, a write invalidates watched GPU copies (versions), lanes per aperture. */
    unsigned x=cpu_xor(d,ap1,off); PtR2IoResult r=wr?pt_r2_io_cpu_write(d->io,off,b,n,x):pt_r2_io_cpu_read(d->io,off,b,n,x);
    d->stats.cpu_vram_accesses++; d->stats.cpu_vram_bytes+=n;
    if(wr && x!=aperture_xor(d,ap1)){ static unsigned shown; if(shown++<16) fprintf(stderr,"optics-frontend: CPU write at VRAM %#x (aperture %d) in SURFACE%d: lanes xor %u instead of %u\n",
        off,ap1,cpu_region(d,off),x,aperture_xor(d,ap1)); }
    leave(d);
    if(r!=PT_R2_IO_OK){ static unsigned shown; if(shown++<8) fprintf(stderr,"optics-frontend: VRAM %s at %#x: %s\n",wr?"write":"read",off,pt_r2_io_result(r)); return EIO; }
    return 0;
}
/* Boot interface of the OpenBIOS fork (Bochs VBE index/data and the VGA DAC). */
static int port_access(PtRadeon *d,unsigned port,uint8_t *b,unsigned n,int wr);
int pt_radeon_port(PtRadeon *d,unsigned port,uint8_t *b,unsigned n,int wr) {
    if(!d || !b || !n) return EINVAL;
    enter(d); int r=port_access(d,port,b,n,wr); leave(d); return r;
}
static int port_access(PtRadeon *d,unsigned port,uint8_t *b,unsigned n,int wr) {
    uint32_t v=0; for(unsigned i=0;i<n && i<4;i++) v|=(uint32_t)b[i]<<8*i;
    switch(port) {
    case 0x1ce: if(wr) d->vbe_index=v&15; else {b[0]=(uint8_t)d->vbe_index;if(n>1)b[1]=0;} return 0;
    case 0x1cf: case 0x1d0:
        if(wr){d->vbe[d->vbe_index]=(uint16_t)v;if(d->vbe_index==4||d->vbe_index<=3)vbe_mode(d);}
        else {uint16_t r=d->vbe_index==0?0xb0c5:d->vbe_index==10?(uint16_t)(d->vram_bytes>>16):d->vbe[d->vbe_index];b[0]=(uint8_t)r;if(n>1)b[1]=(uint8_t)(r>>8);}
        return 0;
    case 0x3c8: if(wr){d->dac_write=v&255;d->dac_component=0;} else b[0]=(uint8_t)d->dac_write; return 0;
    case 0x3c7: if(wr){d->dac_read=v&255;d->dac_component=0;} return 0;
    case 0x3c9:
        if(wr){d->dac[d->dac_write][d->dac_component]=(uint8_t)v;if(++d->dac_component==3){d->dac[d->dac_write][3]=255;d->dac_component=0;d->dac_write=(d->dac_write+1)&255;}}
        else {b[0]=d->dac[d->dac_read][d->dac_component];if(++d->dac_component==3){d->dac_component=0;d->dac_read=(d->dac_read+1)&255;}}
        return 0;
    default: if(!wr) memset(b,0,n); return 0;      /* legacy VGA ports without a modelled effect read as zero */
    }
}
/* ---- Scanout publication (CPU owner fills, UI leases; the UI never reads VRAM or the renderer) ---- */
static void slot_drop(Slot *s) { if(s->native && exec_ops.frame_release) exec_ops.frame_release(s->native); s->native=s->texture=NULL; }
static void slot_free(Slot *s) {
    slot_drop(s);
    if(s->handle){if(storage.destroy)storage.destroy(storage.opaque,s->handle);} else free(s->bytes);
    s->handle=NULL; s->bytes=NULL; s->capacity=0;
}
/* A slot the UI does not hold and that is not the newest published frame (which stays available); unpublished while
 * it is refilled. NULL when the UI holds every other slot. */
static Slot *slot_take(PtRadeon *d) {
    Slot *best=NULL; uint64_t newest=0;
    pthread_mutex_lock(&d->frame_lock);
    for(unsigned i=0;i<FRAME_SLOTS;i++) if(d->slot[i].seq>newest) newest=d->slot[i].seq;
    for(unsigned i=0;i<FRAME_SLOTS;i++){Slot *s=&d->slot[i]; if(!s->refs && (!newest || s->seq!=newest) && (!best || s->seq<best->seq)) best=s;}
    if(best) best->seq=0;
    pthread_mutex_unlock(&d->frame_lock);
    return best;
}
/* force: a pixel copy now (CPU-owner screenshots), materializing GPU-owned scanout bytes if needed. Returns 1 when a
 * frame was published, -1 while a native frame is in flight (retry at the next service tick). */
static int publish_scanout(PtRadeon *d,int force) {
    uint32_t w,h,bpp,line,offset; if(!scanout_mode(d,&w,&h,&bpp,&line,&offset)) return 0;
    if(view_sync(d,offset,(uint64_t)line*h)) return 0;   /* CPU pixels of the view, also for a native frame */
    uint32_t mode[5]={offset,w,h,bpp,line}; void *native=NULL,*texture=NULL; int kind=OPTICS_FRAME_NONE;
    if(!force && exec_ops.frame && bpp==32) kind=exec_ops.frame(d->exec,d,offset,w,h,line,&native,&texture);
    if(kind==OPTICS_FRAME_PENDING) return -1;
    if(kind==OPTICS_FRAME_SAME) return 0;
    if(kind==OPTICS_FRAME_FAILED){ failed(d,"native scanout frame"); return 0; }
    if(kind==OPTICS_FRAME_NONE) {   /* CPU-drawn scanout: copy VRAM rows, only when something wrote VRAM since */
        /* (direct VRAM: CPU writes are not seen, copy at every display period) */
        if(!force && !d->direct && d->frame_seq && d->published_writes==d->vram_writes && !memcmp(mode,d->published_mode,sizeof mode)) return 0;
        if(own_check(d,offset,(uint64_t)line*h,0)) return 0;
    }
    Slot *s=slot_take(d); if(!s){ if(native && exec_ops.frame_release) exec_ops.frame_release(native); return 0; }
    slot_drop(s);
    PtRadeonFrame r={0};
    if(kind==OPTICS_FRAME_READY){ s->native=native; s->texture=texture; r.metal_texture=texture; d->stats.native_frames++; }
    else {
        size_t need=(size_t)line*h;
        if(s->capacity<need) {
            slot_free(s);
            if(storage.create) s->handle=storage.create(storage.opaque,need,&s->bytes); else s->bytes=malloc(need);
            if(!s->bytes){ s->handle=NULL; return 0; }
            s->capacity=need;
        }
        memcpy(s->bytes,d->vram+offset,(size_t)line*h);
        r.metal_buffer=s->handle; r.pixels=s->bytes;
        d->published_writes=d->vram_writes; d->stats.copied_frames++;
    }
    r.width=w; r.height=h; r.bpp=bpp==16 && (d->reg[CRTC_GEN_CNTL/4]>>8&7)==3 ? 15 : bpp; r.line=line;   /* 15: 1555 */
    memcpy(r.palette,d->dac,sizeof r.palette);
    unsigned x=aperture_xor(d,(int)d->fb_ap); r.big_endian=!(bpp==32 && x==3) && !(bpp==16 && x==1);
    memcpy(d->published_mode,mode,sizeof mode);
    pthread_mutex_lock(&d->frame_lock);
    r.ticket=(PtGpuTicket){d->epoch,++d->frame_seq}; r.slot=(unsigned)(s-d->slot); s->f=r; s->seq=d->frame_seq;
    pthread_mutex_unlock(&d->frame_lock);
    d->stats.frames++; return 1;
}
/* CPU owner, about every millisecond: VBL, a CP waiting on a completion fence (or with packets left by its budget),
 * and at the display period the scanout publication. */
static void service(PtRadeon *d,uint64_t guest_ns);
void pt_radeon_service(PtRadeon *d,uint64_t guest_ns) { if(d){ enter(d); service(d,guest_ns); leave(d); } }
static void service(PtRadeon *d,uint64_t guest_ns) {
    d->guest_ns=guest_ns;
    if(guest_ns-d->vblank_ns>=16666667u) {   /* vertical blank at 60 Hz guest time: latch and raise when unmasked */
        d->vblank_ns=guest_ns-guest_ns%16666667u; d->int_status|=INT_VBLANK; d->crtc_status|=CRTC_VBLANK_SAVE; irq_update(d);
        static unsigned shown; if(d->reg[GEN_INT_CNTL/4] && shown++<4) fprintf(stderr,"optics-frontend: vblank with GEN_INT_CNTL %08x\n",d->reg[GEN_INT_CNTL/4]);
    }
    if(cp_pending(d)){ if(!d->cp_async) cp_run(d,0); else if(d->cp_sleeping) pt_event_signal(&d->cp_event); }   /* a missed wake costs one tick */
    if(rate_on()) rate_tick(d);
    if(d->frame_pending) d->frame_pending=publish_scanout(d,0)<0;
    uint32_t w,h,bpp,line,offset;
    if(guest_ns-d->published_ns>=16666667u && scanout_mode(d,&w,&h,&bpp,&line,&offset)) {
        static uint32_t last[4]; static unsigned shown;   /* development trace: scanout geometry changes */
        if(optics_trace() && (last[0]!=offset || last[1]!=line || last[2]!=w || last[3]!=h) && shown++<32) {
            fprintf(stderr,"optics-frontend: scanout %#x %ux%u line %u bpp %u\n",offset,w,h,line,bpp); last[0]=offset; last[1]=line; last[2]=w; last[3]=h;
        }
        d->published_ns=guest_ns; if(!d->frame_pending) d->frame_pending=publish_scanout(d,0)<0;
        /* development: OPTICS_SURFACE_DUMP, by host seconds since the first service call (the input script's clock is the
         * host's; guest time runs at its own rate), from 55 s every 3 s, 12 dumps */
        static const char *dir; static int checked; static uint64_t t0,next; static unsigned n;
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); uint64_t host=(uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;
        if(!checked){checked=1;dir=getenv("OPTICS_SURFACE_DUMP");t0=host;next=t0+55000000000ull;}
        if(dir && exec_ops.dump && host>=next && n<12){ next=host+3000000000ull; exec_ops.dump(d->exec,d,dir,n++); }
    }
}
/* Capability/status record (diagnostics.h, ABI 1) as English JSON: what this run actually wired and used. */
static void status(PtRadeon *d,OpticsStatus *out);
void optics_frontend_status(PtRadeon *d,OpticsStatus *out) {
    if(!out)return;
    if(!d){*out=(OpticsStatus){.state=OPTICS_DISABLED};return;}
    enter(d); status(d,out); leave(d);
}
static void status(PtRadeon *d,OpticsStatus *out) {
    OpticsStatus s={.state=d->stats.failed?OPTICS_FAILED:OPTICS_READY,.features=OPTICS_HARDWARE_FRONTEND};
    if(exec_ops.status) exec_ops.status(d->exec,d,&s);
    optics_status_io(&s,d->io);
    if(d->stats.native_frames) s.features|=OPTICS_HOST_FRAMES;
    if(d->stats.direct_switches) s.features|=OPTICS_CPU_GUARDS;   /* guarded direct VRAM was mapped at least once this run */
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    const unsigned index[]={OPTICS_HOST_NS,OPTICS_RUN_EPOCH,OPTICS_GPU_DONE,OPTICS_CPU_GUARD_FAULTS,OPTICS_UNSUPPORTED};
    const uint64_t value[]={(uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec,d->epoch,pt_r2_timeline_gpu_done(d->tl),d->stats.guard_faults,
        d->stats.draws_refused+d->stats.packets_refused+d->stats.blits_refused};
    for(unsigned i=0;i<sizeof index/sizeof *index;i++){ s.metric[index[i]]=value[i]; s.known|=UINT64_C(1)<<index[i]; }
    *out=s;
}
static void status_json(PtRadeon *d,FILE *f) {
    OpticsStatus s; optics_frontend_status(d,&s);
    char text[2048]; if(optics_status_json(&s,text,sizeof text)) fprintf(f,"optics-status: %s",text);
}
void pt_radeon_stats(PtRadeon *d,FILE *f) {
    if(!d || !f) return;
    enter(d); OpticsFrontendStats s=d->stats; int direct=d->direct; leave(d);
    fprintf(f,"optics-frontend: draws %llu packets %llu type0 %llu IBs %llu blits %llu (%llu bytes) refused: draws %llu packets %llu blits %llu; "
        "faults %llu (last %08x) writebacks %llu frames %llu (native %llu copied %llu) fence waits %llu completions deferred %llu RBBM reads %llu export failures %llu; "
        "VRAM %s (switches %llu), CPU view faults %llu (late syncs %llu, written granules returned %llu); approximated draws: mip %llu border %llu aniso %llu tiled %llu signed %llu%s%s\n",(unsigned long long)s.draws,(unsigned long long)s.packets,(unsigned long long)s.type0_words,
        (unsigned long long)s.ib_launches,(unsigned long long)s.blits,(unsigned long long)s.blit_bytes,(unsigned long long)s.draws_refused,
        (unsigned long long)s.packets_refused,(unsigned long long)s.blits_refused,(unsigned long long)s.translate_faults,s.last_fault_address,
        (unsigned long long)s.writebacks,(unsigned long long)s.frames,(unsigned long long)s.native_frames,(unsigned long long)s.copied_frames,
        (unsigned long long)s.fence_waits,(unsigned long long)s.completions_deferred,(unsigned long long)s.rbbm_reads,(unsigned long long)s.materialize_failures,direct?"CPU view":"trapped",(unsigned long long)s.direct_switches,(unsigned long long)s.guard_faults,
        (unsigned long long)s.guard_emulated,(unsigned long long)s.guard_groups,(unsigned long long)s.approx_mip,(unsigned long long)s.approx_border,
        (unsigned long long)s.approx_aniso,(unsigned long long)s.approx_tiled,(unsigned long long)s.approx_signed,s.failed?"; FAILED":"",strict()?"; strict":"; DEGRADED (refusals continue)");
}
static int reset(PtRadeon *d);
int pt_radeon_reset(PtRadeon *d) { if(!d) return EINVAL; enter(d); int r=reset(d); leave(d); return r; }
static int reset(PtRadeon *d) {
    view_sync(d,0,d->vram_bytes);   /* the CPU's bytes go back before the registers (and lanes) are cleared */
    pthread_mutex_lock(&d->frame_lock); int held=0; for(unsigned i=0;i<FRAME_SLOTS;i++) held|=d->slot[i].refs!=0;
    if(!held) for(unsigned i=0;i<FRAME_SLOTS;i++) d->slot[i].seq=0;   /* no frame of the previous run is shown again */
    pthread_mutex_unlock(&d->frame_lock); if(held) return EBUSY;
    for(unsigned i=0;i<FRAME_SLOTS;i++) slot_drop(&d->slot[i]);
    /* GPU-owned results go back to VRAM and every executor resource of the old run is dropped (its epoch ends). */
    int executor_failed=exec_ops.reset && exec_ops.reset(d->exec,d);
    if(executor_failed) fprintf(stderr,"optics-frontend: executor reset could not export every GPU result\n");
    /* Every page open and every token of the old run stale (CPU stopped); a protection failure leaves the device failed. */
    if(pt_r2_io_reset(d->io)!=PT_R2_IO_OK) failed(d,"VRAM guard reset");
    if(d->ring_ready) pt_r2_ring_destroy(&d->ring);
    (void)pt_r2_timeline_reset(d->tl); d->issued=0; d->defer_head=d->defer_count=0; d->held=0;
    d->ring_ready=0; d->cp_wait=0; d->cp_kicked=0; d->ring_stalled=0; d->frame_pending=0; if(d->stream) optics_stream_reset(d->stream); optics_r300_state_reset(d->r300);
    uint32_t fb=(uint32_t)(((d->vram_bytes>>16)-1)<<16); memset(d->reg,0,sizeof d->reg); d->reg[MC_FB_LOCATION/4]=fb;
    d->reg[DP_WRITE_MASK/4]=0xffffffffu;d->direction_written=0; d->epoch++;
    PtR2IoStats is; pt_r2_io_stats(d->io,&is); d->stats.failed=executor_failed || is.fatal;
    if(d->config.width && d->config.height) boot_mode(d,d->config.width,d->config.height,32,0); else direct_update(d);
    return d->stats.failed?EIO:0;
}
/* UI (any thread): the newest published immutable frame, leased until pt_radeon_frame_release. On the CPU owner (screen
 * shots) a pixel copy of the current scanout is published first. */
int pt_radeon_frame_acquire(PtRadeon *d,PtRadeonFrame *f) {
    if(!d || !f) return 0;
    if(pthread_equal(pthread_self(),d->owner)){ enter(d); publish_scanout(d,1); leave(d); }
    pthread_mutex_lock(&d->frame_lock); Slot *best=NULL;
    for(unsigned i=0;i<FRAME_SLOTS;i++) if(d->slot[i].seq && (!best || d->slot[i].seq>best->seq)) best=&d->slot[i];
    if(best){ best->refs++; *f=best->f; }
    pthread_mutex_unlock(&d->frame_lock); return best!=NULL;
}
void pt_radeon_frame_release(PtRadeon *d,const PtRadeonFrame *f) {
    if(!d || !f || f->slot>=FRAME_SLOTS) return;
    pthread_mutex_lock(&d->frame_lock); if(d->slot[f->slot].refs) d->slot[f->slot].refs--; pthread_mutex_unlock(&d->frame_lock);
}
/* CPU stopped, UI detached and its last presentation drained (radeon.h): the executor drains its GPU work first. */
void pt_radeon_close(PtRadeon *d) {
    if(!d) return;
    if(d->cp_async) {   /* the CP thread stops between two packets; from here on the caller is the only owner */
        enter(d); d->cp_quit=1; leave(d); pt_event_signal(&d->cp_event); pthread_join(d->cp,NULL);
        d->cp_async=0; pt_event_destroy(&d->cp_event); pthread_mutex_destroy(&d->lock);
    }
    pt_radeon_stats(d,stderr);   /* one line at exit: executed/refused work, fences, frames, failures */
    status_json(d,stderr);
    if(exec_ops.destroy) exec_ops.destroy(d->exec,d);
    d->exec=NULL;
    if(d->direct){ d->config.vram_guard(d->config.map_opaque,NULL,NULL); d->config.direct_map(d->config.map_opaque,NULL,0,0); d->direct=0; }   /* before freeing VRAM */
    if(d->view){ munmap(d->view,d->vram_bytes); free(d->view_state); }
    pt_r2_io_destroy(d->io); pt_r2_guard_destroy(d->guards); pt_r2_timeline_destroy(d->tl);
    for(unsigned i=0;i<FRAME_SLOTS;i++) slot_free(&d->slot[i]);
    if(d->ring_ready) pt_r2_ring_destroy(&d->ring);
    pthread_mutex_destroy(&d->frame_lock);
    optics_stream_destroy(d->stream); optics_r300_state_destroy(d->r300); munmap(d->vram,d->vram_bytes); free(d->imm_data); free(d);
}
void optics_frontend_stats(const PtRadeon *d,OpticsFrontendStats *s) { if(d && s) *s=d->stats; }
void optics_frontend_cpu_vram(const PtRadeon *d,uint64_t *accesses,uint64_t *bytes) { *accesses=d?d->stats.cpu_vram_accesses:0; *bytes=d?d->stats.cpu_vram_bytes:0; }
uint32_t optics_frontend_register(const PtRadeon *d,uint32_t a) { return d && a<REG_BYTES ? d->reg[a/4] : 0; }
size_t optics_frontend_scanout(PtRadeon *d,uint8_t *out,size_t cap,uint32_t *w,uint32_t *h,uint32_t *bpp,uint32_t *line) {
    uint32_t offset; if(!d || !scanout_mode(d,w,h,bpp,line,&offset)) return 0;
    if(own_check(d,offset,(uint64_t)*line**h,0)) return 0;
    size_t row=(size_t)*w*(*bpp/8),n=row*(*h); if(n>cap) return 0;
    for(uint32_t y=0;y<*h;y++) memcpy(out+y*row,d->vram+offset+(size_t)y*(*line),row);
    return n;
}
unsigned optics_frontend_cpu_view_xor(const PtRadeon *d) { return d?aperture_xor(d,(int)d->fb_ap):0; }
uint64_t optics_frontend_job(PtRadeon *d) { uint64_t s=d && d->tl ? pt_r2_timeline_issue(d->tl) : 0; if(s) d->issued=s; return s; }
int optics_frontend_timeline_submit(PtRadeon *d,PtR2TimelineToken *t) { return d && d->tl && pt_r2_timeline_submit(d->tl,d->issued,t)==PT_R2_READY; }
void optics_frontend_timeline_complete(PtRadeon *d,PtR2TimelineToken t,int success) { if(d && d->tl) (void)pt_r2_timeline_complete(d->tl,t,success); }
int optics_frontend_idle(PtRadeon *d) { if(!d) return 0; enter(d); int idle=!cp_busy(d); leave(d); return idle; }
/* Another device's DMA reaches guest RAM [at, at+n): GPU ownership first. Any CPU-side thread; 0, EAGAIN (the GPU
 * still owns it: retry later) or EIO. */
int optics_frontend_ram_before(PtRadeon *d,uint64_t at,size_t n,int write) {
    enter(d); PtR2IoResult r=pt_r2_io_before_physical(d->io,1,at,n,write); leave(d);
    return r==PT_R2_IO_OK?0:r==PT_R2_IO_BUSY?EAGAIN:EIO;
}
int optics_frontend_vram_offset(const PtRadeon *d,uint64_t gpu,uint64_t *off) {
    uint64_t base,len; if(!optics_r300_memory_window(d->reg[MC_FB_LOCATION/4],&base,&len) || gpu<base || gpu-base>=len || gpu-base>=d->vram_bytes) return 0;
    *off=gpu-base; return 1;
}
int optics_frontend_materialize_all(PtRadeon *d) { return own_check(d,0,d->vram_bytes,0); }
