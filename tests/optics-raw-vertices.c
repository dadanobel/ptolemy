/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exact raw AoS snapshots, inaccessible padding, sparse indices and CPU fallback. */
#include "draw.h"
#include "r300_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t regs[0x10000/4];static uint8_t source[3*32];static size_t limit;static unsigned reads;static int reject_wide;
static void wr(OpticsR300State *s,unsigned a,uint32_t v){regs[a/4]=v;(void)optics_r300_write(s,a,v);}
static int fetch(void *ctx,uint64_t a,void *out,size_t n){(void)ctx;reads++;if(a<0x4000||a-0x4000>limit||n>limit-(a-0x4000)||(reject_wide&&n>80))return 0;memcpy(out,source+(a-0x4000),n);return 1;}
static void put(unsigned at,float x,unsigned swap){uint32_t u;memcpy(&u,&x,4);for(unsigned k=0;k<4;k++)source[at+k]=(uint8_t)(u>>(8*(swap?3-k:k)));}
int main(int argc,char **argv){
    (void)argv;int expected=argc==1;OpticsR300State *s=optics_r300_state_create(1);assert(s);
    wr(s,0x43e0,1440|1440u<<13);wr(s,0x43e4,1503|1503u<<13);wr(s,0x2090,1);wr(s,0x4e38,6u<<21|64);
    const uint32_t fs[]={0,0,0,0,0,0,0x400000,0,7u<<26,1u<<24,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    const unsigned head[]={0x4600,0x4604,0x4608,0x4610,0x4614,0x4618,0x461c,0x4620,0x46c0,0x47c0,0x48c0,0x49c0};
    for(unsigned k=0;k<12;k++)wr(s,head[k],fs[k]);
    for(unsigned be=0;be<2;be++)for(unsigned walk=1;walk<=2;walk++){
        memset(source,0xcd,sizeof source);limit=2*32+(be?24:28);reads=0;
        for(unsigned v=0;v<3;v++)for(unsigned c=0;c<6;c++)put(v*32+c*4,c==3?1.0f:(float)(v*8+c)/16.0f,be);
        for(unsigned v=0;v<3;v++){source[v*32+24]=0;source[v*32+25]=127;source[v*32+26]=255;source[v*32+27]=64;}
        wr(s,0x2140,1u<<8|(be?2:0));wr(s,0x2150,3u|((1u|1u<<8|(be?1u<<13:0))<<16));
        wr(s,0x2154,0x8004|2u<<8|1u<<13);wr(s,0x21e0,0xf688f688);wr(s,0x21e4,0xf688);
        OpticsAos aos={.count=be?2:3,.array={{4,8,0x4000},{2,8,0x4010},{1,8,0x4018}}};
        uint32_t p[]={4u|walk<<4|3u<<16,2u|0u<<16,1};OpticsDraw d;const char *why=NULL;
        assert(optics_draw_build(&d,regs,s,walk==1?0x36:0x34,p,walk==1?3:1,&aos,fetch,NULL,&why));
        assert((d.raw_vertex!=NULL)==expected);assert((d.vertex==NULL)==expected);assert(d.vertices==3);
        if(expected){assert(reads==1&&d.raw_stride==32);for(size_t k=limit;k<sizeof source;k++)assert(d.raw_vertex[k]==0);}
        for(unsigned v=0;v<3;v++){
            float input[OPTICS_DRAW_INPUTS][4];optics_draw_input(&d,v,input);
            for(unsigned c=0;c<4;c++)assert(input[0][c]==(c==3?1.0f:(float)(v*8+c)/16.0f));
            assert(input[1][0]==(float)(v*8+4)/16.0f&&input[1][1]==(float)(v*8+5)/16.0f&&input[1][2]==0&&input[1][3]==1);
            if(!be){assert(input[2][0]==0&&input[2][1]==127/255.0f&&input[2][2]==1&&input[2][3]==64/255.0f);}
            for(unsigned j=be?2:3;j<16;j++)assert(input[j][0]==0&&input[j][1]==0&&input[j][2]==0&&input[j][3]==1);
        }
        if(walk==1)assert(d.index[0]==2&&d.index[1]==0&&d.index[2]==1);
        /* Producer changes after snapshot cannot change either representation. */
        memset(source,0,sizeof source);float input[16][4];optics_draw_input(&d,2,input);assert(input[0][0]==1.0f);
        OpticsVertexOut out[3];assert(optics_draw_vertices(&d,out,&why));assert(out[2].window[0]==1.0f);
        optics_draw_free(&d);assert(!d.vertex&&!d.raw_vertex&&!d.index);optics_draw_free(&d);
    }
    /* A layout outside the raw profile must still build through the normalized decoder. */
    wr(s,0x2140,1u<<8|1);wr(s,0x2150,3|1u<<13);wr(s,0x21e0,0xf688);limit=sizeof source;
    OpticsAos aos={.count=1,.array={{4,8,0x4000}}};uint32_t p[]={4|2u<<4|3u<<16};OpticsDraw d;const char *why=NULL;
    assert(optics_draw_build(&d,regs,s,0x34,p,1,&aos,fetch,NULL,&why));assert(d.vertex&&!d.raw_vertex);optics_draw_free(&d);
    /* A failed speculative combined fetch falls back without publishing borrowed bytes. */
    for(unsigned c=0;c<24;c++)put(4*c,(float)(c+1),0);
    wr(s,0x2140,1u<<8);wr(s,0x2150,3u|((1u|1u<<8|1u<<13)<<16));wr(s,0x21e0,0xf688f688);aos.count=2;aos.array[1]=aos.array[0];aos.array[1].components=2;aos.array[1].offset+=16;reject_wide=1;
    assert(optics_draw_build(&d,regs,s,0x34,p,1,&aos,fetch,NULL,&why));assert(d.vertex&&!d.raw_vertex);float a[16][4],b[16][4];optics_draw_input(&d,0,a);optics_draw_free(&d);
    /* PVS bypass: the stream vectors in order are the output vertex, whatever their PVS input field (here 8). */
    wr(s,0x2150,3u|((1u|8u<<8|1u<<13)<<16));assert(optics_draw_build(&d,regs,s,0x34,p,1,&aos,fetch,NULL,&why));optics_draw_input(&d,0,b);optics_draw_free(&d);
    assert(a[1][0]!=0&&!memcmp(a,b,sizeof a));wr(s,0x2150,3u|((1u|1u<<8|1u<<13)<<16));
    reject_wide=0;wr(s,0x2090,0);assert(!optics_draw_build(&d,regs,s,0x34,p,1,&aos,fetch,NULL,&why));assert(!d.vertex&&!d.raw_vertex&&!d.index);
    optics_r300_state_destroy(s);puts("PASS: raw AoS LE/BE, UNORM8, indexed fetch, zero tail, immutable bytes and CPU fallback");return 0;
}
