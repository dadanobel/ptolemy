/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_BOOT_MODE_H
#define PT_BOOT_MODE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {const char *name;unsigned width,height;uint8_t dtd[15];} PtBootMode;
/* One strict catalogue for fw_cfg AND the Radeon EDID. A sscanf prefix match
 * would let the GPU and firmware choose different modes for a malformed env.
 * DTD fields are pixel clock/active/blanking/sync/physical size; borders/flags
 * are set by the EDID builder. 2560x1440 uses 241.50 MHz, totals 2720 x 1481.
 */
/* VESA CVT reduced blanking v1 at 60 Hz; reproduces the four catalogue DTDs above
 * except their physical size (checked by tests/boot-mode.c). Size here: ~100 dpi. */
static inline unsigned pt_cvt_rb(unsigned w,unsigned h,uint8_t d[15]){
    unsigned vsync=h*4==w*3?4:h*16==w*9?5:h*16==w*10?6:h*5==w*4||h*15==w*9?7:10;
    double period=(1e6/60-460)/h;unsigned vblank=(unsigned)(460/period)+1;
    if(vblank<3+vsync+6)vblank=3+vsync+6;
    unsigned vt=h+vblank,clock=(unsigned)(60.0*vt*(w+160)/250000)*25; /* 10 kHz units, 0.25 MHz steps */
    unsigned mw=w/4,mh=h/4;
    const uint8_t v[15]={clock&255,clock>>8,w&255,160,(uint8_t)((w>>8)<<4),h&255,vblank&255,(uint8_t)(((h>>8)<<4)|(vblank>>8)),
        48,32,(uint8_t)(0x30|vsync),0,mw&255,mh&255,(uint8_t)(((mw>>8)<<4)|(mh>>8))};
    memcpy(d,v,15);return 60*vt; /* horizontal rate, Hz */
}
static inline const PtBootMode *pt_boot_mode(const char *name){
    static const PtBootMode modes[]={
        {"1920x1080",1920,1080,{0x1a,0x36,0x80,0xa0,0x70,0x38,0x1f,0x40,0x30,0x20,0x35,0x00,0x13,0x2b,0x21}},
        {"1920x1200",1920,1200,{0x28,0x3c,0x80,0xa0,0x70,0xb0,0x23,0x40,0x30,0x20,0x36,0x00,0x06,0x44,0x21}},
        {"2560x1440",2560,1440,{0x56,0x5e,0x00,0xa0,0xa0,0xa0,0x29,0x50,0x30,0x20,0x35,0x00,0x81,0x56,0x21}},
        {"2560x1600",2560,1600,{0xe2,0x68,0x00,0xa0,0xa0,0x40,0x2e,0x60,0x30,0x20,0x36,0x00,0x81,0x90,0x21}}
    };
    if(!name)return NULL;
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++)if(!strcmp(name,modes[i].name))return &modes[i];
    /* Any other strict "WxH" (e.g. the host's full-screen size, see stand_window_macos.m):
     * width in 8-pixel CRTC units, horizontal rate within the EDID range limit (81 kHz). */
    static PtBootMode custom;unsigned w,h;char tail;
    if(sscanf(name,"%ux%u%c",&w,&h,&tail)!=2||w<640||w>2560||w%8||h<480||h>1600||pt_cvt_rb(w,h,custom.dtd)>81000)return NULL;
    custom.name=name;custom.width=w;custom.height=h;return &custom;
}
#endif
