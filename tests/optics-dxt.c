/* SPDX-License-Identifier: GPL-2.0-or-later */
/* S3TC textures (TX_FORMAT 0xF-0x11) in the product Metal executor. A 16x16 DXT1, DXT3 and DXT5 texture of
 * pseudo-random blocks, drawn 1:1 with point sampling: every pixel equals pixel.c's reference decode within 2 per channel
 * (DXT1 blocks in both colour modes). Then a DXT1 texture with five levels (rows of blocks, the 4x4, 2x2 and 1x1 levels
 * one block each), each level one colour: a step of 16 texels per pixel samples the 1x1 level. */
#include "frontend.h"
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const OpticsExecutor *m;
enum { C0=0x100000, TEX=0x200000, N=16 };
static PtR2ShaderIR pvs,us; static OpticsDraw dr; static float vert[3][OPTICS_DRAW_INPUTS][4]; static uint32_t idx[3]={0,1,2};
static void draw(void *x,PtRadeon *d,float step,unsigned levels,uint8_t out[N*N*4]) {
    const float pos[3][2]={{0,0},{2*N,0},{0,2*N}};
    for(int v=0;v<3;v++){ vert[v][1][0]=pos[v][0]/N*step; vert[v][1][1]=pos[v][1]/N*step; }
    dr.texture[0].levels=(uint8_t)levels; dr.texture[0].sampler=0x12|(levels>1?1u<<8:0);   /* repeat, point, nearest level */
    const char *why=NULL; int rc=m->draw(x,d,&dr,&why);
    if(rc!=OPTICS_EXEC_DONE){ fprintf(stderr,"draw: %d %s\n",rc,why?why:""); assert(0); }
    for(int i=0;m->fence(x,d)!=OPTICS_FENCE_DONE;i++){ assert(i<5000); usleep(1000); }
    assert(!m->materialize(x,d,C0,C0+N*N*4,0));
    assert(!pt_radeon_vram(d,C0,out,N*N*4,0));
}
int main(void) {
    /* US: TEX LD unit 0 from temp 0 (the varying) into temp 1, then out.rgba = temp 1 (as optics-mip-levels). */
    uint32_t fs[]={8,0,0,0,0,0,0x400000,(1u<<15)|(1u<<6),(7u<<26)|1u,(1u<<24)|1u,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    PtR2ShaderError e; assert(optics_draw_passthrough(2,&pvs) && pt_r2_shader_decode(PT_R2_SHADER_US,PT_R2_SHADER_FP32_FTZ0,fs,12,0,&us,&e)==PT_R2_SHADER_OK);
    dr.pvs=&pvs; dr.us=&us; dr.pvs_id=1; dr.us_id=0x313;
    dr.vi.abi=dr.fi.abi=PT_R2_SHADER_ABI; dr.vi.input_count=OPTICS_DRAW_INPUTS; dr.vi.output_count=2; dr.vi.constant_count=256;
    dr.fi.input_count=1; dr.fi.output_count=4; dr.fi.constant_count=32; dr.fi.texture_mask=1;
    dr.width=dr.height=N; dr.cb_pitch=N; dr.scissor[2]=dr.scissor[3]=N; dr.vte=0x300;
    dr.inputs=OPTICS_DRAW_INPUTS; dr.vertices=3; dr.vertex=vert; dr.indices=3; dr.index=idx;
    const float pos[3][2]={{0,0},{2*N,0},{0,2*N}};
    for(int v=0;v<3;v++){ vert[v][0][0]=pos[v][0]; vert[v][0][1]=pos[v][1]; vert[v][0][3]=1; vert[v][1][3]=1; }
    dr.position_output=0; dr.color_output=0; dr.varyings=1; dr.pvs_output[0]=1; dr.us_input[0]=0;
    for(int c=0;c<4;c++) dr.component[0][c]=(uint8_t)c;
    dr.cb_offset=C0; dr.color_mask=15; dr.texture_mask=1;
    OpticsTexture *t=&dr.texture[0]; t->width=t->height=t->pitch=N;
    for(int c=0;c<4;c++) t->select[c]=(uint8_t)c;
    m=optics_metal_executor(); optics_frontend_set_executor(NULL);
    static uint8_t ram[1<<20]; PtRadeonConfig c={.ram={ram,sizeof ram,NULL}}; PtRadeon *d; assert(!pt_radeon_create(&d,&c));
    void *x=m->create(d); assert(x);
    static uint8_t tex[N*N],out[N*N*4]; int bad=0; unsigned three=0;
    srand(29102026);
    for(unsigned k=0;k<3;k++) {   /* DXT1, DXT3, DXT5 */
        unsigned block=k?16:8,bytes=N/4*N/4*block;
        for(unsigned i=0;i<bytes;i++) tex[i]=(uint8_t)rand();
        if(!k) for(unsigned b=0;b<bytes;b+=8) three+=(tex[b]|tex[b+1]<<8)<=(tex[b+2]|tex[b+3]<<8);
        t->offset=TEX+0x10000*k; t->format=0xf+k; t->layout=OPTICS_PIXEL_DXT1+k; t->bpp=(uint8_t)block;
        assert(!pt_radeon_vram(d,t->offset,tex,bytes,1));
        draw(x,d,1,1,out);
        OpticsPixelLayout l={t->layout,{0,1,2,3}};
        for(unsigned y=0;y<N;y++) for(unsigned i=0;i<N;i++) {
            uint8_t want[4],*p=out+(y*N+i)*4,got[4]={p[2],p[1],p[0],p[3]};   /* ARGB8888 LE -> RGBA */
            assert(optics_pixel_reference_texel(l,tex,N,N,N/4*block,i,y,want));
            for(int q=0;q<4;q++) if(abs(got[q]-want[q])>2) {
                if(bad++<8) fprintf(stderr,"DXT%u texel %u,%u: %02x%02x%02x%02x, want %02x%02x%02x%02x\n",k?2*k+1:1,i,y,got[0],got[1],got[2],got[3],want[0],want[1],want[2],want[3]);
                break;
            }
        }
    }
    assert(three>2 && three<14);   /* both DXT1 colour modes were drawn */
    /* Five DXT1 levels, one colour each: c0 = c1 = an RGB565 value, all indices 0. */
    static const uint16_t level[5]={0xf800,0x07e0,0x001f,0xffe0,0x07ff}; unsigned at=0;
    for(unsigned k=0;k<5;k++) { unsigned w=N>>k,blocks=(w+3)/4*((w+3)/4);
        for(unsigned b=0;b<blocks;b++){ uint8_t *q=tex+at+8*b; q[0]=q[2]=(uint8_t)level[k]; q[1]=q[3]=(uint8_t)(level[k]>>8); memset(q+4,0,4); }
        at+=8*blocks; }
    assert(at==128+32+8+8+8);
    t->offset=TEX+0x40000; t->format=0xf; t->layout=OPTICS_PIXEL_DXT1; t->bpp=8;
    assert(!pt_radeon_vram(d,t->offset,tex,at,1));
    draw(x,d,16,5,out); uint8_t *p=out+(N/2*N+N/2)*4;   /* level 4: 0x07ff, cyan */
    if(p[2]!=0 || p[1]!=255 || p[0]!=255) { fprintf(stderr,"DXT1 level 4: B %u G %u R %u, want cyan\n",p[0],p[1],p[2]); bad++; }
    assert(!bad);
    m->destroy(x,d); pt_radeon_close(d);
    printf("PASS: DXT1/3/5 decode against the reference (%u three-colour DXT1 blocks of 16), DXT1 levels of blocks\n",three); return 0;
}
