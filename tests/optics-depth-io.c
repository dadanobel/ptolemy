/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A byte read exports one dirty version; partial CPU writes preserve neighbours. */
#define main legacy_depth_contract
#include "optics-depth-metal.c"
#undef main
#include "pixel.h"
#include "io.h"
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

    void *saved=dr.vertex;
    for(unsigned format=1;format<=2;format++) {
        static uint8_t ram[1<<20];PtRadeonConfig c={.ram={ram,sizeof ram,NULL}};PtRadeon *d;assert(!pt_radeon_create(&d,&c));
        void *x=m->create(d);assert(x);dr.vertex=saved;
        dr.height=dr.scissor[3]=N;dr.depth_compare=LESS;dr.depth_write=1;dr.stencil_enable=0;dr.depth_clear_format=format;
        uint8_t pitch[4]={N,0,0,0};assert(!pt_radeon_mmio(d,0x4f24,pitch,4,1));
        unsigned bpp=format==1?2:4;uint8_t bytes[N*N*4];
        for(unsigned i=0;i<N*N;i++)assert(optics_depth_pack(format,0.75f,0x67,bytes+i*bpp));
        assert(optics_frontend_gpu_write(d,Z0,bytes,N*N*bpp));
        draw(x,d,C0,Z0,0.5f,1,0,0);assert(pixel(x,d,C0)==0xffff0000u);
        /* Read only one byte first; it must publish both planes and every row. */
        assert(!m->materialize(x,d,Z0+N*bpp*8,Z0+N*bpp*8+1,0));
        uint8_t q[4];assert(optics_frontend_gpu_read(d,Z0+(8*N+8)*bpp,q,bpp));
        uint8_t want[4]={0};assert(optics_depth_pack(format,0.5f,0x67,want));assert(!memcmp(q,want,bpp));
        /* The CPU aperture presents the canonical integer in PPC byte order. */
        uint32_t control=(format==1?1u:2u)<<20;uint8_t aperture[4],ctrl[4];
        for(unsigned i=0;i<4;i++)ctrl[i]=(uint8_t)(control>>(8*i));
        assert(!pt_radeon_mmio(d,0xb00,ctrl,4,1));assert(!pt_radeon_vram(d,Z0+(8*N+8)*bpp,aperture,bpp,0));
        for(unsigned i=0;i<bpp;i++)assert(aperture[i]==want[bpp-1-i]);
        memset(ctrl,0,4);assert(!pt_radeon_mmio(d,0xb00,ctrl,4,1));
        PtR2IoStats before,after;pt_r2_io_stats(optics_frontend_io(d),&before);
        for(unsigned i=0;i<200;i++)assert(!m->materialize(x,d,Z0+7,Z0+8,0));
        pt_r2_io_stats(optics_frontend_io(d),&after);assert(before.cpu_writes==after.cpu_writes);
        /* A later CPU byte write drops the view only after exporting untouched bytes. */
        draw(x,d,C0,Z0,0.25f,0,1,0);assert(!m->materialize(x,d,Z0+(8*N+8)*bpp,Z0+(8*N+8)*bpp+1,1));
        assert(optics_frontend_gpu_write(d,Z0+(8*N+8)*bpp,(uint8_t[]){0x42},1));
        draw(x,d,C0,Z0,0.6f,0,0,1);assert(pixel(x,d,C0)==0xff00ff00u);
        assert(!m->materialize(x,d,Z0+(8*N+9)*bpp,Z0+(8*N+9)*bpp+bpp,0));
        assert(optics_frontend_gpu_read(d,Z0+(8*N+9)*bpp,q,bpp));assert(optics_depth_pack(format,0.25f,0x67,want));assert(!memcmp(q,want,bpp));
        /* A pending clear becomes visible on a CPU read even without another draw. */
        uint32_t clear=format==1?0x1234u:0x89abcd67u;uint8_t word[4];
        for(unsigned i=0;i<4;i++)word[i]=(uint8_t)(clear>>(8*i));
        assert(!pt_radeon_mmio(d,0x4f28,word,4,1));memset(word,0,4);word[0]=(uint8_t)format;
        assert(!pt_radeon_mmio(d,0x4f10,word,4,1));assert(!m->clear(x,d,Z0,2));
        assert(!m->materialize(x,d,Z0,Z0+1,0));assert(optics_frontend_gpu_read(d,Z0,q,bpp));
        for(unsigned i=0;i<bpp;i++)assert(q[i]==(uint8_t)(clear>>(8*i)));
        /* Reset must export dirty depth before retiring its private Metal storage. */
        draw(x,d,C0,Z0,0.0f,1,0,0);assert(!m->reset(x,d));assert(optics_frontend_gpu_read(d,Z0+(8*N+8)*bpp,q,bpp));
        assert(optics_depth_pack(format,0.0f,(uint8_t)clear,want));assert(!memcmp(q,want,bpp));
        m->destroy(x,d);pt_radeon_close(d);
    }
    puts("PASS: linear D16/D24S8 import, lazy exact packing, retained stencil, byte write and repeated reads");return 0;
}
