/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Line lists and the constant blend colour in the product Metal executor. A one-pixel line across row 8 of
 * a 16x16 target colours that row only; then a triangle over the target with blend CONST_COLOR x source + ZERO x
 * destination (RB3D_BLEND_COLOR 0xff4080ff, ARGB) leaves the blend colour in every pixel. */
#include "frontend.h"
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
enum { C0=0x100000, TEX=0x200000, N=16 };
static const OpticsExecutor *m;
static void run(void *x,PtRadeon *d,OpticsDraw *dr,uint8_t out[N*N*4]) {
    const char *why=NULL; int rc=m->draw(x,d,dr,&why);
    if(rc!=OPTICS_EXEC_DONE){ fprintf(stderr,"draw: %d %s\n",rc,why?why:""); assert(0); }
    for(int i=0;m->fence(x,d)!=OPTICS_FENCE_DONE;i++){ assert(i<5000); usleep(1000); }
    assert(!m->materialize(x,d,C0,C0+N*N*4,0)); assert(!pt_radeon_vram(d,C0,out,N*N*4,0));
}
int main(void) {
    static PtR2ShaderIR pvs,us; static OpticsDraw dr; static float vert[3][OPTICS_DRAW_INPUTS][4]; static uint32_t idx[3]={0,1,2};
    uint32_t fs[]={8,0,0,0,0,0,0x400000,(1u<<15)|(1u<<6),(7u<<26)|1u,(1u<<24)|1u,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};   /* as optics-mip-levels */
    PtR2ShaderError e; assert(optics_draw_passthrough(2,&pvs) && pt_r2_shader_decode(PT_R2_SHADER_US,PT_R2_SHADER_FP32_FTZ0,fs,12,0,&us,&e)==PT_R2_SHADER_OK);
    dr.pvs=&pvs; dr.us=&us; dr.pvs_id=1; dr.us_id=0x313;
    dr.vi.abi=dr.fi.abi=PT_R2_SHADER_ABI; dr.vi.input_count=OPTICS_DRAW_INPUTS; dr.vi.output_count=2; dr.vi.constant_count=256;
    dr.fi.input_count=1; dr.fi.output_count=4; dr.fi.constant_count=32; dr.fi.texture_mask=1;
    dr.width=dr.height=N; dr.cb_pitch=N; dr.scissor[2]=dr.scissor[3]=N; dr.vte=0x300;
    dr.inputs=OPTICS_DRAW_INPUTS; dr.vertex=vert; dr.index=idx;
    dr.position_output=0; dr.color_output=0; dr.varyings=1; dr.pvs_output[0]=1; dr.us_input[0]=0;
    for(int c=0;c<4;c++) dr.component[0][c]=(uint8_t)c;
    dr.cb_offset=C0; dr.color_mask=15; dr.texture_mask=1;
    OpticsTexture *t=&dr.texture[0]; t->offset=TEX; t->width=t->height=t->pitch=1; t->format=0xc; t->layout=OPTICS_PIXEL_RGBA8; t->bpp=4; t->levels=1; t->sampler=0x12;
    for(int c=0;c<4;c++) t->select[c]=(uint8_t)c;
    m=optics_metal_executor(); optics_frontend_set_executor(NULL);
    static uint8_t ram[1<<20]; PtRadeonConfig c={.ram={ram,sizeof ram,NULL}}; PtRadeon *d; assert(!pt_radeon_create(&d,&c));
    void *x=m->create(d); assert(x);
    uint8_t white[4]={255,255,255,255}; assert(!pt_radeon_vram(d,TEX,white,4,1));
    static uint8_t zero[N*N*4],out[N*N*4]; assert(!pt_radeon_vram(d,C0,zero,sizeof zero,1));
    /* 1. a line from (0, 8.5) to (16, 8.5) */
    vert[0][0][0]=0; vert[1][0][0]=N; vert[0][0][1]=vert[1][0][1]=8.5f;
    for(int v=0;v<3;v++){ vert[v][0][3]=1; vert[v][1][3]=1; }
    dr.vertices=2; dr.indices=2; dr.lines=1; run(x,d,&dr,out);
    int bad=0;
    for(unsigned y=0;y<N;y++) for(unsigned i=1;i<N-1;i++) { int on=out[(y*N+i)*4+2]!=0;
        if(on!=(y==8)) { if(bad++<4) fprintf(stderr,"line: pixel %u,%u R %u\n",i,y,out[(y*N+i)*4+2]); } }
    /* 2. constant blend colour: CONST_COLOR * src + ZERO * dst, alpha the same (Metal factors 11 and 0, operation add) */
    const float pos[3][2]={{0,0},{2*N,0},{0,2*N}};
    for(int v=0;v<3;v++){ vert[v][0][0]=pos[v][0]; vert[v][0][1]=pos[v][1]; }
    dr.vertices=3; dr.indices=3; dr.lines=0; dr.blend=1u<<31|11u|0u<<4|11u<<12|0u<<16; dr.blend_color=0xff4080ffu;
    run(x,d,&dr,out);
    for(unsigned i=0;i<N*N;i++) { const uint8_t *p=out+4*i;   /* B, G, R, A */
        if(abs(p[0]-0xff)>1||abs(p[1]-0x80)>1||abs(p[2]-0x40)>1||abs(p[3]-0xff)>1) { if(bad++<8) fprintf(stderr,"blend: pixel %u: %02x%02x%02x%02x\n",i,p[2],p[1],p[0],p[3]); } }
    assert(!bad);
    m->destroy(x,d); pt_radeon_close(d);
    puts("PASS: a line colours its row only; constant blend colour"); return 0;
}
