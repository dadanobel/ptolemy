/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Colour formats other than ARGB8888 in the product Metal executor (COLORFORMAT 3 ARGB1555, 4 RGB565, 9 I8, 13 UV88,
 * 15 ARGB4444), and ARGB8888 with the output selects R, G, B, A (US_OUT_FMT_0 0x39xx, an observed value). A 16x16 target
 * filled with a byte pattern is imported, its left half drawn with one colour: after the write-back the left half holds
 * that colour packed in the format (I8 from C2, UV88 from C0 and C1 of the default output selects B, G, R, A) and the
 * right half every original byte (decode and encode round-trip). */
#include "frontend.h"
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
enum { C0=0x100000, TEX=0x200000, N=16 };
static unsigned q(unsigned x,unsigned max) { return (x*max+127)/255; }
int main(void) {
    static PtR2ShaderIR pvs,us; static OpticsDraw dr; static float vert[3][OPTICS_DRAW_INPUTS][4]; static uint32_t idx[3]={0,1,2};
    uint32_t fs[]={8,0,0,0,0,0,0x400000,(1u<<15)|(1u<<6),(7u<<26)|1u,(1u<<24)|1u,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};   /* as optics-mip-levels */
    PtR2ShaderError e; assert(optics_draw_passthrough(2,&pvs) && pt_r2_shader_decode(PT_R2_SHADER_US,PT_R2_SHADER_FP32_FTZ0,fs,12,0,&us,&e)==PT_R2_SHADER_OK);
    dr.pvs=&pvs; dr.us=&us; dr.pvs_id=1; dr.us_id=0x313;
    dr.vi.abi=dr.fi.abi=PT_R2_SHADER_ABI; dr.vi.input_count=OPTICS_DRAW_INPUTS; dr.vi.output_count=2; dr.vi.constant_count=256;
    dr.fi.input_count=1; dr.fi.output_count=4; dr.fi.constant_count=32; dr.fi.texture_mask=1;
    dr.width=dr.height=N; dr.cb_pitch=N; dr.scissor[2]=N/2; dr.scissor[3]=N; dr.vte=0x300;
    dr.inputs=OPTICS_DRAW_INPUTS; dr.vertices=3; dr.vertex=vert; dr.indices=3; dr.index=idx;
    const float pos[3][2]={{0,0},{2*N,0},{0,2*N}};
    for(int v=0;v<3;v++){ vert[v][0][0]=pos[v][0]; vert[v][0][1]=pos[v][1]; vert[v][0][3]=1; vert[v][1][3]=1; }
    dr.position_output=0; dr.color_output=0; dr.varyings=1; dr.pvs_output[0]=1; dr.us_input[0]=0;
    for(int c=0;c<4;c++) dr.component[0][c]=(uint8_t)c;
    dr.color_mask=15; dr.texture_mask=1;
    OpticsTexture *t=&dr.texture[0]; t->offset=TEX; t->width=t->height=t->pitch=1; t->format=0xc; t->layout=OPTICS_PIXEL_RGBA8; t->bpp=4; t->levels=1; t->sampler=0x12;
    for(int c=0;c<4;c++) t->select[c]=(uint8_t)c;
    const OpticsExecutor *m=optics_metal_executor(); optics_frontend_set_executor(NULL);
    static uint8_t ram[1<<20]; PtRadeonConfig c={.ram={ram,sizeof ram,NULL}}; PtRadeon *d; assert(!pt_radeon_create(&d,&c));
    void *x=m->create(d); assert(x);
    uint8_t rgba[4]={0x80,0x40,0xc0,0xff}; assert(!pt_radeon_vram(d,TEX,rgba,4,1));
    static const uint8_t formats[6]={3,4,9,13,15,6}; int bad=0;
    for(unsigned k=0;k<6;k++) {
        unsigned f=formats[k],bpp=f==9?1:f==6?4:2,v=0; uint8_t want[4];
        if(f==3) v=1u<<15|q(0x80,31)<<10|q(0x40,31)<<5|q(0xc0,31);
        if(f==4) v=q(0x80,31)<<11|q(0x40,63)<<5|q(0xc0,31);
        if(f==15) v=q(0xff,15)<<12|q(0x80,15)<<8|q(0x40,15)<<4|q(0xc0,15);
        want[0]=(uint8_t)v; want[1]=(uint8_t)(v>>8);
        if(f==9) want[0]=0x80;                         /* C2 = R */
        if(f==13){ want[0]=0xc0; want[1]=0x40; }       /* C0 = B, C1 = G */
        if(f==6){ want[0]=0x80; want[1]=0x40; want[2]=0xc0; want[3]=0xff; }   /* selects 0x39: R, G, B, A */
        static uint8_t before[N*N*4],after[N*N*4];
        for(unsigned i=0;i<sizeof before;i++) before[i]=(uint8_t)(i*37+k*11+5);
        dr.cb_offset=C0+0x10000*k; dr.cb_format=(uint8_t)f; dr.cb_bpp=(uint8_t)bpp; dr.cb_select=f==6?0x39:0;
        assert(!pt_radeon_vram(d,dr.cb_offset,before,N*N*bpp,1));
        const char *why=NULL; int rc=m->draw(x,d,&dr,&why);
        if(rc!=OPTICS_EXEC_DONE){ fprintf(stderr,"format %u: draw %d %s\n",f,rc,why?why:""); assert(0); }
        for(int i=0;m->fence(x,d)!=OPTICS_FENCE_DONE;i++){ assert(i<5000); usleep(1000); }
        assert(!m->materialize(x,d,dr.cb_offset,dr.cb_offset+N*N*bpp,0));
        assert(!pt_radeon_vram(d,dr.cb_offset,after,N*N*bpp,0));
        for(unsigned y=0;y<N;y++) for(unsigned i=0;i<N;i++) {
            const uint8_t *g=after+(y*N+i)*bpp,*w=i<N/2?want:before+(y*N+i)*bpp;
            if(memcmp(g,w,bpp)) { if(bad++<8) fprintf(stderr,"format %u pixel %u,%u: %02x%02x.., want %02x%02x.. (%s)\n",f,i,y,g[0],bpp>1?g[1]:0,w[0],bpp>1?w[1]:0,i<N/2?"drawn":"kept"); break; }
        }
    }
    assert(!bad);
    m->destroy(x,d); pt_radeon_close(d);
    puts("PASS: colour formats ARGB1555, RGB565, I8, UV88, ARGB4444, ARGB8888 with selects R,G,B,A: drawn half packed, other half's bytes kept"); return 0;
}
