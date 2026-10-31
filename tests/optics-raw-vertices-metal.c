/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Product executor: raw native fetch and normalized fetch must produce the same frame. */
#include "frontend.h"
#include "raw_vertex.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
enum{N=32};static const OpticsExecutor *metal;static uint8_t baseline[N*N*4],actual[N*N*4];
static void image(void *x,PtRadeon *d,uint32_t cb,uint8_t *out){
    for(unsigned k=0;metal->fence(x,d)!=OPTICS_FENCE_DONE;k++){assert(k<5000);usleep(1000);}
    assert(!metal->materialize(x,d,cb,cb+N*N*4,0));assert(!pt_radeon_vram(d,cb,out,N*N*4,0));
}
static void put(uint8_t *p,float f,int be){uint32_t u;memcpy(&u,&f,4);for(unsigned k=0;k<4;k++)p[k]=(uint8_t)(u>>(8*(be?3-k:k)));}
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
    const char *why=NULL;draw.vertex=vertices;draw.cb_offset=0x100000;
    assert(metal->draw(x,d,&draw,&why)==OPTICS_EXEC_DONE);image(x,d,draw.cb_offset,baseline);
    assert(baseline[(N/2*N+N/2)*4]==255&&baseline[(N/2*N+N/2)*4+1]==128&&baseline[(N/2*N+N/2)*4+2]==64);
    for(unsigned variant=0;variant<5;variant++){
        uint8_t raw[3*508]={};unsigned stride=variant==2?20:variant==3?508:variant==4?8:32;
        for(unsigned v=0;v<3;v++){
            if(variant==4){
                uint32_t pos=(uint16_t)(int)xy[v][0]|(uint32_t)(uint16_t)(int)xy[v][1]<<16,color=0xff4080ff;
                for(unsigned c=0;c<4;c++){raw[v*stride+c]=(uint8_t)(pos>>(8*(c^1)));raw[v*stride+4+c]=(uint8_t)(color>>(8*(c^1)));}
            }else{
                for(unsigned c=0;c<4;c++)put(raw+v*stride+c*4,vertices[v][0][c],variant==1);
                if(variant==2){raw[v*stride+16]=64;raw[v*stride+17]=128;raw[v*stride+18]=raw[v*stride+19]=255;}
                else for(unsigned c=0;c<4;c++)put(raw+v*stride+16+c*4,vertices[v][1][c],variant==1);
            }
        }
        draw.cb_offset=0x200000+variant*0x100000;draw.vertex=NULL;draw.raw_vertex=raw;draw.raw_stride=(uint16_t)stride;
        draw.raw_attribute[0]=(OpticsRawAttribute){0,4,(uint8_t)(variant==1)};
        draw.raw_attribute[1]=(OpticsRawAttribute){16,4,(uint8_t)(variant==2?2:variant==1)};
        draw.raw_general=variant>=3;
        if(variant==3){draw.raw_attribute[0].format=draw.raw_attribute[1].format=OPTICS_RAW_FLOAT+3;draw.raw_select[0]=draw.raw_select[1]=OPTICS_RAW_IDENTITY;}
        if(variant==4){draw.raw_attribute[0]=(OpticsRawAttribute){0,2,OPTICS_RAW_INT+2+4+32};draw.raw_attribute[1]=(OpticsRawAttribute){4,4,OPTICS_RAW_INT+1+8+32};draw.raw_select[0]=draw.raw_select[1]=OPTICS_RAW_IDENTITY;}
        assert(metal->draw(x,d,&draw,&why)==OPTICS_EXEC_DONE);memset(raw,0,sizeof raw);
        image(x,d,draw.cb_offset,actual);assert(!memcmp(actual,baseline,sizeof actual));
    }
    metal->destroy(x,d);pt_radeon_close(d);puts("PASS: product GPU LE/BE/UNORM8/SHORT2/D3DCOLOR and 508-byte stride fetch matches normalized frames; producer may release bytes");return 0;
}
