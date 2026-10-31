/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Register-direction copies across alias bases; exact pixel width and guards. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void reg(PtRadeon *d,unsigned a,uint32_t v){uint8_t b[4];for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(v>>(i*8));assert(!pt_radeon_mmio(d,a,b,4,1));}
int main(void){
    static uint8_t ram[4096];PtRadeonConfig c={.ram={ram,sizeof ram,NULL}};PtRadeon *d;assert(!pt_radeon_create(&d,&c));
    enum{BASE=0x1000,PITCH=256,SIZE=16*PITCH};uint8_t initial[SIZE],actual[SIZE],want[SIZE];
    for(unsigned i=0;i<SIZE;i++)initial[i]=(uint8_t)(i*13+(i/PITCH)*37);
    for(unsigned direction=0;direction<4;direction++) {
        assert(optics_frontend_gpu_write(d,BASE,initial,SIZE));memcpy(want,initial,SIZE);
        reg(d,0x1428,(BASE>>10)|(PITCH/64)<<22);reg(d,0x142c,((BASE+1024)>>10)|(PITCH/64)<<22);
        reg(d,0x146c,3u|6u<<8|0xccu<<16|2u<<24);reg(d,0x16cc,0xffffffff);reg(d,0x15d4,0);reg(d,0x16c0,direction);
        unsigned x=direction&1?0:7,y=direction&2?0:5;
        reg(d,0x1434,y<<16|x);reg(d,0x1438,y<<16|x);reg(d,0x143c,6u<<16|8u);
        /* Each row's source is the original snapshot; reverse Y is the safe overlap order. */
        if(!(direction&2))for(unsigned j=0;j<6;j++)memcpy(want+1024+j*PITCH,initial+j*PITCH,32);
        else for(unsigned j=0;j<6;j++)memmove(want+1024+j*PITCH,want+j*PITCH,32);
        assert(optics_frontend_gpu_read(d,BASE,actual,SIZE));assert(!memcmp(actual,want,SIZE));
    }
    for(unsigned bytes=2;bytes<=4;bytes+=2) {
        memset(initial,0xa5,SIZE);assert(optics_frontend_gpu_write(d,BASE,initial,SIZE));
        reg(d,0x1404,BASE);reg(d,0x1408,PITCH);reg(d,0x146c,2u|13u<<4|(bytes==2?3u:6u)<<8|0xf0u<<16);
        reg(d,0x147c,bytes==2?0xf81fu:0xff123456u);reg(d,0x16c0,3);reg(d,0x1438,3u<<16|5u);reg(d,0x143c,1u<<16|1u);
        assert(optics_frontend_gpu_read(d,BASE,actual,SIZE));unsigned at=3*PITCH+5*bytes;
        for(unsigned i=0;i<SIZE;i++)assert(actual[i]==(i>=at&&i<at+bytes?(uint8_t)((bytes==2?0xf81fu:0xff123456u)>>(8*(i-at))):0xa5));
    }
    /* Unsupported 2D cannot quietly continue under the existing strict switch. */
    reg(d,0x146c,0x12340000);reg(d,0x143c,1u<<16|1u);OpticsFrontendStats stats;optics_frontend_stats(d,&stats);
    const char *strict=getenv("OPTICS_STRICT");assert(stats.blits_refused&&stats.failed==(strict&&atoi(strict)!=0));
    pt_radeon_close(d);puts("PASS: DP_CNTL X/Y, alias-base overlap, one-pixel RGB565/ARGB8888 guards and strict refusal");return 0;
}
