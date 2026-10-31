/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Directed test of independent depth/stencil storage in the product Metal executor: depth belongs to
 * the ZB buffer, not to the colour target. C0/Z0 -> C1/Z0 -> C0/Z1 -> C0/Z0 -> C0/Z1 with LESS tests and no clear
 * between them; then a CPU write over Z0 drops its GPU copy (depth is not imported: fresh storage starts at 1.0). */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
static const OpticsExecutor *m;
enum { C0=0x100000, C1=0x200000, Z0=0x300000, Z1=0x400000, N=64, LESS=1 };
static PtR2ShaderIR pvs,us; static OpticsDraw dr; static float vert[3][OPTICS_DRAW_INPUTS][4]; static uint32_t idx[3]={0,1,2};
static void draw(void *x,PtRadeon *d,uint32_t cb,uint32_t zb,float z,float r,float g,float b) {
    dr.cb_offset=cb; dr.zb_offset=zb;
    for(int v=0;v<3;v++) vert[v][0][2]=z;
    dr.us_constant[0][0]=r; dr.us_constant[0][1]=g; dr.us_constant[0][2]=b; dr.us_constant[0][3]=1;
    const char *why=NULL; int rc=m->draw(x,d,&dr,&why);
    if(rc!=OPTICS_EXEC_DONE){ fprintf(stderr,"draw %#x/%#x: %d %s\n",cb,zb,rc,why?why:""); assert(0); }
}
static uint32_t pixel(void *x,PtRadeon *d,uint32_t cb) {   /* centre pixel after completion and export, 0xAARRGGBB */
    for(int i=0;m->fence(x,d)!=OPTICS_FENCE_DONE;i++){ assert(i<5000); usleep(1000); }
    assert(!m->materialize(x,d,cb,cb+N*N*4,0));
    uint8_t p[4]; assert(!pt_radeon_vram(d,cb+(N/2*N+N/2)*4,p,4,0)); return p[0]|p[1]<<8|p[2]<<16|(uint32_t)p[3]<<24;
}
int main(void) {
    uint32_t fs[]={0,0,0,0,0,0,0x400000,0,(7u<<26)|32u,(1u<<24)|32u,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};   /* colour = constant 0 */
    PtR2ShaderError e; assert(optics_draw_passthrough(2,&pvs) && pt_r2_shader_decode(PT_R2_SHADER_US,PT_R2_SHADER_FP32_FTZ0,fs,12,0,&us,&e)==PT_R2_SHADER_OK);
    dr.pvs=&pvs; dr.us=&us; dr.pvs_id=1; dr.us_id=0x5e57;   /* pvs_id 1: CPU vertex stage, window coordinates as given */
    dr.vi.abi=dr.fi.abi=PT_R2_SHADER_ABI; dr.vi.input_count=OPTICS_DRAW_INPUTS; dr.vi.output_count=2; dr.vi.constant_count=256;
    dr.fi.input_count=1; dr.fi.output_count=4; dr.fi.constant_count=32;
    dr.width=dr.height=N; dr.cb_pitch=N; dr.scissor[2]=dr.scissor[3]=N; dr.vte=0x300;   /* no divide, no viewport */
    dr.inputs=OPTICS_DRAW_INPUTS; dr.vertices=3; dr.vertex=vert; dr.indices=3; dr.index=idx;
    const float pos[3][2]={{0,0},{2*N,0},{0,2*N}};
    for(int v=0;v<3;v++){ vert[v][0][0]=pos[v][0]; vert[v][0][1]=pos[v][1]; vert[v][0][3]=1; vert[v][1][3]=1; }
    dr.position_output=0; dr.color_output=0; dr.varyings=1; dr.pvs_output[0]=1; dr.us_input[0]=0;
    for(int c=0;c<4;c++) dr.component[0][c]=(uint8_t)c;
    dr.color_mask=15; dr.depth_enable=1; dr.depth_write=1; dr.depth_compare=LESS;
    dr.depth_clear_word=0xffffff00u; dr.depth_clear_format=2;   /* D24S8: depth 1.0, stencil 0 */
    m=optics_metal_executor(); optics_frontend_set_executor(NULL);   /* the device gets no instance; this test owns one */
    static uint8_t ram[1<<20]; PtRadeonConfig c={.ram={ram,sizeof ram,NULL}}; PtRadeon *d; assert(!pt_radeon_create(&d,&c));
    uint8_t pitch[4]={N,0,0,0}; assert(!pt_radeon_mmio(d,0x4f24,pitch,4,1));   /* ZB_DEPTHPITCH */
    void *x=m->create(d); assert(x);
    assert(!m->clear(x,d,Z0,2) && !m->clear(x,d,Z1,2));
    draw(x,d,C0,Z0,0.5f,1,0,0); assert(pixel(x,d,C0)==0xffff0000u);   /* Z0 = 0.5 */
    draw(x,d,C1,Z0,0.7f,0,1,0); assert(pixel(x,d,C1)==0);             /* Z0 shared with C0: 0.7 fails */
    draw(x,d,C0,Z1,0.7f,0,0,1); assert(pixel(x,d,C0)==0xff0000ffu);   /* Z1 independent (1.0): passes, Z1 = 0.7 */
    draw(x,d,C0,Z0,0.6f,1,1,1); assert(pixel(x,d,C0)==0xff0000ffu);   /* Z0 kept 0.5: 0.6 fails */
    draw(x,d,C0,Z1,0.65f,1,1,0); assert(pixel(x,d,C0)==0xffffff00u);  /* Z1 kept 0.7: 0.65 passes */
    assert(!m->materialize(x,d,Z0,Z0+4,1));                            /* CPU write over Z0: its GPU copy is dropped */
    draw(x,d,C1,Z0,0.9f,0,1,1); assert(pixel(x,d,C1)==0xff00ffffu);   /* fresh Z0 storage at 1.0 */
    if(getenv("OPTICS_DEPTH_IO")){
        /* A tiled ZB_DEPTHPITCH layout with OPTICS_DEPTH_IO keeps the GPU-only depth path: the draw is not refused. */
        uint8_t tiled[4]={N,0,1,0};assert(!pt_radeon_mmio(d,0x4f24,tiled,4,1));
        draw(x,d,C0,Z0,0.1f,1,0,1);assert(pixel(x,d,C0)==0xffff00ffu);
    }
    m->destroy(x,d); pt_radeon_close(d);
    puts("optics-depth-metal: ok"); return 0;
}
