/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Allocation accounting catches ownership escape on every injected failure. */
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned outstanding,attempt,fail_at;
static void *checked_malloc(size_t n){if(fail_at&&++attempt==fail_at)return NULL;void *p=malloc(n);if(p)outstanding++;return p;}
static void *checked_calloc(size_t n,size_t z){if(fail_at&&++attempt==fail_at)return NULL;void *p=calloc(n,z);if(p)outstanding++;return p;}
static void checked_free(void *p){if(p){assert(outstanding);outstanding--;}free(p);}
#define malloc checked_malloc
#define calloc checked_calloc
#define free checked_free
#include "../src/optics/draw.c"
#undef malloc
#undef calloc
#undef free
static uint32_t regs[0x10000/4];
static void wr(OpticsR300State *s,unsigned a,uint32_t v){regs[a/4]=v;(void)optics_r300_write(s,a,v);}
int main(void){
    OpticsR300State *s=optics_r300_state_create(1); assert(s);
    wr(s,0x43e0,1440|1440u<<13); wr(s,0x43e4,1503|1503u<<13);
    wr(s,0x2140,1u<<8); wr(s,0x20b4,4); wr(s,0x2150,3u|1u<<13);
    wr(s,0x21e0,0xf000|1u<<3|2u<<6|3u<<9); wr(s,0x2090,1);
    const uint32_t fs[]={0,0,0,0,0,0,0x400000,0,7u<<26,1u<<24,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    const unsigned head[]={0x4600,0x4604,0x4608,0x4610,0x4614,0x4618,0x461c,0x4620,0x46c0,0x47c0,0x48c0,0x49c0};
    for(unsigned k=0;k<12;k++) wr(s,head[k],fs[k]);
    uint32_t payload[13]={4u|3u<<4|3u<<16,0,0,0,0x3f800000,0,0,0,0x3f800000,0,0,0,0x3f800000};

    wr(s,0x4e38,6u<<21|64);OpticsDraw d;const char *why=NULL;
    assert(optics_draw_build(&d,regs,s,0x35,payload,13,NULL,NULL,NULL,&why));optics_draw_free(&d);
    unsigned cached=outstanding;
    /* Failure after both vertex and index allocation: missing position output. */
    wr(s,0x2090,0);
    assert(!optics_draw_build(&d,regs,s,0x35,payload,13,NULL,NULL,NULL,&why));
    assert(!d.vertex&&!d.index&&!d.corner&&outstanding==cached);optics_draw_free(&d);
    wr(s,0x2090,1);
    for(unsigned n=1;n<=2;n++){
        attempt=0;fail_at=n;assert(!optics_draw_build(&d,regs,s,0x35,payload,13,NULL,NULL,NULL,&why));
        assert(!d.vertex&&!d.index&&!d.corner&&outstanding==cached);optics_draw_free(&d);
    }
    fail_at=0;
    /* The same failure with point-sprite corner and expanded vertex allocations. */
    payload[0]=1u|3u<<4|3u<<16;wr(s,0x2090,0);
    assert(!optics_draw_build(&d,regs,s,0x35,payload,13,NULL,NULL,NULL,&why));
    assert(!d.vertex&&!d.index&&!d.corner&&outstanding==cached);
    optics_draw_free(&d);optics_r300_state_destroy(s);
    puts("PASS: one cleanup owner, post-allocation refusal and injected OOM, repeated free safe");return 0;
}
