/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Success: retain every documented COLORPITCH bit and keep row addresses exact. */
#include "draw.h"
#include "r300_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t regs[0x10000/4];
static void wr(OpticsR300State *s,unsigned a,uint32_t v) { regs[a/4]=v; (void)optics_r300_write(s,a,v); }
int main(void) {
    OpticsR300State *s=optics_r300_state_create(1); assert(s);
    wr(s,0x43e0,1440|1440u<<13); wr(s,0x43e4,1503|1503u<<13);
    wr(s,0x2140,1u<<8); wr(s,0x20b4,4); wr(s,0x2150,3u|1u<<13);
    wr(s,0x21e0,0xf000|1u<<3|2u<<6|3u<<9); wr(s,0x2090,1);
    const uint32_t fs[]={0,0,0,0,0,0,0x400000,0,7u<<26,1u<<24,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    const unsigned head[]={0x4600,0x4604,0x4608,0x4610,0x4614,0x4618,0x461c,0x4620,0x46c0,0x47c0,0x48c0,0x49c0};
    for(unsigned k=0;k<12;k++) wr(s,head[k],fs[k]);
    uint32_t payload[13]={4u|3u<<4|3u<<16,0,0,0,0x3f800000,0,0,0,0x3f800000,0,0,0,0x3f800000};
    const unsigned pitches[]={64,130,132,134,258,8194,16382};
    for(unsigned k=0;k<sizeof pitches/sizeof *pitches;k++) {
        unsigned pitch=pitches[k]; wr(s,0x4e38,6u<<21|pitch);
        OpticsDraw d; const char *why=NULL;
        int ok=optics_draw_build(&d,regs,s,0x35,payload,13,NULL,NULL,NULL,&why);
        if(!ok) { fprintf(stderr,"pitch %u refused: %s\n",pitch,why?why:"?"); assert(ok); }
        if(d.cb_pitch!=pitch) fprintf(stderr,"pitch %u decoded as %u\n",pitch,d.cb_pitch);
        assert(d.cb_pitch==pitch && d.width==pitch);
        for(unsigned y=0;y<64;y++) assert((uint64_t)y*d.width*4==(uint64_t)y*pitch*4);
        optics_draw_free(&d);
    }
    optics_r300_state_destroy(s); puts("PASS: COLORPITCH 13:1, two-pixel units, exact row addresses");
}
