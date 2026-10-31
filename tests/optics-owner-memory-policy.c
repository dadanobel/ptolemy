/* SPDX-License-Identifier: GPL-2.0-or-later */
/* New ownership paths cannot accidentally activate incomplete direct coherence. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
static unsigned mappings;
static void direct(void *o,uint8_t *p,size_t n,int enable){(void)o;(void)p;(void)n;if(enable)mappings++;}
static int protect(void *o,uint64_t off,uint64_t n,unsigned prot){(void)o;(void)off;(void)n;(void)prot;return 0;}
static void guard(void *o,int (*f)(void *,uint64_t,uint64_t,int,uint64_t,uint32_t),void *d){(void)o;(void)f;(void)d;}
static int fault(void *d,uint64_t off,uint64_t far,int w,uint64_t pc,uint32_t ins){(void)d;(void)off;(void)far;(void)w;(void)pc;(void)ins;return 0;}
int main(void){
    static uint8_t ram[4096];PtRadeonConfig c={.ram={ram,sizeof ram,NULL},.direct_map=direct,.vram_protect=protect,.vram_guard=guard};
    optics_frontend_set_cpu_fault(fault);PtRadeon *d;assert(!pt_radeon_create(&d,&c));uint8_t zero[4]={0};
    assert(!pt_radeon_mmio(d,0xb00,zero,4,1));assert(mappings==0);
    /* RAM watches fail closed; no shared RAM GPU path is silently enabled. */
    PtR2IoToken token={0};assert(pt_r2_io_watch(optics_frontend_io(d),0x10000000,4,&token)!=PT_R2_IO_OK);
    pt_radeon_close(d);puts("PASS: new owner/depth paths force trapped VRAM; RAM watches do not claim unimplemented protection");return 0;
}
