/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Line protocol for an independent exact-rational range-exception oracle. */
#include "ppc.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
int sys_call(CPU *c){(void)c;abort();}
int main(void){
    uint64_t a,b;unsigned op,control;
    while(scanf("%u %x %"SCNx64" %"SCNx64,&op,&control,&a,&b)==4){
        uint8_t mem[16]={0};CPU c={0};c.msr=MSR_FP;c.fpscr=control;c.pc=0;c.npreg=1;
        c.preg[0]=(PRegion){.base=0,.size=sizeof mem,.host=mem};
        wbe32(mem,63u<<26|1u<<21|2u<<16|3u<<11|4u<<6|op<<1|1);
        memcpy(c.fpr+2,&a,8);memcpy(c.fpr+3,&b,8);memcpy(c.fpr+4,&b,8);
        if(interp_step(&c)||c.pc!=4)return 2;
        uint64_t t;memcpy(&t,c.fpr+1,8);
        printf("%016"PRIx64" %08x %x\n",t,c.fpscr,cr_field(&c,1));
    }
    return ferror(stdin)?3:0;
}
