/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc.h"
#include <stdio.h>
#include <stdlib.h>
int ppc_ld(CPU *c,uint32_t a,int n,uint64_t *v){(void)c;(void)a;(void)n;(void)v;abort();}
int ppc_st(CPU *c,uint32_t a,int n,uint64_t v){(void)c;(void)a;(void)n;(void)v;abort();}
void ppc_exception(CPU *c,uint32_t v,uint32_t p,uint32_t b){(void)c;(void)v;(void)p;(void)b;abort();}
int main(void){uint8_t in[528],out[524];while(fread(in,1,sizeof in,stdin)==sizeof in){CPU c={0};c.spr[287]=be32(in+4);c.vscr=be32(in+8);cr_set_field(&c,6,be32(in+12));memcpy(c.vr,in+16,512);
    int rc=ppc_altivec(&c,be32(in));wbe32(out,rc);memcpy(out+4,c.vr,512);wbe32(out+516,c.vscr);wbe32(out+520,cr_field(&c,6));if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 2;}return ferror(stdin)?2:0;}
