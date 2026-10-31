/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Directed test of fast VRAM - the CPU view of aperture 0 - on real Hypervisor.framework stage-2 protection (no
 * protection double): the frontend maps its view into a VM at IPA 0x90000000 through the board callbacks (direct_map 2,
 * vram_protect, vram_guard) and single ARM instructions run in the VM; each stage-2 fault goes to the product route
 * (cpu_guard.c) exactly as epicycle/hvf.c vm_enter calls it.
 * Aperture 0 in 32-bit swap, as Tiger sets it. Checks: a. a read sees the VRAM word in the CPU's byte order and the
 * granule stays open for reads; b. a store after a read grant faults once (upgrade), reaches VRAM in device order when
 * the device reads it, and the granule is closed again (the next read faults); c. GPU-owned bytes are exported before
 * the CPU reads them; d. a CPU write invalidates a GPU snapshot of its page when the page comes back; e. a lane change
 * (SURFACE_CNTL) takes the CPU's bytes back with the old lanes, the next grant uses the new ones. Negative control
 * ("bypass"): with the stage-2 protection skipped, a device write after a sync is not seen by the CPU (stale), so the
 * test can see missing protection. Needs the hypervisor entitlement (codesign with epicycle/hv.entitlements). */
#include "frontend.h"
#include "guard.h"
#include <Hypervisor/Hypervisor.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <libkern/OSCacheControl.h>

#define CK(x) do { hv_return_t r_=(x); if(r_!=HV_SUCCESS){fprintf(stderr,"%s = %#x\n",#x,r_);exit(1);} } while(0)
enum { VRAM_IPA=0x90000000u, CODE=0x10000, VEC=0x20000, PT=0x30000, SURF=0x8000, TEX=0x10000, LANE=0x14000 };
static hv_vcpu_t vcpu; static hv_vcpu_exit_t *vx;
hv_vcpu_t pt_hv_vcpu(void) { return vcpu; }   /* epicycle/hvf.c's accessor, for cpu_guard.c */

/* Board side, as src/firmware.c does it. */
static uint8_t *mapped; static size_t mapped_bytes; static int bypass;
static int (*route)(void *,uint64_t,uint64_t,int,uint64_t,uint32_t); static void *route_device;
static void board_map(void *o,uint8_t *p,size_t n,int enable) {
    (void)o; if(enable==2){ CK(hv_vm_map(p,VRAM_IPA,n,HV_MEMORY_READ|HV_MEMORY_WRITE|HV_MEMORY_EXEC)); mapped=p; mapped_bytes=n; }
    else if(!enable && mapped){ CK(hv_vm_unmap(VRAM_IPA,mapped_bytes)); mapped=NULL; }
}
static int board_protect(void *o,uint64_t off,uint64_t n,unsigned prot) {
    (void)o; if(!mapped || bypass) return 0;
    return hv_vm_protect(VRAM_IPA+off,n,(prot&1?HV_MEMORY_READ|HV_MEMORY_EXEC:0)|(prot&2?HV_MEMORY_WRITE:0))==HV_SUCCESS?0:-1;
}
static void board_guard(void *o,int (*f)(void *,uint64_t,uint64_t,int,uint64_t,uint32_t),void *device) { (void)o; route=f; route_device=device; }

/* Test executor: one GPU-owned destination [SURF,SURF+4K) whose export writes 0xA5 bytes, one watched texture source. */
static PtRadeon *dev; static PtR2IoToken surf_own,surf_watch,tex_watch; static unsigned exports,drops;
static int overlaps(uint64_t s,uint64_t e,uint64_t a,uint64_t n) { return s<a+n && a<e; }
static int materialize(void *o,PtRadeon *d,uint64_t s,uint64_t e,int write) {
    (void)o; PtR2Io *io=optics_frontend_io(d);
    if(overlaps(s,e,SURF,4096)) {
        if(surf_own.id){ uint8_t b[4096]; memset(b,0xa5,sizeof b); assert(pt_r2_io_write_complete(io,surf_own,b,sizeof b)==PT_R2_IO_OK); surf_own=(PtR2IoToken){0}; exports++;
            if(!write) assert(pt_r2_io_watch(io,SURF,4096,&surf_watch)==PT_R2_IO_OK); }
        if(write && surf_watch.id){ pt_r2_io_release(io,surf_watch); surf_watch=(PtR2IoToken){0}; drops++; }
    }
    if(write && overlaps(s,e,TEX,4096) && tex_watch.id){ pt_r2_io_release(io,tex_watch); tex_watch=(PtR2IoToken){0}; drops++; }
    return 0;
}
static void own_surface(void) { PtR2Io *io=optics_frontend_io(dev); if(surf_watch.id){pt_r2_io_release(io,surf_watch);surf_watch=(PtR2IoToken){0};}
    assert(pt_r2_io_write_begin(io,SURF,4096,&surf_own)==PT_R2_IO_OK); }

static uint32_t *code; static unsigned slot;
/* Run one instruction (then brk #0) with x0 = VRAM_IPA+off; stage-2 faults go through the product route. */
static unsigned run(uint32_t in,uint64_t off) {
    uint64_t pc=CODE+8*slot; code[2*slot]=in; code[2*slot+1]=0xd4200000; sys_icache_invalidate(&code[2*slot],8); slot++;
    CK(hv_vcpu_set_reg(vcpu,HV_REG_X0,VRAM_IPA+off)); CK(hv_vcpu_set_reg(vcpu,HV_REG_PC,pc)); CK(hv_vcpu_set_reg(vcpu,HV_REG_CPSR,0x3c5));
    unsigned faults=0;
    for(;;) {
        CK(hv_vcpu_run(vcpu));
        if(vx->reason!=HV_EXIT_REASON_EXCEPTION) continue;
        uint64_t esr=vx->exception.syndrome; unsigned ec=esr>>26&0x3f;
        if(ec==0x3c && (esr&0xffff)) {   /* an EL1 exception reached the vector table: report it */
            uint64_t e1,far,elr; CK(hv_vcpu_get_sys_reg(vcpu,HV_SYS_REG_ESR_EL1,&e1)); CK(hv_vcpu_get_sys_reg(vcpu,HV_SYS_REG_FAR_EL1,&far)); CK(hv_vcpu_get_sys_reg(vcpu,HV_SYS_REG_ELR_EL1,&elr));
            fprintf(stderr,"optics-guard-hvf: EL1 vector %#llx: ESR_EL1 %#llx FAR %#llx ELR %#llx (case slot %u)\n",(unsigned long long)(esr&0xffff),(unsigned long long)e1,(unsigned long long)far,(unsigned long long)elr,slot-1);
            exit(2);
        }
        if(ec==0x3c) return faults;
        assert(ec==0x24 && route && vx->exception.physical_address>=VRAM_IPA);
        uint64_t at; CK(hv_vcpu_get_reg(vcpu,HV_REG_PC,&at)); faults++; assert(faults<8);
        int g=route(route_device,vx->exception.physical_address-VRAM_IPA,vx->exception.virtual_address,(int)(esr>>6&1),at,*(uint32_t *)((uint8_t *)code+(at-CODE)));
        assert(g==1);
    }
}
static uint64_t x(unsigned r) { uint64_t v; CK(hv_vcpu_get_reg(vcpu,HV_REG_X0+r,&v)); return v; }
static void gpu_write(uint64_t off,const char *b) { assert(optics_frontend_gpu_write(dev,off,b,4)); }
static int gpu_is(uint64_t off,const char *b) { uint8_t r[4]; assert(optics_frontend_gpu_read(dev,off,r,4)); return !memcmp(r,b,4); }
static void mmio(unsigned a,uint32_t v) { uint8_t b[4]; for(unsigned i=0;i<4;i++) b[i]=(uint8_t)(v>>(8*i)); assert(!pt_radeon_mmio(dev,a,b,4,1)); }

int main(int argc,char **argv) {
    (void)argv; bypass=argc>1;   /* negative control: any argument bypasses the stage-2 protection */
    setenv("OPTICS_DIRECT_VRAM","1",1);   /* the default; a caller's =0 (diagnostic switch) must not turn this test into a no-op */
    static const OpticsExecutor ex={.materialize=materialize}; optics_frontend_set_executor(&ex);
    CK(hv_vm_create(NULL));
    uint8_t *low=mmap(NULL,0x40000,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0); assert(low!=MAP_FAILED);
    CK(hv_vm_map(low,0,0x40000,HV_MEMORY_READ|HV_MEMORY_WRITE|HV_MEMORY_EXEC));
    code=(uint32_t *)(low+CODE);
    for(unsigned n=0;n<16;n++) ((uint32_t *)(low+VEC))[n*32]=0xd4200000|(0x100+n)<<5;   /* any EL1 exception: brk #0x10n */
    uint64_t *pt=(uint64_t *)(low+PT); for(uint64_t i=0;i<4;i++) pt[i]=i<<30|1u<<10|3u<<8|1;   /* 1 GiB Normal WB blocks */
    uint8_t *ram=calloc(1,1<<20); assert(ram);
    PtRadeonConfig c={.ram={ram,1<<20,NULL},.direct_map=board_map,.vram_protect=board_protect,.vram_guard=board_guard};
    assert(!pt_radeon_create(&dev,&c));
    mmio(0x0b00,2u<<20);   /* SURFACE_CNTL: aperture 0 32-bit swap; the view is mapped, all closed */
    assert(mapped && route);
    CK(hv_vcpu_create(&vcpu,&vx,NULL)); CK(hv_vcpu_set_trap_debug_exceptions(vcpu,true));
    CK(hv_vcpu_set_sys_reg(vcpu,HV_SYS_REG_MAIR_EL1,0xff));
    CK(hv_vcpu_set_sys_reg(vcpu,HV_SYS_REG_TCR_EL1,28|1u<<8|1u<<10|3u<<12|1u<<23|2ull<<32));
    CK(hv_vcpu_set_sys_reg(vcpu,HV_SYS_REG_TTBR0_EL1,PT)); CK(hv_vcpu_set_sys_reg(vcpu,HV_SYS_REG_VBAR_EL1,VEC));
    CK(hv_vcpu_set_sys_reg(vcpu,HV_SYS_REG_CPACR_EL1,3u<<20));
    uint64_t sctlr; CK(hv_vcpu_get_sys_reg(vcpu,HV_SYS_REG_SCTLR_EL1,&sctlr)); CK(hv_vcpu_set_sys_reg(vcpu,HV_SYS_REG_SCTLR_EL1,sctlr|1|1u<<2|1u<<12));
    unsigned f;
    /* a. VRAM 04 03 02 01 (device order) reads as 01 02 03 04 through the swapped aperture */
    gpu_write(0,"\x04\x03\x02\x01"); f=run(0xb9400001,0);                                   /* ldr w1,[x0] */
    if(bypass) {   /* without protection nothing faults: the CPU reads the view, never converted from VRAM */
        printf("optics-guard-hvf negative control: read %#llx after %u faults (%s)\n",(unsigned long long)x(1),f,x(1)==0x04030201u?"UNEXPECTED":"stale, detected");
        return x(1)==0x04030201u ? 1 : 0;
    }
    assert(f==1 && x(1)==0x04030201u);
    f=run(0xb9400001,0); assert(f==0 && x(1)==0x04030201u);                                    /* open for reads */
    /* b. the store upgrades the grant; the device reads it in device order; the granule is closed again */
    CK(hv_vcpu_set_reg(vcpu,HV_REG_X2,0x0a0b0c0d)); f=run(0xb9000402,0); assert(f==1);          /* str w2,[x0,#4] */
    assert(gpu_is(4,"\x0a\x0b\x0c\x0d"));
    f=run(0xb9400401,0); assert(f==1 && x(1)==0x0a0b0c0du);                                    /* ldr w1,[x0,#4] */
    /* c. GPU-owned bytes are exported before the read */
    own_surface(); f=run(0xb9400001,SURF); assert(f==1 && x(1)==0xa5a5a5a5u && exports==1);
    /* d. a snapshot of a page the CPU then writes is stale once the page comes back */
    PtR2Io *io=optics_frontend_io(dev); PtR2IoToken snap; assert(pt_r2_io_snapshot(io,TEX,16,&snap)==PT_R2_IO_OK && pt_r2_io_snapshot_valid(io,snap));
    CK(hv_vcpu_set_reg(vcpu,HV_REG_X2,0x5a)); f=run(0x39000002,TEX); assert(f==1);              /* strb w2,[x0] */
    assert(pt_r2_io_snapshot_valid(io,snap)); assert(gpu_is(TEX&~3u,"\0\0\0\x5a"));           /* swapped lane of byte 0 */
    assert(!pt_r2_io_snapshot_valid(io,snap)); pt_r2_io_release(io,snap);
    /* e. a lane change: the CPU's word goes back with the old lanes, the next grant uses the new ones */
    CK(hv_vcpu_set_reg(vcpu,HV_REG_X2,0x11223344)); f=run(0xb9000002,LANE); assert(f==1);       /* str w2,[x0] */
    mmio(0x0b00,0); assert(gpu_is(LANE,"\x11\x22\x33\x44"));
    f=run(0xb9400001,LANE); assert(f==1 && x(1)==0x44332211u);
    OpticsFrontendStats s; optics_frontend_stats(dev,&s);
    printf("optics-guard-hvf: ok: CPU view faults %llu, written granules returned %llu, late syncs %llu, exports %u\n",(unsigned long long)s.guard_faults,
        (unsigned long long)s.guard_groups,(unsigned long long)s.guard_emulated,exports);
    if(surf_watch.id) pt_r2_io_release(io,surf_watch); if(tex_watch.id) pt_r2_io_release(io,tex_watch);
    if(surf_own.id) pt_r2_io_write_abort(io,surf_own);
    pt_radeon_close(dev); CK(hv_vcpu_destroy(vcpu)); CK(hv_vm_destroy()); return 0;
}
