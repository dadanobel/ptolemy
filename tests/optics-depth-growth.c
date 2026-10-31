/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Success: growing a scissor/colour view preserves both existing depth planes. */
#define main original_depth_contract
#include "optics-depth-metal.c"
#undef main
static uint32_t at(void *x,PtRadeon *d,unsigned y) {
    for(unsigned i=0;m->fence(x,d)!=OPTICS_FENCE_DONE;i++){assert(i<5000);usleep(1000);}
    uint32_t off=C0+(y*N+8)*4;
    assert(!m->materialize(x,d,off,off+4,0));
    uint8_t b[4];assert(!pt_radeon_vram(d,off,b,4,0));
    return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
int main(void) {
    assert(!original_depth_contract());
    static uint8_t ram[1<<20];PtRadeonConfig c={.ram={ram,sizeof ram,NULL}};PtRadeon *d;
    assert(!pt_radeon_create(&d,&c));void *x=m->create(d);assert(x);
    uint8_t pitch[4]={N,0,0,0};assert(!pt_radeon_mmio(d,0x4f24,pitch,4,1));
    assert(!m->clear(x,d,Z0,2));
    for(unsigned v=0;v<3;v++)vert[v][0][1]*=2;
    dr.height=dr.scissor[3]=32;dr.depth_compare=LESS;dr.depth_write=1;
    dr.stencil_enable=1;dr.stencil_ref=0x5a;dr.stencil_mask=dr.stencil_writemask=255;
    dr.stencil_op[0]=7;dr.stencil_op[1]=dr.stencil_op[2]=0;dr.stencil_op[3]=2;
    memcpy(dr.stencil_back,dr.stencil_op,4);
    draw(x,d,C0,Z0,0.25f,1,0,0);assert(at(x,d,8)==0xffff0000);
    dr.height=dr.scissor[3]=64;dr.stencil_enable=0;
    draw(x,d,C0,Z0,0.75f,0,1,0);
    assert(at(x,d,8)==0xffff0000 && at(x,d,48)==0xff00ff00);
    dr.stencil_enable=1;dr.stencil_op[0]=2;dr.stencil_op[3]=0;
    memcpy(dr.stencil_back,dr.stencil_op,4);dr.depth_compare=7;dr.depth_write=0;
    draw(x,d,C0,Z0,0.5f,0,0,1);
    assert(at(x,d,8)==0xff0000ff && at(x,d,48)==0xff00ff00);
    dr.height=dr.scissor[3]=96;dr.stencil_enable=0;dr.depth_compare=LESS;dr.depth_write=1;
    draw(x,d,C0,Z0,0.9f,1,1,0);
    assert(at(x,d,8)==0xff0000ff && at(x,d,48)==0xff00ff00 && at(x,d,80)==0xffffff00);
    dr.height=dr.scissor[3]=16;draw(x,d,C0,Z0,0.95f,1,0,1);
    assert(at(x,d,8)==0xff0000ff && at(x,d,80)==0xffffff00);
    m->destroy(x,d);pt_radeon_close(d);
    puts("PASS: scissor 32 -> 64 -> 96 -> 16 preserves Z and stencil; new rows initialize independently");return 0;
}
