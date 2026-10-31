/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "draw.h"
#include "r300_state.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t reg[0x10000/4];static uint8_t memory[512];static unsigned limit,reads,hole;
static void wr(OpticsR300State *s,unsigned a,uint32_t v){reg[a/4]=v;(void)optics_r300_write(s,a,v);}
static int fetch(void *p,uint64_t a,void *out,size_t n){(void)p;reads++;if(hole&&a<0x4008&&a+n>0x4004)return 0;if(a<0x4000||a-0x4000>limit||n>limit-(a-0x4000))return 0;memcpy(out,memory+(a-0x4000),n);return 1;}
static void store(uint8_t *p,uint32_t value,unsigned mode){static const uint8_t byte[4][4]={{0,1,2,3},{1,0,3,2},{3,2,1,0},{2,3,0,1}};for(unsigned k=0;k<4;k++)p[k]=(uint8_t)(value>>(8*byte[mode][k]));}
static float f32(uint32_t v){float f;memcpy(&f,&v,4);return f;}
static void wanted(unsigned type,unsigned sign,unsigned norm,unsigned method,unsigned words,float out[4]){
    static const uint32_t floating[]={0x3f800000,0xc0000000,0x3e800000,0x80000000};
    out[0]=out[1]=out[2]=0;out[3]=1;
    if(type<4){for(unsigned c=0;c<=type;c++)out[c]=c<words?f32(floating[c]):0;return;}
    const float byte_u[4]={127,128,1,255},byte_s[4]={127,-128,1,-1},short_u[4]={32767,32768,1,65535},short_s[4]={32767,-32768,1,-1};
    const float *v=type<6?(sign?byte_s:byte_u):(sign?short_s:short_u);unsigned count=type==6?2:4;
    for(unsigned c=0;c<count;c++){unsigned k=type==5?(c<3?2-c:3):c;float x=(type<6?words!=0:k/2<words)?v[k]:0;
        if(norm&&sign&&method==2)x=(2*x+1)/(type<6?255.0f:65535.0f);else if(norm){x/=type<6?(sign?127.0f:255.0f):(sign?32767.0f:65535.0f);if(method==1&&x<-1)x=-1;}out[c]=x;}
}
int main(int argc,char **argv){
    setenv("OPTICS_BYPASS_DST","1",1);   /* these cases route streams by their PVS input field */
    (void)argv;unsigned raw=argc==1;OpticsR300State *state=optics_r300_state_create(1);assert(state);
    wr(state,0x43e0,1440|1440u<<13);wr(state,0x43e4,1503|1503u<<13);wr(state,0x2090,1);wr(state,0x4e38,6u<<21|64);
    uint32_t fs[]={0,0,0,0,0,0,0x400000,0,7u<<26,1u<<24,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    unsigned head[]={0x4600,0x4604,0x4608,0x4610,0x4614,0x4618,0x461c,0x4620,0x46c0,0x47c0,0x48c0,0x49c0};for(unsigned i=0;i<12;i++)wr(state,head[i],fs[i]);
    unsigned cases=0;
    for(unsigned type=0;type<8;type++)for(unsigned swap=0;swap<4;swap++)for(unsigned sign=0;sign<2;sign++)for(unsigned norm=0;norm<2;norm++)for(unsigned method=0;method<4;method++)for(unsigned immd=0;immd<2;immd++)for(unsigned partial=0;partial<2;partial++){
        unsigned width=type<4?type+1:type==7?2:1,components=partial?width-1:width;
        uint32_t words[4]={0x3f800000,0xc0000000,0x3e800000,0x80000000};if(type>=4){words[0]=type<6?0xff01807f:0x80007fff;words[1]=0xffff0001;}
        memset(memory,0xcc,sizeof memory);unsigned stride=width*4+12;limit=2*stride+components*4;reads=0;
        for(unsigned v=0;v<3;v++)for(unsigned c=0;c<components;c++)store(memory+v*stride+4*c,words[c],swap);
        uint16_t select=0xb000|1u|0u<<3|5u<<6|2u<<9;
        wr(state,0x2140,1u<<8|swap);wr(state,0x2150,type|3u<<8|1u<<13|sign<<14|norm<<15);wr(state,0x21e0,select);wr(state,0x21dc,method);wr(state,0x20b4,width+3);
        OpticsAos aos={.count=1,.array={{components,stride/4,0x4000}}};uint32_t p[1+3*7]={4u|(immd?3u:2u)<<4|3u<<16};
        if(immd)for(unsigned v=0;v<3;v++)for(unsigned c=0;c<width;c++)p[1+v*(width+3)+c]=c<components?words[c]:0;
        OpticsDraw d;const char *why=NULL;int ok=optics_draw_build(&d,reg,state,immd?0x35:0x34,p,immd?1+3*(width+3):1,&aos,fetch,NULL,&why);
        if(type>=4&&sign&&norm&&(method==3||(method==2&&(!getenv("OPTICS_VAP_NORM2")||!atoi(getenv("OPTICS_VAP_NORM2")))))){assert(!ok&&!d.vertex&&!d.raw_vertex);continue;}
        assert(ok);assert(!!d.raw_vertex==raw&&!!d.vertex==!raw);assert(!raw||d.raw_general);
        memset(memory,0,sizeof memory);memset(p,0,sizeof p);float value[4];wanted(type,sign,norm,method,components,value);
        for(unsigned v=0;v<3;v++){float in[16][4];optics_draw_input(&d,v,in);
            for(unsigned k=0;k<16;k++)if(k!=3)assert(in[k][0]==0&&in[k][1]==0&&in[k][2]==0&&in[k][3]==1);
            assert(!memcmp(&in[3][0],&value[1],4)&&!memcmp(&in[3][1],&value[0],4)&&in[3][2]==0&&!memcmp(&in[3][3],&value[2],4));}
        optics_draw_free(&d);assert(!d.vertex&&!d.raw_vertex&&!d.index);optics_draw_free(&d);cases++;
    }
    /* Independent streams, a stride-zero constant, and duplicate destination
     * masks must agree with the normalized fallback. */
    for(unsigned duplicate=0;duplicate<2;duplicate++){
        memset(memory,0,sizeof memory);limit=sizeof memory;
        for(unsigned v=0;v<3;v++){store(memory+v*16,0x3f800000,2);store(memory+v*16+4,0xc0000000,2);}
        store(memory+256,0xff01807f,2);
        wr(state,0x2140,1u<<8|2);wr(state,0x2150,1u|((0x8004u|(duplicate?0:4)<<8|1u<<13)<<16));
        wr(state,0x21e0,0xf688u|((duplicate?0x4000u|2u<<6:0xf688u)<<16));wr(state,0x21dc,0);
        OpticsAos aos={.count=2,.array={{2,4,0x4000},{1,0,0x4100}}};uint32_t packet[]={4|2u<<4|3u<<16};
        OpticsDraw d;const char *why=NULL;assert(optics_draw_build(&d,reg,state,0x34,packet,1,&aos,fetch,NULL,&why));
        assert(!!d.raw_vertex==(raw&&!duplicate));memset(memory,0,sizeof memory);
        for(unsigned v=0;v<3;v++){float in[16][4];optics_draw_input(&d,v,in);assert(in[0][0]==1&&in[0][1]==-2&&in[0][3]==1);
            if(duplicate)assert(in[0][2]==1/255.0f);else assert(in[4][0]==127/255.0f&&in[4][1]==128/255.0f&&in[4][2]==1/255.0f&&in[4][3]==1);}
        optics_draw_free(&d);
    }
    /* A coalesced range can be inaccessible although every live attribute is
     * readable. A failed speculative fetch must fall back without stale offsets. */
    hole=1;memset(memory,0,sizeof memory);limit=sizeof memory;store(memory,0x3f800000,2);store(memory+8,0xc0000000,2);
    wr(state,0x2140,1u<<8|2);wr(state,0x2150,0u|((0u|1u<<8|1u<<13)<<16));wr(state,0x21e0,0xf688f688u);
    OpticsAos aos={.count=2,.array={{1,4,0x4000},{1,4,0x4008}}};uint32_t packet[]={4|1u<<4|3u<<16,0,0};OpticsDraw d;const char *why=NULL;
    assert(optics_draw_build(&d,reg,state,0x36,packet,3,&aos,fetch,NULL,&why));assert(!!d.raw_vertex==raw);
    float in[16][4];optics_draw_input(&d,0,in);assert(in[0][0]==1&&in[1][0]==-2);optics_draw_free(&d);hole=0;
    /* An actually invalid source must refuse and release partial snapshots. */
    aos.array[1].offset=0x5000;assert(!optics_draw_build(&d,reg,state,0x36,packet,3,&aos,fetch,NULL,&why));assert(!d.raw_vertex&&!d.vertex&&!d.index);
    /* Large immediate packet padding remains owned, not a dangling PM4 pointer. */
    {uint32_t packet[1+3*127]={4|3u<<4|3u<<16};for(unsigned v=0;v<3;v++)packet[1+127*v]=0x3f800000;
     wr(state,0x2150,1u<<13);wr(state,0x21e0,0xf688);wr(state,0x20b4,127);
     assert(optics_draw_build(&d,reg,state,0x35,packet,1+3*127,NULL,NULL,NULL,&why));assert(!raw||d.raw_stride==508);
     memset(packet,0,sizeof packet);for(unsigned v=0;v<3;v++){optics_draw_input(&d,v,in);assert(in[0][0]==1&&in[0][3]==1);}optics_draw_free(&d);}
    optics_r300_state_destroy(state);printf("PASS: %u VAP cases: all supported types, four byte permutations, signed/unsigned normalization, partial arrays, masks/swizzles, gaps, immutable immediate/array input; raw=%u\n",cases,raw);
}
