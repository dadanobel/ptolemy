/* SPDX-License-Identifier: GPL-2.0-or-later
 * Cached multi-instruction FP chains, exact state at each slow exit, RN changes,
 * integer conversion boundaries, and load/store coherence. No HVF required. */
#include "../epicycle/jit.c"
#include <assert.h>
#include <math.h>
#include <pthread.h>
int sys_call(CPU *c) { (void)c; abort(); }
static uint32_t xfp(unsigned xo,unsigned d,unsigned a,unsigned b) { return 63u<<26|d<<21|a<<16|b<<11|xo<<1; }
int main(void) {
    uint32_t *code = mmap(NULL, 65536, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_JIT, -1, 0);
    uint8_t *mem = calloc(1,65536), *ref = calloc(1,65536);
    assert(code != MAP_FAILED && mem && ref);
    static const double values[] = {0.,-0.,1.,-1.,1.25,0x1.000001p0,0x1p-126,0x1.fffffffffffffp-127,
        0x1p-149,0x1p-150,0x1p-1022,0x0.0000000000001p-1022,0x1.fffffep127,0x1.ffffffp127,
        2147483647.,2147483647.5,-2147483648.,-2147483648.5,2147483648.,INFINITY,-INFINITY,NAN};
    unsigned count = 0;
    for (unsigned kind=0;kind<4;kind++) for (unsigned i=0;i<sizeof values/sizeof *values;i++)
    for (unsigned rn=0;rn<4;rn++) for (unsigned cached=0;cached<2;cached++) for(unsigned sys=0;sys<2;sys++) {
        uint32_t ins[32];unsigned n=0;
        if (kind==0) {
            ins[n++]=50u<<26|1u<<21|3u<<16; /* lfd f1,0(r3) */
            ins[n++]=50u<<26|2u<<21|3u<<16|8;
            ins[n++]=xfp(21,3,1,2);ins[n++]=xfp(25,4,3,0)|1u<<6;
            ins[n++]=xfp(20,1,4,3);ins[n++]=xfp(29,5,1,4)|3u<<6;
            ins[n++]=xfp(12,5,0,5);ins[n++]=xfp(15,6,0,5);
            ins[n++]=xfp(583,7,0,0);ins[n++]=xfp(40,1,0,5);ins[n++]=xfp(72,2,0,1);
            ins[n++]=54u<<26|1u<<21|3u<<16|16; /* stfd f1,16(r3) */
            ins[n++]=48u<<26|4u<<21|3u<<16|24; /* lfs f4,24(r3) */
            ins[n++]=59u<<26|4u<<21|4u<<16|2u<<11|21u<<1;
            ins[n++]=63u<<26|1u<<17|10u<<11|711u<<1; /* mtfsf 1,f10 */
            ins[n++]=xfp(14,6,0,4);ins[n++]=xfp(21,2,2,1);
            if(sys){ins[n++]=31u<<26|4u<<21|146u<<1;ins[n++]=xfp(21,1,1,2);} /* mtmsr r4 disables FP */
        } else ins[n++]=xfp(kind==1?12:kind==2?14:15,1,0,2);
        CPU c={0};c.pc=0x1000;c.gpr[3]=0x3000;c.msr=MSR_FP;c.sys=sys;c.mem=mem;
        c.npreg=1;c.preg[0]=(PRegion){0,65536,mem,0};
        for (unsigned r=0;r<32;r++) c.fpr[r]=values[(i+r)%(sizeof values/sizeof *values)];
        c.fpr[2]=values[i];uint64_t bits=rn;memcpy(&c.fpr[10],&bits,8);
        if(kind&&isnan(values[i])&&(rn&1)){bits=0x7ff0000000000001ull;memcpy(&c.fpr[2],&bits,8);}
        c.fpscr=(kind==0?0:rn)|(uint32_t[]){0,0x82060000u,0x02000000u}[i%3];
        uint64_t u;memcpy(&u,&values[i],8);wbe64(mem+0x3000,u);wbe64(mem+0x3008,0x3ff4000000000000ull);wbe32(mem+0x3018,0x3fa00000);
        for (unsigned j=0;j<n;j++)wbe32(mem+0x1000+j*4,ins[j]);
        memcpy(ref,mem,65536);CPU expected=c;expected.mem=ref;expected.preg[0].host=ref;
        jmsr=MSR_750|MSR_FP;jsys=sys;fp_old=0;fma_fast=fma_single_direct=1;fp_hoist=fp_round_fast=1;fp_checked=fp_guarded=0;
        fcache_active=cached;fvalid=0;memset(fmap,-1,sizeof fmap);
        for (unsigned r=1;r<=7;r++)fmap[r]=(int8_t)(24+r);
        memset(cmap,-1,sizeof cmap);cwr=0;
        pthread_jit_write_protect_np(0);
        cp=code;E(0xa9bf7bfb);E(0xa9bf77fc);E(0xaa0003fb);E(0xaa0103fc);ldrw(3,ENV,OGPR(3));ldrw(4,ENV,OGPR(4));
        exit_common=code+8192;
        for (unsigned j=0;j<n;j++)assert(tr(ins[j],0x1000+j*4)==0);
        movw(T0,0x1000+n*4);strw(T0,ENV,OFF(pc));b_to(exit_common);
        cp=exit_common;E(0xa8c177fc);E(0xa8c17bfb);E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code,(char *)(exit_common+3));
        ((void (*)(CPU *,uint8_t *))(void *)code)(&c,mem);
        assert(c.pc>=0x1000 && c.pc<=0x1000+n*4 && !(c.pc&3));
        while(expected.pc<c.pc)assert(interp_step(&expected)==0);
        if(memcmp(c.fpr,expected.fpr,sizeof c.fpr)||c.fpscr!=expected.fpscr||c.msr!=expected.msr||memcmp(mem,ref,65536)) {
            fprintf(stderr,"FP chain mismatch kind=%u input=%a RN=%u cache=%u sys=%u pc=%08x FPSCR=%08x/%08x\n",kind,values[i],rn,cached,sys,c.pc,c.fpscr,expected.fpscr);return 1;
        }
        count++;
    }
    fcache_active=fp_hoist=0;
    printf("FPR cache/rounding: %u exact chain/slow-exit checks\n",count);return 0;
}
