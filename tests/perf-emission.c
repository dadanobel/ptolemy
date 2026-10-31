/* SPDX-License-Identifier: GPL-2.0-or-later
 * STATIC analysis only: translate representative blocks and count emitted words
 * and CPU.fpr loads. Never enter JIT code, HVF, Tiger or Metal. Not an FPS test. */
#include "../epicycle/jit.c"
#include <assert.h>
int sys_call(CPU *c){(void)c;abort();}
static uint32_t afp(unsigned op,unsigned xo,unsigned d,unsigned a,unsigned b,unsigned cc){
    return op<<26|d<<21|a<<16|b<<11|cc<<6|xo<<1;
}
int main(void){
    static uint32_t code[1<<18];static uint8_t memory[65536];
    static const char *const names[]={"double FP chain","single FMA chain","frsp chain","load/FMA/store","mfcr/rlwinm"};
    for(unsigned kind=0;kind<5;kind++)for(unsigned candidate=0;candidate<3;candidate++){
        unsigned n=0;
        for(unsigned i=0;i<20;i++){
            uint32_t words[4];unsigned count=4;
            if(kind==0){words[0]=afp(63,21,1,1,2,0);words[1]=afp(63,25,3,1,0,2);words[2]=afp(63,29,4,1,3,2);words[3]=afp(63,20,2,4,1,0);}
            else if(kind==1){for(unsigned j=0;j<4;j++)words[j]=afp(59,29,1+j,1+j,2,3);}
            else if(kind==2){for(unsigned j=0;j<4;j++)words[j]=afp(63,12,1+j,0,1+j,0);}
            else if(kind==3){words[0]=48u<<26|1u<<21|3u<<16;words[1]=48u<<26|2u<<21|3u<<16|4;words[2]=afp(59,29,1,1,1,2);words[3]=52u<<26|1u<<21|3u<<16|8;}
            else {count=2;words[0]=31u<<26|3u<<21|19u<<1;words[1]=21u<<26|3u<<21|3u<<16|3u<<11|31u<<6|31u<<1;}
            for(unsigned j=0;j<count;j++)wbe32(memory+0x1000+4*n++,words[j]);
        }
        wbe32(memory+0x1000+4*n,0x48000000u); /* terminate block, do not execute */
        CPU c={0};c.mem=memory;c.npreg=1;c.preg[0]=(PRegion){0,sizeof memory,memory,0};
        jmsr=MSR_750|MSR_FP|MSR_VEC;jsys=0;check=0;maxblk=128;fp_old=0;fma_fast=1;
        fcache_enabled=fp_hoist_enabled=fp_round_fast=fma_single_direct=cr_fuse=candidate!=0;
        fp_lazy_enabled=candidate==2;
        buf=code;bufend=code+sizeof code/sizeof *code;exit_common=code;cp=code+64;
        ++gen;nblk=nim=nstub=nside=0;zbase=0;
        uint32_t *start=translate(&c,0x1000,0x1000);unsigned loads=0;
        for(uint32_t *p=start;p<blk[0].end;p++){
            uint32_t in=*p,off=(in>>10&4095)*8;
            if((in&0xffc00000u)==0xfd400000u&&(in>>5&31)==ENV&&off>=OFF(fpr)&&off<OFF(fpr)+256)loads++;
        }
        printf("%-19s %-9s PPC=%u ARM=%lu CPU-FPR-loads=%u\n",names[kind],candidate==2?"lazy":candidate?"candidate":"control",blk[0].n,(unsigned long)(blk[0].end-start),loads);
    }
    return 0;
}
