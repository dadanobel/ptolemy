/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Product executor: raw native fetch and normalized fetch must produce the same frame. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
enum{N=32};static const OpticsExecutor *metal;static uint8_t baseline[N*N*4],actual[N*N*4];
static void image(void *x,PtRadeon *d,uint32_t cb,uint8_t *out){
    for(unsigned k=0;metal->fence(x,d)!=OPTICS_FENCE_DONE;k++){assert(k<5000);usleep(1000);}
    assert(!metal->materialize(x,d,cb,cb+N*N*4,0));assert(!pt_radeon_vram(d,cb,out,N*N*4,0));
}
int main(void){
    static uint8_t ram[4096];PtRadeonConfig cfg={.ram={ram,sizeof ram,NULL}};PtRadeon *d;metal=optics_metal_executor();optics_frontend_set_executor(NULL);assert(!pt_radeon_create(&d,&cfg));void *x=metal->create(d);assert(x);
    PtR2ShaderIR pvs,us;PtR2ShaderError e={};assert(optics_draw_passthrough(2,&pvs));
    uint32_t fs[]={0,0,0,0,0,0,0x400000,0,7u<<26,1u<<24,(21u<<7)|(20u<<14),9u|(17u<<7)|(16u<<14)};
    assert(pt_r2_shader_decode(PT_R2_SHADER_US,PT_R2_SHADER_FP32_FTZ0,fs,12,0,&us,&e)==PT_R2_SHADER_OK);
    float vertices[3][OPTICS_DRAW_INPUTS][4]={};const float xy[3][2]={{-1,-1},{3,-1},{-1,3}};uint32_t indices[]={0,1,2};
    for(unsigned v=0;v<3;v++){
        vertices[v][0][0]=xy[v][0];vertices[v][0][1]=xy[v][1];vertices[v][0][3]=1;
        vertices[v][1][0]=64/255.0f;vertices[v][1][1]=128/255.0f;vertices[v][1][2]=vertices[v][1][3]=1;
    }
    OpticsDraw draw={};draw.pvs=&pvs;draw.us=&us;draw.pvs_id=0x726177;draw.us_id=0x74657374;
    draw.vi.abi=draw.fi.abi=PT_R2_SHADER_ABI;draw.vi.input_count=draw.vi.output_count=2;draw.vi.constant_count=1;
    draw.fi.input_count=1;draw.fi.output_count=4;draw.fi.constant_count=32;draw.pvs_inputs=2;
    draw.inputs=2;draw.vertices=3;draw.indices=3;draw.index=indices;draw.width=draw.height=draw.cb_pitch=N;
    draw.scissor[2]=draw.scissor[3]=N;draw.color_mask=15;draw.varyings=1;draw.pvs_output[0]=1;
    for(unsigned c=0;c<4;c++)draw.component[0][c]=(uint8_t)c;
    draw.vte=0x3f;draw.viewport[0]=draw.viewport[1]=draw.viewport[2]=draw.viewport[3]=N/2;draw.viewport[4]=1;
    const char *why=NULL;draw.vertex=vertices;
    unsigned cases=0;
    /* The reference is the existing interpreter/window transform, forced by a
     * conservative input count. Both images use the same shader and data. */
    for(unsigned m=0;m<256;m++)for(unsigned depth=0;depth<2;depth++){
        draw.vte=(m&63)|((m>>6&1)<<8)|((m>>7&1)<<9);
        draw.viewport[0]=N/2;draw.viewport[1]=N/2;draw.viewport[2]=N/2;draw.viewport[3]=N/2;draw.viewport[4]=0.5f;draw.viewport[5]=0.125f;
        draw.depth_enable=depth;draw.depth_compare=7;draw.depth_write=depth;draw.depth_clear_format=2;draw.zb_offset=0x400000;
        draw.pvs_id=(m&1)?1:0x726177;
        for(unsigned v=0;v<3;v++){
            float w=v==0?1.0f:v==1?2.0f:0.5f;float x=(xy[v][0]+1)*N/2,y=(xy[v][1]+1)*N/2,z=0.375f;
            if(draw.vte&2)x-=draw.viewport[1];if(draw.vte&1)x/=draw.viewport[0];
            if(draw.vte&8)y-=draw.viewport[3];if(draw.vte&4)y/=draw.viewport[2];
            if(draw.vte&32)z-=draw.viewport[5];if(draw.vte&16)z/=draw.viewport[4];
            vertices[v][0][0]=draw.vte&256?x:x*w;vertices[v][0][1]=draw.vte&256?y:y*w;
            vertices[v][0][2]=draw.vte&512?z:z*w;vertices[v][0][3]=w;
            vertices[v][1][0]=(v+1)/4.0f;vertices[v][1][1]=(3-v)/4.0f;
        }
        draw.pvs_inputs=17;draw.cb_offset=0x100000;
        assert(metal->draw(x,d,&draw,&why)==OPTICS_EXEC_DONE);image(x,d,draw.cb_offset,baseline);
        draw.pvs_inputs=2;draw.cb_offset=0x200000;
        assert(metal->draw(x,d,&draw,&why)==OPTICS_EXEC_DONE);image(x,d,draw.cb_offset,actual);
        for(unsigned k=0;k<sizeof actual;k++){
            int delta=(int)actual[k]-baseline[k];if(delta<-1||delta>1){fprintf(stderr,"VTE=%#x depth=%u byte=%u GPU=%u CPU=%u\n",draw.vte,depth,k,actual[k],baseline[k]);assert(0);}
        }
        assert(actual[(N/2*N+N/2)*4]==255);cases++;
    }
    metal->destroy(x,d);pt_radeon_close(d);
    printf("PASS: %u VTE/PVS-bypass candidate frames match CPU reference (<=1 RGBA8 step); perspective W, all scale/offset/divide masks, depth enabled/disabled\n",cases);return 0;
}
