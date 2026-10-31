/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A rejected copy must not be reported as a completed blit. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
static void wr(PtRadeon *d,unsigned a,uint32_t v){uint8_t b[4];for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(v>>(8*i));assert(!pt_radeon_mmio(d,a,b,4,1));}
int main(void){
    static uint8_t ram[4096];PtRadeonConfig c={.ram={ram,sizeof ram,NULL}};PtRadeon *d;assert(!pt_radeon_create(&d,&c));
    wr(d,0x146c,3u|6u<<8|0xccu<<16|2u<<24);wr(d,0x16cc,0xffffffff);wr(d,0x15d4,0);
    wr(d,0x1408,256);wr(d,0x15b0,256);wr(d,0x1434,0);wr(d,0x1438,0);
    for(unsigned bad=0;bad<2;bad++){
        wr(d,0x15ac,bad?0x1000:0x40000000);wr(d,0x1404,bad?0x40000000:0x2000);
        OpticsFrontendStats before,after;optics_frontend_stats(d,&before);wr(d,0x143c,1u<<16|1u);optics_frontend_stats(d,&after);
        assert(after.blits==before.blits&&after.blit_bytes==before.blit_bytes);
        assert(after.blits_refused==before.blits_refused+1);
    }
    wr(d,0x15ac,0x1000);wr(d,0x1404,0x2000);wr(d,0x143c,1u<<16|1u);
    OpticsFrontendStats stats;optics_frontend_stats(d,&stats);assert(stats.blits==1&&stats.blit_bytes==4);
    pt_radeon_close(d);puts("PASS: failed source/destination do not increment completed blits");return 0;
}
