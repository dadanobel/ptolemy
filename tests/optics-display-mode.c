/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The display NDRV's mode switch and colour table (hypercall ops 3 and 4) reach the scanout: geometry,
 * 64-pixel pitch (the boot mode keeps its own), depth (16 bits published as 1555), the framebuffer's aperture and its byte
 * lanes, palette; bad requests are refused and change nothing. 8-bit pixels written through the returned aperture after
 * Tiger's driver sets SURFACE_CNTL 0x00200001 (observed after every switch) reach the scanout in order. */
#include "frontend.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static uint8_t out[1u<<23];
static void scanout(PtRadeon *d,uint32_t ew,uint32_t eh,uint32_t ebpp,uint32_t eline,unsigned swap) {
    uint32_t w,h,bpp,line; assert(optics_frontend_scanout(d,out,sizeof out,&w,&h,&bpp,&line));
    assert(w==ew && h==eh && bpp==ebpp && line==eline && optics_frontend_cpu_view_xor(d)==swap);
}
static void frame(PtRadeon *d,uint64_t ns,uint32_t w,uint32_t bpp) {
    PtRadeonFrame f; pt_radeon_service(d,ns); assert(pt_radeon_frame_acquire(d,&f));
    assert(f.width==w && f.bpp==bpp); if(bpp==8) assert(f.palette[254][0]==0x12 && f.palette[254][2]==0x56 && f.palette[255][1]==0xcd);
    pt_radeon_frame_release(d,&f);
}
static void mmio(PtRadeon *d,unsigned a,uint32_t v){ uint8_t b[4]; for(unsigned i=0;i<4;i++) b[i]=(uint8_t)(v>>(8*i)); assert(!pt_radeon_mmio(d,a,b,4,1)); }
int main(void) {
    static uint8_t ram[4096]; PtRadeonConfig c={.ram={ram,sizeof ram,NULL},.width=1000,.height=700}; PtRadeon *d;
    assert(!pt_radeon_create(&d,&c));
    scanout(d,1000,700,32,4000,3);                                   /* boot console */
    uint32_t fb=1; const uint32_t ap1=128u<<20;
    assert(!pt_radeon_display_mode(d,800,600,8,&fb) && fb==ap1); scanout(d,800,600,8,832,0);
    mmio(d,0x0b00,0x00200001); assert(!pt_radeon_vram(d,fb+832,(uint8_t *)"\x01\x02\x03\x04",4,1));
    scanout(d,800,600,8,832,0); assert(out[800]==1 && out[801]==2 && out[802]==3 && out[803]==4);
    const uint32_t rgb[2]={0x123456,0xabcdef}; assert(!pt_radeon_palette(d,254,2,rgb)); frame(d,20000000,800,8);
    assert(!pt_radeon_display_mode(d,640,480,16,&fb) && fb==ap1); scanout(d,640,480,16,1280,1); frame(d,40000000,640,15);
    assert(!pt_radeon_display_mode(d,1000,700,16,NULL)); scanout(d,1000,700,16,2048,1);
    assert(!pt_radeon_display_mode(d,1000,700,32,&fb) && !fb); scanout(d,1000,700,32,4000,3); frame(d,60000000,1000,32);
    assert(pt_radeon_display_mode(d,1000,700,24,NULL)==EINVAL && pt_radeon_display_mode(d,1004,700,32,NULL)==EINVAL &&
           pt_radeon_display_mode(d,4096,700,8,NULL)==EINVAL && pt_radeon_palette(d,255,2,rgb)==EINVAL);
    scanout(d,1000,700,32,4000,3);
    pt_radeon_close(d); puts("PASS: display modes 8/16/32 bpp, pitch, framebuffer aperture and swap, colour table; bad requests refused");
    return 0;
}
