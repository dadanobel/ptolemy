/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
extern int trace_unimpl;
int sys_call(CPU *c){(void)c;abort();}
static uint32_t x(unsigned xo,unsigned d,unsigned a,unsigned b){return 31u<<26|d<<21|a<<16|b<<11|xo<<1;}
static void step(CPU *c,uint32_t in){c->pc=0x100;wbe32(c->mem+0x100,in);(void)interp_step(c);}
static void profile(uint32_t pvr){
    CPU *c=calloc(1,sizeof *c);assert(c);c->mem=calloc(1,0x10000);assert(c->mem);c->preg[0]=(PRegion){0,0x10000,c->mem,0};c->npreg=1;c->sys=1;c->spr[287]=pvr;
    step(c,0x10000484);assert(c->pc==0xf20 && c->spr[SPR_SRR0]==0x100); /* vector unavailable */
    c->msr=MSR_VEC;c->gpr[3]=0x2003;c->gpr[4]=0;
    for(unsigned i=0;i<32;i++)c->mem[0x2000+i]=i;
    step(c,x(103,7,3,4));for(unsigned i=0;i<16;i++)assert(c->vr[7][i]==i); /* lvx aligns down */
    memset(c->vr[8],0xaa,16);step(c,x(39,8,3,4));assert(c->vr[8][2]==2 && c->vr[8][3]==3 && c->vr[8][4]==0xaa);
    step(c,x(6,9,3,4));step(c,x(38,10,3,4));for(unsigned i=0;i<16;i++){assert(c->vr[9][i]==i+3);assert(c->vr[10][i]==i+13);}
    c->gpr[3]=0x3007;uint32_t ea;assert(ppc_store_window(c,x(231,7,3,4),&ea)==16 && ea==0x3000);
    step(c,x(231,7,3,4));assert(!memcmp(c->mem+0x3000,c->vr[7],16));
    assert(ppc_store_window(c,x(199,7,3,4),&ea)==4 && ea==0x3004);
    memset(c->mem+0x3000,0,16);step(c,x(199,7,3,4));assert(be32(c->mem+0x3004)==0x04050607 && c->mem[0x3003]==0);
    c->gpr[3]=0x7000;c->msr=MSR_VEC|MSR_DR;memset(c->vr[11],0xa5,16); /* no HTAB translation: atomic destination */
    step(c,x(103,11,3,4));assert(c->pc==0x300 && c->spr[SPR_DAR]==0x7000);for(unsigned i=0;i<16;i++)assert(c->vr[11][i]==0xa5);
    c->msr=MSR_VEC|MSR_FP;ppc_exception(c,0x500,0x4000,0);assert(c->spr[SPR_SRR1]&MSR_VEC);assert(!(c->msr&MSR_VEC));
    step(c,19u<<26|50<<1);assert(c->pc==0x4000 && (c->msr&MSR_VEC));
    c->msr=0;c->gpr[3]=MSR_VEC;step(c,x(146,3,0,0));assert(c->msr&MSR_VEC);
    c->msr=MSR_PR;c->gpr[3]=0x12345678;step(c,x(467,3,0,8));assert(c->spr[256]==0x12345678);step(c,x(339,4,0,8));assert(c->gpr[4]==0x12345678 && c->pc==0x104);
    c->msr=MSR_VEC;c->fpscr=0x12345678;fesetround(FE_DOWNWARD);step(c,0x1000000a);assert(c->fpscr==0x12345678 && fegetround()==FE_DOWNWARD);fesetround(FE_TONEAREST);
    free(c->mem);free(c);
}
static CPU *machine(uint32_t pvr) {
    CPU *c=calloc(1,sizeof *c);assert(c);
    c->mem=calloc(1,0x10000);assert(c->mem);c->sys=1;c->spr[287]=pvr;
    c->preg[0]=(PRegion){0,0x10000,c->mem,0};c->npreg=1;return c;
}
static void drop(CPU *c){free(c->mem);free(c);}
static void spr_write(CPU *c,unsigned spr,uint32_t value) {
    c->gpr[3]=value;step(c,x(467,3,spr&31,spr>>5));assert(c->pc==0x104);
}
static void vector_memory(uint32_t pvr) {
    /* All twelve VMX memory/address mnemonics, plus six stream forms. */
    const unsigned ops[]={7,39,71,6,38,103,359,135,167,199,231,487};
    CPU *c=machine(pvr);c->gpr[3]=0x2007;
    for(unsigned k=0;k<sizeof ops/sizeof *ops;k++) {
        c->msr=0;step(c,x(ops[k],7,3,4));assert(c->pc==0xf20);
        c->msr=MSR_VEC;
        for(unsigned j=0;j<16;j++){c->vr[7][j]=j+16;c->mem[0x2000+j]=j;}
        step(c,x(ops[k],7,3,4));assert(c->pc==0x104);
        if(ops[k]==103 || ops[k]==359)for(unsigned j=0;j<16;j++)assert(c->vr[7][j]==j);
        if(ops[k]==231 || ops[k]==487)for(unsigned j=0;j<16;j++)assert(c->mem[0x2000+j]==j+16);
        if(ops[k]==7)assert(c->vr[7][7]==7 && c->vr[7][6]==22);
        if(ops[k]==39)assert(be16(c->vr[7]+6)==0x0607 && c->vr[7][5]==21);
        if(ops[k]==71)assert(be32(c->vr[7]+4)==0x04050607 && c->vr[7][3]==19);
        if(ops[k]==135)assert(c->mem[0x2007]==23 && c->mem[0x2006]==6);
        if(ops[k]==167)assert(be16(c->mem+0x2006)==0x1617 && c->mem[0x2005]==5);
        if(ops[k]==199)assert(be32(c->mem+0x2004)==0x14151617 && c->mem[0x2003]==3);
    }
    c->msr=0;
    for(unsigned t=0;t<2;t++)for(unsigned k=0;k<3;k++) {
        const unsigned op[]={342,374,822};step(c,x(op[k],3|(t<<4),k==2?0:3,k==2?0:4));assert(c->pc==0x104);
    }
    step(c,x(758,0,3,4));assert(c->pc==0x104); /* dcba is a G4 cache hint. */
    drop(c);
}
static unsigned external_calls;
static void external_bus(CPU *c,unsigned rid,uint32_t pa,uint8_t b[4],int store) {
    (void)c;assert(rid==10 && pa==0x2804);external_calls++;
    if(store)assert(be32(b)==0x12345678);else wbe32(b,0x12345678);
}
static void external_control(void) {
    CPU *c=machine(0x80030102);c->env_external=external_bus;c->gpr[3]=0x2804;
    c->spr[536]=3;c->spr[537]=2;c->spr[282]=0x8000000a;
    c->msr=MSR_PR|MSR_DR;step(c,x(310,5,3,4));assert(c->pc==0x104 && c->gpr[5]==0x12345678);
    step(c,x(438,5,3,4));assert(c->pc==0x104 && external_calls==2);
    assert(!be32(c->mem+0x2804)); /* Resource-ID bus must not alias guest RAM. */
    c->spr[282]=0;c->gpr[3]++;step(c,x(438,5,3,4));
    assert(c->pc==0x300 && c->spr[SPR_DSISR]==0x02100000 && c->spr[SPR_DAR]==0x2805);
    c->spr[282]=0x8000000a;c->msr=MSR_DR;step(c,x(310,5,3,4));assert(c->pc==0x600 && external_calls==2);
    c->gpr[3]=0x2804;c->spr[536]=0;c->sr[0]=0x80000000;c->msr=MSR_DR;
    step(c,x(310,5,3,4));assert(c->pc==0x300 && c->spr[SPR_DSISR]==0x04000000);
    drop(c);
}
static void extended_bats(void) {
    CPU *c=machine(0x80030102);uint32_t pa;int wr;
    spr_write(c,568,0x30000003);spr_write(c,569,0x40000002);
    assert(pt_xlate(c,0x30000100,0,0,&pa,&wr)!=0); /* disabled high BATs */
    spr_write(c,1008,HID0_HIGH_BAT);assert(ppc_bat_count(c)==8);
    assert(!pt_xlate(c,0x30000100,0,0,&pa,&wr) && pa==0x40000100 && wr);
    spr_write(c,560,0x50000003);spr_write(c,561,0x60000002);
    assert(!pt_xlate(c,0x50000100,2,1,&pa,&wr) && pa==0x60000100);
    spr_write(c,561,0x6000000a);assert(pt_xlate(c,0x50000100,2,1,&pa,&wr)==0x10000000);
    spr_write(c,1008,HID0_HIGH_BAT|HID0_XBSEN);
    spr_write(c,536,0x1ffff);spr_write(c,537,2);
    assert(!pt_xlate(c,0x90000100,0,0,&pa,&wr) && pa==0x90000100);
    spr_write(c,1008,HID0_HIGH_BAT);assert(c->xbsen);
    assert(!pt_xlate(c,0xf0000100,0,0,&pa,&wr) && pa==0xf0000100);
    drop(c);
}
static void software_tlb(void) {
    CPU *c=machine(0x80030102);uint32_t pa,in;int wr;
    c->sr[3]=0x20000abc;c->spr[1008]=HID0_STEN;c->spr[981]=0x80000000u|(0xabc<<7);
    c->spr[982]=0x4082;c->gpr[5]=0x30002001;
    step(c,x(978,0,0,5));assert(c->pc==0x104);
    assert(!pt_xlate(c,0x30002020,1,1,&pa,&wr) && pa==0x4020 && wr);
    c->tlb[0][5].lo&=~0x80u;c->msr=MSR_VEC|MSR_PR|MSR_DR;
    uint32_t why=pt_xlate(c,0x30002020,1,1,&pa,&wr);assert(why==(PT_TLB_MISS|PT_TLB_CHANGED));
    ppc_mmu_fault(c,0x30002020,1,why);assert(c->pc==0x1200 && (c->spr[SPR_SRR1]&0x180000)==0x180000 && (c->spr[SPR_SRR1]&MSR_VEC));
    assert(c->spr[980]==0x30002020); /* LRU opposite matched way; C-bit handler inverts it. */
    c->msr=0;c->gpr[5]=0x30002001;c->spr[982]=0x4082;
    step(c,x(1010,0,0,5));assert(c->pc==0x104);wbe32(c->mem+0x4000,0x60000000);
    c->msr=MSR_IR;c->pc=0x30002000;assert(!ppc_ifetch(c,c->pc,&in) && in==0x60000000);
    c->msr=0;c->gpr[5]=0x90042000;step(c,x(306,0,0,5)); /* same TLB index, different page/segment */
    assert(!c->tlb[0][5].valid && !c->tlb[1][5].valid);
    c->msr=MSR_IR|MSR_VEC;c->pc=0x30002000;assert(ppc_ifetch(c,c->pc,&in) && c->pc==0x1000);
    assert(c->spr[981]==(0x80000000u|(0xabc<<7)) && (c->spr[SPR_SRR1]&MSR_VEC));
    c->msr=MSR_PR;step(c,x(978,0,0,5));assert(c->pc==0x700 && (c->spr[SPR_SRR1]&0x40000));
    drop(c);
    c=machine(0x000c0209);step(c,x(978,0,0,5));assert(c->pc==0x700 && (c->spr[SPR_SRR1]&0x80000));drop(c);
}
static void cache_control(void) { /* 7447A L2CR/L3CR as XNU init745X and cacheInit use them */
    CPU *c=machine(0x80030102);
    spr_write(c,1017,0x80000000u|0x00200000u|0x00100000u|0x00010000u|0x00000800u|1u);   /* L2E, L2I, IONLY, DONLY, L2HWF, L2IP */
    c->gpr[13]=0;step(c,x(339,13,1017&31,1017>>5));assert(c->pc==0x104 && c->gpr[13]==0x80110000u);   /* invalidate/flush done */
    spr_write(c,1018,0x80000000u);c->gpr[13]=0x80000000u;   /* an L2CR value still in r13 must not become an L3 */
    step(c,x(339,13,1018&31,1018>>5));assert(c->pc==0x104 && c->gpr[13]==0);
    drop(c);
}
static uint32_t ext_dec(CPU *c,uint32_t ack){(void)c;(void)ack;return PEND_EXT|PEND_DEC;}
static void dec_behind_ext(void){ /* EXT and DEC together: EXT first, and DEC stays marked as waiting for EE (JIT rfi/mtmsr exit) */
    CPU c={.sys=1};uint8_t mem[4096]={0};c.mem=mem;c.preg[0]=(PRegion){0,sizeof mem,mem,0};c.npreg=1;c.spr[287]=0x80030102;
    c.msr=MSR_EE;c.pc=0x100;c.env_irq=ext_dec;ppc_irq(&c);assert(c.pc==0x500 && !(c.msr&MSR_EE) && c.pend_masked);}
int main(void){trace_unimpl=0;dec_behind_ext();profile(0x000c0209);profile(0x80030102);
    vector_memory(0x000c0209);vector_memory(0x80030102);external_control();extended_bats();software_tlb();cache_control();
    CPU c={.sys=1};uint8_t mem[4096]={0};c.mem=mem;c.preg[0]=(PRegion){0,sizeof mem,mem,0};c.npreg=1;c.spr[287]=0x00080301;step(&c,0x10000484);assert(c.pc==0x700 && (c.spr[SPR_SRR1]&0x80000));
    puts("G4: all 18 VMX memory/stream forms; VEC/DSI, rfi/mtmsr, VRSAVE, fenv; EAR external bus, high/extended BATs, software TLB/C-bit/miss/index invalidation; 7447A L2CR/L3CR; G3 illegal; DEC behind EXT: PASS");
}
