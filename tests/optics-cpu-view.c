/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Fast VRAM's CPU view without a VM (optics-guard-hvf runs it on real stage-2). A board double records the
 * view mapping and the protection of each 16 KiB granule; a CPU access the protection forbids goes to the fault route
 * (optics_frontend_cpu_grant) as HVF delivers it, then touches the view. Checks: lanes (swap32, a swap16 SURFACE region
 * inside a granule, none), read-only grants and the write upgrade, device reads/writes taking the CPU's bytes back and
 * closing the granule, snapshot invalidation, a lane change, trapped accesses on a held granule (interpreter) and through
 * aperture 1, the scanout of CPU pixels, a failed protection. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { G=16384 };
static uint8_t *view; static uint8_t prot[1u<<14]; static int fail,mapped; static PtRadeon *dev;
static int (*route)(void *,uint64_t,uint64_t,int,uint64_t,uint32_t); static void *route_device; static unsigned faults;
static void map(void *o,uint8_t *p,size_t n,int enable){ (void)o; (void)n; view=enable?p:NULL; mapped=enable; }
static int protect(void *o,uint64_t off,uint64_t n,unsigned p){ (void)o; if(fail) return -1; for(uint64_t g=off/G;g<(off+n)/G;g++) prot[g]=(uint8_t)p; return 0; }
static void guard(void *o,int (*f)(void *,uint64_t,uint64_t,int,uint64_t,uint32_t),void *d){ (void)o; route=f; route_device=d; }
static int fault(void *d,uint64_t off,uint64_t far,int w,uint64_t pc,uint32_t in){ (void)far; (void)pc; (void)in; faults++; return optics_frontend_cpu_grant(d,off,w); }
static int cpu(uint32_t off,void *b,unsigned n,int w) {   /* one CPU access through the mapped view */
    for(int k=0;!(prot[off/G]&(w?2:1));k++){ if(k==2 || route(route_device,off,0,w,0,0)!=1) return -1; }
    if(w) memcpy(view+off,b,n); else memcpy(b,view+off,n); return 0;
}
static int cpu_is(uint32_t off,const char *e){ uint8_t b[4]; return !cpu(off,b,4,0) && !memcmp(b,e,4); }
static void cpu_put(uint32_t off,const char *b){ assert(!cpu(off,(void *)b,4,1)); }
static void gpu_put(uint32_t off,const char *b){ assert(optics_frontend_gpu_write(dev,off,b,4)); }
static int gpu_is(uint32_t off,const char *e){ uint8_t b[4]; assert(optics_frontend_gpu_read(dev,off,b,4)); return !memcmp(b,e,4); }
static void mmio(unsigned a,uint32_t v){ uint8_t b[4]; for(unsigned i=0;i<4;i++) b[i]=(uint8_t)(v>>(8*i)); assert(!pt_radeon_mmio(dev,a,b,4,1)); }
int main(void) {
    static uint8_t ram[4096]; PtRadeonConfig c={.ram={ram,sizeof ram,NULL},.direct_map=map,.vram_protect=protect,.vram_guard=guard};
    optics_frontend_set_cpu_fault(fault); assert(!pt_radeon_create(&dev,&c));
    mmio(0x0b00,2u<<20); assert(view && route && !prot[0]);                       /* aperture 0 swap32: mapped, closed */
    gpu_put(0,"\x04\x03\x02\x01"); assert(cpu_is(0,"\x01\x02\x03\x04") && prot[0]==1 && faults==1);   /* read grant */
    assert(cpu_is(0,"\x01\x02\x03\x04") && faults==1);
    cpu_put(4,"\x0d\x0c\x0b\x0a"); assert(prot[0]==3 && faults==2);              /* write upgrade */
    assert(gpu_is(4,"\x0a\x0b\x0c\x0d") && prot[0]==0);                          /* device read: back in device order, closed */
    assert(cpu_is(4,"\x0d\x0c\x0b\x0a") && faults==3);
    /* a GPU snapshot of a page the CPU writes is stale once the page comes back */
    PtR2Io *io=optics_frontend_io(dev); PtR2IoToken snap; assert(pt_r2_io_snapshot(io,0x10000,16,&snap)==PT_R2_IO_OK);
    cpu_put(0x10000,"\x5a\0\0\0"); assert(pt_r2_io_snapshot_valid(io,snap));
    assert(gpu_is(0x10000,"\0\0\0\x5a") && !pt_r2_io_snapshot_valid(io,snap)); pt_r2_io_release(io,snap);
    /* a swap16 SURFACE region inside a granule: per-word lanes */
    mmio(0x0b04,0x20000); mmio(0x0b08,0x20fff); mmio(0x0b0c,1u<<20);
    gpu_put(0x20000,"\x02\x01\x04\x03"); gpu_put(0x21000,"\x04\x03\x02\x01");
    assert(cpu_is(0x20000,"\x01\x02\x03\x04") && cpu_is(0x21000,"\x01\x02\x03\x04"));
    cpu_put(0x20004,"\x11\x22\x33\x44"); mmio(0x0b08,0x1ffff);                  /* region ends: the word goes back as swap16 */
    assert(gpu_is(0x20004,"\x22\x11\x44\x33")); assert(cpu_is(0x20004,"\x33\x44\x11\x22"));   /* now swap32 */
    /* a lane change takes the CPU's bytes back with the old lanes */
    cpu_put(0x14000,"\x44\x33\x22\x11"); mmio(0x0b00,0);
    assert(gpu_is(0x14000,"\x11\x22\x33\x44") && cpu_is(0x14000,"\x11\x22\x33\x44"));
    /* trapped accesses: a held granule's view (interpreter), aperture 1 takes it back */
    cpu_put(0x30000,"\xaa\xbb\xcc\xdd"); uint8_t b[4];
    assert(!pt_radeon_vram(dev,0x30000,b,4,0) && !memcmp(b,"\xaa\xbb\xcc\xdd",4));
    assert(!pt_radeon_vram(dev,0x30004,(uint8_t *)"\xee\xee\xee\xee",4,1) && prot[0x30000/G]==3 && cpu_is(0x30004,"\xee\xee\xee\xee"));
    OpticsFrontendStats s; optics_frontend_stats(dev,&s); uint64_t trapped=s.cpu_vram_accesses;
    assert(!pt_radeon_vram(dev,(uint32_t)(128u<<20)+0x30000,b,4,0) && !memcmp(b,"\xaa\xbb\xcc\xdd",4) && prot[0x30000/G]==0);
    optics_frontend_stats(dev,&s); assert(s.cpu_vram_accesses==trapped+1);
    /* the scanout shows the CPU's pixels: 8 bits through aperture 1, linear also after Tiger's driver swaps aperture 0 */
    uint32_t fb; assert(!pt_radeon_display_mode(dev,640,480,8,&fb) && fb==128u<<20 && mapped==3 && !prot[0]);
    mmio(0x0b00,0x00200001); cpu_put(0x40000,"\x01\x02\x03\x04");
    PtRadeonFrame f; pt_radeon_service(dev,20000000); assert(pt_radeon_frame_acquire(dev,&f) && f.bpp==8 && !memcmp(f.pixels+0x40000,"\x01\x02\x03\x04",4));
    pt_radeon_frame_release(dev,&f); assert(prot[0x40000/G]==0);
    assert(!pt_radeon_display_mode(dev,640,480,32,NULL) && mapped==2 && cpu_is(0x40000,"\x04\x03\x02\x01"));   /* aperture 0 again */
    /* a failed protection is never a silent success */
    fail=1; assert(cpu(0x50000,b,4,0)==-1); fail=0;
    /* a mode switch whose sync fails reports it and changes nothing */
    cpu_put(0x60000,"\x01\x02\x03\x04"); uint8_t g0[4],g1[4]; assert(!pt_radeon_mmio(dev,0x50,g0,4,0));
    fail=1; assert(pt_radeon_display_mode(dev,800,600,16,NULL)!=0); fail=0;
    assert(!pt_radeon_mmio(dev,0x50,g1,4,0) && !memcmp(g0,g1,4));
    optics_frontend_stats(dev,&s); assert(s.guard_faults && s.guard_groups && !s.guard_emulated);
    pt_radeon_close(dev); puts("PASS: CPU view lanes (swap32, swap16 region, none), grants, device syncs, snapshots, lane changes, trapped accesses, scanout");
    return 0;
}
