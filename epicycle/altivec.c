/* SPDX-License-Identifier: GPL-2.0-or-later
 * Classic G4 VMX, canonical big-endian register bytes.
 * Encodings, lane order, saturation and VSCR as defined by the AltiVec Technology Programming
 * Environments Manual; they are explicit here. */
#include "ppc.h"
#include <math.h>
#include <fenv.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
static uint64_t vmx_alu,vmx_memory,vmx_op[2048];   /* per extended opcode (VA forms: their low 6 bits) */
void ppc_altivec_stats(void){
    uint64_t n[2048];memcpy(n,vmx_op,sizeof n);
    fprintf(stderr,"VMX interpreter: arithmetic=%llu memory=%llu",(unsigned long long)vmx_alu,(unsigned long long)vmx_memory);
    for(unsigned k=0;k<8;k++){unsigned m=0;for(unsigned x=1;x<2048;x++)if(n[x]>n[m])m=x;
        if(!n[m])break;fprintf(stderr,"%s%#x×%llu",k?" ":"; top xo ",m,(unsigned long long)n[m]);n[m]=0;}
    fputc('\n',stderr);
}
#pragma STDC FENV_ACCESS ON
static uint32_t vu(const uint8_t *v,unsigned n,unsigned i){v+=n*i;return n==1?v[0]:n==2?be16(v):be32(v);}
static int64_t vs(const uint8_t *v,unsigned n,unsigned i){uint32_t x=vu(v,n,i);return n==1?(int8_t)x:n==2?(int16_t)x:(int32_t)x;}
static void vp(uint8_t *v,unsigned n,unsigned i,uint32_t x){v+=n*i;if(n==1)*v=x;else if(n==2)wbe16(v,x);else wbe32(v,x);}
static uint32_t sat(CPU *c,int64_t x,unsigned bits,int sign){int64_t lo=sign?-(INT64_C(1)<<(bits-1)):0,hi=sign?(INT64_C(1)<<(bits-1))-1:(INT64_C(1)<<bits)-1;
    if(x<lo){x=lo;c->vscr|=1;}if(x>hi){x=hi;c->vscr|=1;}return (uint32_t)x;}
static uint32_t vf_bits(CPU *c,uint32_t u){return (c->vscr&0x10000) && !(u&0x7f800000)?u&0x80000000:u;}
static float vf(uint32_t u){float f;memcpy(&f,&u,4);return f;}
static uint32_t fv(CPU *c,float f){uint32_t u;memcpy(&u,&f,4);return vf_bits(c,u);}
static int nan32(uint32_t v){return (v&0x7fffffff)>0x7f800000;}
static int vec_float(CPU *c,unsigned xo,unsigned va,const uint8_t *a,const uint8_t *b,const uint8_t *k,uint8_t *r){
    unsigned low=xo&63;
    if((xo&63)!=10 && low!=46 && low!=47 && (xo&0x3ff)!=0xc6 && (xo&0x3ff)!=0x1c6 && (xo&0x3ff)!=0x2c6 && (xo&0x3ff)!=0x3c6)return 0;
    if(low==10 && xo>0x44a)return 0;
    fenv_t env;feholdexcept(&env);fesetround(FE_TONEAREST);unsigned all=1,none=1,inbounds=1;
    for(unsigned i=0;i<4;i++) {
        uint32_t au=vf_bits(c,be32(a+4*i)),bu=vf_bits(c,be32(b+4*i)),ku=vf_bits(c,be32(k+4*i));float af=vf(au),bf=vf(bu),kf=vf(ku),f=0;uint32_t u=0;int integer=0;
        if((xo&0x3f)==6){unsigned op=xo&0x3ff;int ok=0;
            if(op==0x3c6){u=(isnan(af)||isnan(bf))?0xc0000000u:((uint32_t)!(af<=bf)<<31)|((uint32_t)!(af>=-bf)<<30);inbounds&=u==0;}
            else {ok=op==0xc6?af==bf:op==0x1c6?af>=bf:af>bf;u=ok?UINT32_MAX:0;all&=ok;none&=!ok;}integer=1;
        }else if(low==46 || low==47) {
            f=fmaf(af,kf,low==46?bf:-bf);if(low==47)f=-f;
            if(nan32(au)||nan32(bu)||nan32(ku)){u=(nan32(au)?au:nan32(bu)?bu:ku)|0x400000;integer=1;}
        }else switch(xo) {
        case 0x00a:f=af+bf;break;case 0x04a:f=af-bf;break;
        case 0x10a:f=1.0f/bf;break;case 0x14a:f=1.0f/sqrtf(bf);break;
        case 0x18a:f=exp2f(bf);break;case 0x1ca:f=log2f(bf);break;
        case 0x20a:f=nearbyintf(bf);break;case 0x24a:f=truncf(bf);break;
        case 0x28a:f=ceilf(bf);break;case 0x2ca:f=floorf(bf);break;
        case 0x30a:f=ldexpf((float)be32(b+4*i),-(int)va);break;
        case 0x34a:f=ldexpf((float)(int32_t)be32(b+4*i),-(int)va);break;
        case 0x38a:case 0x3ca:{double x=trunc(ldexp((double)bf,va));integer=1;int sign=xo==0x3ca;
            if(isnan(x))u=0;
            else if(x<(sign?-2147483648.0:0.0)){u=sign?0x80000000:0;c->vscr|=1;}
            else if(x>(sign?2147483647.0:4294967295.0)){u=sign?0x7fffffff:UINT32_MAX;c->vscr|=1;}
            else u=sign?(uint32_t)(int32_t)x:(uint32_t)x;break;}
        case 0x40a:case 0x44a:
            if(!(au&0x7fffffff) && !(bu&0x7fffffff)){u=xo==0x40a?au&bu:au|bu;integer=1;}
            else f=xo==0x40a?fmaxf(af,bf):fminf(af,bf);break;
        default:fesetenv(&env);return 0;
        }
        if(!integer){int two=xo==0x00a || xo==0x04a || xo==0x40a || xo==0x44a;
            if(two && (nan32(au)||nan32(bu)))u=(nan32(au)?au:bu)|0x400000;
            else if(!two && low!=46 && low!=47 && xo!=0x30a && xo!=0x34a && nan32(bu))u=bu|0x400000;
            else u=isnan(f)?0x7fc00000:fv(c,f);
        }wbe32(r+4*i,u);
    }
    if((xo&63)==6 && (xo&0x400))c->cr[6]=cr_make((xo&0x3ff)==0x3c6?0:all,0,(xo&0x3ff)==0x3c6?inbounds:none,0);
    fesetenv(&env);return 1;
}
int ppc_altivec(CPU *c,uint32_t in){
    vmx_alu++;vmx_op[(in&63)>=32 && (in&63)<48?in&63:in&2047]++;
    unsigned d=in>>21&31,ia=in>>16&31,ib=in>>11&31,ic=in>>6&31,xo=in&2047,low=xo&63;
    uint8_t a[16],b[16],k[16],r[16]={0};memcpy(a,c->vr[ia],16);memcpy(b,c->vr[ib],16);memcpy(k,c->vr[ic],16);
    if(low==42 || low==43 || low==44){for(unsigned i=0;i<16;i++){
        if(low==42)r[i]=(a[i]&~k[i])|(b[i]&k[i]);
        else {unsigned j=low==43?k[i]&31:i+(ic&15);r[i]=j<16?a[j]:b[j-16];}
    }goto done;}
    if(low>=32 && low<=41 && low!=35){
        if(low<=34){for(unsigned i=0;i<8;i++){int64_t x;
            if(low==34)x=(uint64_t)vu(a,2,i)*vu(b,2,i)+vu(k,2,i);
            else x=((vs(a,2,i)*vs(b,2,i)+(low==33?0x4000:0))>>15)+vs(k,2,i);
            vp(r,2,i,low==34?(uint32_t)x:sat(c,x,16,1));}
        }else for(unsigned i=0;i<4;i++){int64_t x=low==36 || low==38 || low==39?vu(k,4,i):vs(k,4,i);
            if(low==36 || low==37){for(unsigned j=0;j<4;j++)x+=(low==36?vu(a,1,4*i+j):vs(a,1,4*i+j))*(int64_t)vu(b,1,4*i+j);}
            else for(unsigned j=0;j<2;j++)x+=(low==38 || low==39?(int64_t)vu(a,2,2*i+j):vs(a,2,2*i+j))*(low==38 || low==39?(int64_t)vu(b,2,2*i+j):vs(b,2,2*i+j));
            vp(r,4,i,(low==39 || low==41)?sat(c,x,32,low==41):(uint32_t)x);
        }goto done;
    }
    if(vec_float(c,xo,ia,a,b,k,r))goto done;
    if(xo==0x604){wbe32(r+12,c->vscr);goto done;}
    if(xo==0x644){c->vscr=be32(b+12)&0x10001;return 1;}
    if(xo==0x180 || xo==0x580){for(unsigned i=0;i<4;i++){uint32_t x=vu(a,4,i),y=vu(b,4,i);vp(r,4,i,xo==0x180?((uint64_t)x+y)>>32:x>=y);}goto done;}
    unsigned lane=(xo>>6)&3,n=1u<<lane,base=xo&~0xc0u;
    if(low==0 && lane<3 && (base==0 || base==0x200 || base==0x300 || base==0x400 || base==0x600 || base==0x700)){
        for(unsigned i=0;i<16/n;i++){int sign=base&0x100,sub=base&0x400;int64_t x=sign?vs(a,n,i):vu(a,n,i),y=sign?vs(b,n,i):vu(b,n,i);x=sub?x-y:x+y;
            vp(r,n,i,(base&0x200)?sat(c,x,n*8,sign):(uint32_t)x);}goto done;
    }
    if(low==2 && lane<3 && base<=0x502){for(unsigned i=0;i<16/n;i++){int sign=base&0x100;int64_t x=sign?vs(a,n,i):vu(a,n,i),y=sign?vs(b,n,i):vu(b,n,i);
        vp(r,n,i,(uint32_t)(base&0x400?(x+y+1)>>1:base&0x200?(x<y?x:y):(x>y?x:y)));}goto done;}
    if(xo==0x404 || xo==0x444 || xo==0x484 || xo==0x4c4 || xo==0x504){for(unsigned i=0;i<16;i++)r[i]=xo==0x404?a[i]&b[i]:xo==0x444?a[i]&~b[i]:xo==0x484?a[i]|b[i]:xo==0x4c4?a[i]^b[i]:~(a[i]|b[i]);goto done;}
    if(low==4 && lane<3 && base<0x400){for(unsigned i=0;i<16/n;i++){uint32_t x=vu(a,n,i),sh=vu(b,n,i)&(n*8-1),v;
        if(base==4)v=(x<<sh)|(x>>((n*8-sh)&(n*8-1)));else if(base==0x104)v=x<<sh;else if(base==0x204)v=x>>sh;else v=(uint32_t)(vs(a,n,i)>>sh);vp(r,n,i,v);}goto done;}
    if(xo==0x1c4 || xo==0x2c4){unsigned sh=b[15]&7;for(unsigned i=0;i<16;i++)r[i]=xo==0x1c4?(a[i]<<sh)|((i+1<16?a[i+1]:0)>>(8-sh)):(a[i]>>sh)|((i?a[i-1]:0)<<(8-sh));goto done;}
    if(low==8 && lane<2 && base<=0x308){for(unsigned i=0;i<8/n;i++){unsigned j=2*i+!(base&0x200);int64_t x=(base&0x100)?vs(a,n,j)*vs(b,n,j):(int64_t)vu(a,n,j)*vu(b,n,j);vp(r,2*n,i,(uint32_t)x);}goto done;}
    unsigned cmp=xo&~0x400u,cb=cmp&~0xc0u;
    if((cmp&63)==6 && ((cmp>>6)&3)<3 && (cb==6 || cb==0x206 || cb==0x306)){
        unsigned all=1,none=1;n=1u<<((cmp>>6)&3);for(unsigned i=0;i<16/n;i++){int ok=cb==6?vu(a,n,i)==vu(b,n,i):cb==0x206?vu(a,n,i)>vu(b,n,i):vs(a,n,i)>vs(b,n,i);vp(r,n,i,ok?UINT32_MAX:0);all&=ok;none&=!ok;}
        if(xo&0x400)c->cr[6]=cr_make(all,0,none,0);goto done;
    }
    if(low==12 && lane<3 && base<=0x30c){
        for(unsigned i=0;i<16/n;i++){
            if(base==0xc || base==0x10c){unsigned j=i/2+(base==0x10c?8/n:0);vp(r,n,i,vu(i&1?b:a,n,j));}
            else if(base==0x20c)vp(r,n,i,vu(b,n,ia&(16/n-1)));
            else vp(r,n,i,(uint32_t)((int)(ia^16)-16));
        }goto done;
    }
    if(xo==0x40c || xo==0x44c){unsigned sh=(b[15]>>3)&15;for(unsigned i=0;i<16;i++){int j=xo==0x40c?(int)i+sh:(int)i-sh;r[i]=j>=0 && j<16?a[j]:0;}goto done;}
    if(low==14 && xo<0x200){n=xo&0x40?4:2;unsigned mode=xo&~0x40u,sign=mode==0x10e || mode==0x18e;
        for(unsigned i=0;i<32/n;i++){const uint8_t *v=i<16/n?a:b;unsigned j=i%(16/n);int64_t x=sign?vs(v,n,j):vu(v,n,j);
            vp(r,n/2,i,mode==0xe?(uint32_t)x:sat(c,x,n*4,mode==0x18e));}goto done;
    }
    if(xo==0x20e || xo==0x24e || xo==0x28e || xo==0x2ce){n=xo&0x40?2:1;unsigned off=xo&0x80?8/n:0;for(unsigned i=0;i<8/n;i++)vp(r,n*2,i,(uint32_t)vs(b,n,off+i));goto done;}
    if(xo==0x30e){for(unsigned i=0;i<8;i++){uint32_t u=vu(i<4?a:b,4,i&3);vp(r,2,i,((u>>9)&0xfc00)|((u>>6)&0x3e0)|((u>>3)&31));}goto done;}
    if(xo==0x34e || xo==0x3ce){for(unsigned i=0;i<4;i++){uint32_t u=vu(b,2,i+(xo==0x3ce?4:0));vp(r,4,i,((u&0x8000)?0xff000000u:0)|(((u>>10)&31)<<16)|(((u>>5)&31)<<8)|(u&31));}goto done;}
    if(xo==0x608 || xo==0x708 || xo==0x648){for(unsigned i=0;i<4;i++){int sign=xo!=0x608;int64_t x=sign?vs(b,4,i):vu(b,4,i);
        if(xo==0x648)for(unsigned j=0;j<2;j++)x+=vs(a,2,2*i+j);
        else for(unsigned j=0;j<4;j++)x+=sign?vs(a,1,4*i+j):vu(a,1,4*i+j);
        vp(r,4,i,sat(c,x,32,sign));}goto done;}
    if(xo==0x688 || xo==0x788){for(unsigned i=xo==0x688?1:3;i<4;i+=2){int64_t x=vs(b,4,i);for(unsigned j=xo==0x688?i-1:0;j<=i;j++)x+=vs(a,4,j);vp(r,4,i,sat(c,x,32,1));}goto done;}
    return 0;
done:memcpy(c->vr[d],r,16);return 1;
}
/* Return -1: another op31 instruction; 0: done; 1: exception delivered. */
int ppc_altivec_mem(CPU *c,uint32_t in){
    unsigned xo=in>>1&1023,d=in>>21&31,a=in>>16&31,b=in>>11&31;uint32_t ea=(a?c->gpr[a]:0)+c->gpr[b];
    switch(xo){case 6:case 38:case 7:case 39:case 71:case 103:case 359:case 135:case 167:case 199:case 231:case 487:case 342:case 374:case 822:case 758:break;default:return -1;}
    if(!ppc_has_vec(c))return -1;
    if(xo==342 || xo==374 || xo==822 || xo==758)return 0; /* cache/data-stream hints */
    if(c->sys && !(c->msr&MSR_VEC)){ppc_exception(c,0xf20,c->pc,0);return 1;}
    vmx_memory++;
    if(xo==6 || xo==38){for(unsigned i=0;i<16;i++)c->vr[d][i]=(xo==6?(ea&15):16-(ea&15))+i;return 0;}
    unsigned n=xo==7 || xo==135?1:xo==39 || xo==167?2:xo==71 || xo==199?4:16;
    ea&=~(n-1);unsigned off=n==16?0:ea&15;int store=xo==135 || xo==167 || xo==199 || xo==231 || xo==487;uint8_t tmp[16];memcpy(tmp,c->vr[d],16);
    for(unsigned i=0;i<n;){unsigned z=n-i>=4?4:n-i;uint64_t v;
        if(store){v=z==1?tmp[off+i]:z==2?be16(tmp+off+i):be32(tmp+off+i);if(ppc_st(c,ea+i,z,v))return 1;}
        else {if(ppc_ld(c,ea+i,z,&v))return 1;vp(tmp,1,off+i,v>>(8*(z-1)));if(z==2)wbe16(tmp+off+i,v);else if(z==4)wbe32(tmp+off+i,v);}i+=z;
    }
    if(!store)memcpy(c->vr[d],tmp,16);return 0;
}
