/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mip levels in the product Metal executor. A 64x64 RGBA8 texture with seven levels in VRAM, in the layout Tiger's
 * driver uses (observed: levels one after another from the largest, rows of exactly width * 4 bytes), each level one
 * colour; a draw that steps 1, 16 or 64 texels per pixel samples level 0, 4 or 6 (the 1x1 level, whose offset differs
 * from a layout with rows padded to 32 bytes); the first level field (MAX_MIP_LEVEL) starts at level 2; a texture
 * without levels samples level 0 at any step. */
#include "frontend.h"
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static const OpticsExecutor *m;
enum { C0=0x100000, TEX=0x200000, N=64, LEVELS=7 };
static PtR2ShaderIR pvs,us; static OpticsDraw dr; static float vert[3][OPTICS_DRAW_INPUTS][4]; static uint32_t idx[3]={0,1,2};
static uint32_t colour(unsigned k) { return 0xff000000u|(uint32_t)(10+30*k)<<16|(uint32_t)(255-30*k)<<8|(53*k&255); }   /* 0xAARRGGBB */
static uint32_t sample(void *x,PtRadeon *d,float step,unsigned levels,unsigned first) {
    const float pos[3][2]={{0,0},{2*N,0},{0,2*N}};
    for(int v=0;v<3;v++){ vert[v][1][0]=pos[v][0]/N*step; vert[v][1][1]=pos[v][1]/N*step; }
    dr.texture[0].levels=(uint8_t)levels; dr.texture[0].sampler=0x12|(levels>1?1u<<8:0)|(uint32_t)first<<13;   /* repeat, point, nearest level */
    const char *why=NULL; int rc=m->draw(x,d,&dr,&why);
    if(rc!=OPTICS_EXEC_DONE){ fprintf(stderr,"draw: %d %s\n",rc,why?why:""); assert(0); }
    for(int i=0;m->fence(x,d)!=OPTICS_FENCE_DONE;i++){ assert(i<5000); usleep(1000); }
    assert(!m->materialize(x,d,C0,C0+N*N*4,0));
    uint8_t p[4]; assert(!pt_radeon_vram(d,C0+(N/2*N+N/2)*4,p,4,0)); return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;
}
int main(void) {
    /* US: TEX LD unit 0 from temp 0 (the varying) into temp 1, then out.rgba = temp 1 (MAD temp1 * 1 + 0). */
    uint32_t fs[]={8,0,0,0,0,0,0x400000,(1u<<15)|(1u<<6),(7u<<26)|1u,(1u<<24)|1u,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    PtR2ShaderError e; assert(optics_draw_passthrough(2,&pvs) && pt_r2_shader_decode(PT_R2_SHADER_US,PT_R2_SHADER_FP32_FTZ0,fs,12,0,&us,&e)==PT_R2_SHADER_OK);
    assert(us.textures==1);
    dr.pvs=&pvs; dr.us=&us; dr.pvs_id=1; dr.us_id=0x313;   /* pvs_id 1: CPU vertex stage, window coordinates as given */
    dr.vi.abi=dr.fi.abi=PT_R2_SHADER_ABI; dr.vi.input_count=OPTICS_DRAW_INPUTS; dr.vi.output_count=2; dr.vi.constant_count=256;
    dr.fi.input_count=1; dr.fi.output_count=4; dr.fi.constant_count=32; dr.fi.texture_mask=1;
    dr.width=dr.height=N; dr.cb_pitch=N; dr.scissor[2]=dr.scissor[3]=N; dr.vte=0x300;
    dr.inputs=OPTICS_DRAW_INPUTS; dr.vertices=3; dr.vertex=vert; dr.indices=3; dr.index=idx;
    const float pos[3][2]={{0,0},{2*N,0},{0,2*N}};
    for(int v=0;v<3;v++){ vert[v][0][0]=pos[v][0]; vert[v][0][1]=pos[v][1]; vert[v][0][3]=1; vert[v][1][3]=1; }
    dr.position_output=0; dr.color_output=0; dr.varyings=1; dr.pvs_output[0]=1; dr.us_input[0]=0;
    for(int c=0;c<4;c++) dr.component[0][c]=(uint8_t)c;
    dr.cb_offset=C0; dr.color_mask=15; dr.texture_mask=1;
    OpticsTexture *t=&dr.texture[0]; t->offset=TEX; t->width=t->height=t->pitch=N; t->format=0xc; t->layout=OPTICS_PIXEL_RGBA8; t->bpp=4;
    for(int c=0;c<4;c++) t->select[c]=(uint8_t)c;
    m=optics_metal_executor(); optics_frontend_set_executor(NULL);
    static uint8_t ram[1<<20]; PtRadeonConfig c={.ram={ram,sizeof ram,NULL}}; PtRadeon *d; assert(!pt_radeon_create(&d,&c));
    void *x=m->create(d); assert(x);
    static uint8_t level[N*N*4]; uint32_t at=TEX;
    for(unsigned k=0;k<LEVELS;k++) {   /* bytes R, G, B, A of each texel */
        unsigned w=N>>k,n=w*w*4; uint32_t col=colour(k);
        for(unsigned i=0;i<n;i+=4){ level[i]=(uint8_t)(col>>16); level[i+1]=(uint8_t)(col>>8); level[i+2]=(uint8_t)col; level[i+3]=255; }
        assert(!pt_radeon_vram(d,at,level,n,1)); at+=n;
    }
    static const struct { float step; unsigned levels,first,expect; } cases[]={
        {1,LEVELS,0,0},{16,LEVELS,0,4},{64,LEVELS,0,6},   /* the 1x1 level at TEX + 21840 (rows padded to 32 bytes: + 21888) */
        {1,LEVELS,2,2},                                    /* MAX_MIP_LEVEL 2 */
        {16,1,0,0}};                                       /* no levels: level 0 */
    int bad=0;
    for(unsigned i=0;i<sizeof cases/sizeof *cases;i++) {
        uint32_t got=sample(x,d,cases[i].step,cases[i].levels,cases[i].first),want=colour(cases[i].expect);
        if(got!=want){ fprintf(stderr,"case %u (step %g, levels %u, first %u): %08x, want level %u %08x\n",i,cases[i].step,cases[i].levels,cases[i].first,got,cases[i].expect,want); bad++; }
    }
    if(bad) for(unsigned k=0;k<LEVELS;k++) fprintf(stderr,"level %u = %08x\n",k,colour(k));
    assert(!bad);
    m->destroy(x,d); pt_radeon_close(d);
    puts("PASS: mip levels (Tiger layout), nearest level by texel step, first level, no levels"); return 0;
}
