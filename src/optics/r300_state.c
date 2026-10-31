/* SPDX-License-Identifier: GPL-2.0-or-later
 * Register facts: AMD R3xx reference, Mesa r300_reg.h and Linux radeon_reg.h.
 * Register shadow and shader upload frontend. Not a complete PCI/2D/VAP/RS device. */
#include "r300_state.h"
#include <stdlib.h>
#include <string.h>
enum {REG_WORDS=0x8000/4,PVS_WORDS=1536*4};
struct OpticsR300State {
    uint32_t reg[REG_WORDS],pvs[PVS_WORDS],upload;
    uint8_t source[REG_WORDS],pvs_source[PVS_WORDS],upload_valid,zero_reset;
    uint64_t us_generation,pvs_generation;
};
OpticsR300State *optics_r300_state_create(int zero) {
    OpticsR300State *s=calloc(1,sizeof *s);if(s){s->zero_reset=!!zero;optics_r300_state_reset(s);}return s;
}
void optics_r300_state_destroy(OpticsR300State *s) {free(s);}
void optics_r300_state_reset(OpticsR300State *s) {
    if(!s)return;unsigned zero=s->zero_reset;uint64_t us=s->us_generation,pvs=s->pvs_generation;
    memset(s,0,sizeof *s);s->zero_reset=(uint8_t)zero;s->us_generation=us+1;s->pvs_generation=pvs+1;
    if(zero){memset(s->source+0x1c00/4,OPTICS_VIRTUAL_RESET,(0x5000-0x1c00)/4);memset(s->pvs_source,OPTICS_VIRTUAL_RESET,sizeof s->pvs_source);s->upload_valid=1;}
}
static int external(uint32_t a) {
    /* Only these shader-storage writes have implemented pure semantics here.
     * Other registers require explicit coordinator dispatch; an unknown port
     * must never become a successful no-op merely because it lies in 3D space. */
    return !(a==0x2200||a==0x2208||a==0x22d0||a==0x22d8||a==0x22dc||
        a==0x4600||a==0x4604||a==0x4608||(a>=0x4610&&a<=0x461c)||
        (a>=0x4620&&a<0x46a0)||(a>=0x46c0&&a<0x4ac0)||
        (a>=0x4c00&&a<0x4e00));
}
OpticsStateResult optics_r300_read(const OpticsR300State *s,uint32_t a,uint32_t *v,uint8_t *source) {
    if(!s||!v||(a&3)||a>=0x8000)return OPTICS_STATE_RANGE;
    if(!s->source[a/4])return OPTICS_STATE_UNKNOWN;
    *v=s->reg[a/4];if(source)*source=s->source[a/4];return OPTICS_STATE_OK;
}
OpticsStateResult optics_r300_accept_register(OpticsR300State *s,uint32_t a,uint32_t v,uint8_t source) {
    if(!s||(a&3)||a>=0x8000||!source||source>OPTICS_VIRTUAL_RESET)return OPTICS_STATE_RANGE;
    s->reg[a/4]=v;s->source[a/4]=source;return OPTICS_STATE_OK;
}
OpticsStateResult optics_r300_write(OpticsR300State *s,uint32_t a,uint32_t v) {
    if(!s||(a&3)||a>=0x8000)return OPTICS_STATE_RANGE;
    if(external(a))return OPTICS_STATE_EXTERNAL;
    if(a==0x2200) {
        if((v&~0x7ffu)||(v&0x7ffu)>=1536)return OPTICS_STATE_RANGE;
        s->upload=v*4;s->upload_valid=1;
    } else if(a==0x2208) {
        if(!s->upload_valid)return OPTICS_STATE_UNKNOWN;if(s->upload>=PVS_WORDS)return OPTICS_STATE_RANGE;
        if(s->upload<512*4)s->pvs_generation++;
        s->pvs[s->upload]=v;s->pvs_source[s->upload++]=OPTICS_OBSERVED;
    }
    if(a>=0x4600&&a<0x4ac0)s->us_generation++;
    if(a==0x22d0||a==0x22d8||a==0x22dc)s->pvs_generation++;
    return optics_r300_accept_register(s,a,v,OPTICS_OBSERVED);
}
OpticsStateResult optics_r300_packet(OpticsR300State *s,const PtR2Pm4Packet *p,OpticsStateEffect *effect) {
    if(effect)*effect=OPTICS_EFFECT_NONE;if(!s||!p||!effect)return OPTICS_STATE_RANGE;
    if(p->type==2)return OPTICS_STATE_OK;
    if(p->type==3) {
        switch(p->opcode) {
        case 0x10:return OPTICS_STATE_OK;
        case 0x26:*effect=OPTICS_EFFECT_BARRIER;break;
        case 0x28:case 0x29:case 0x2a:case 0x34:case 0x35:case 0x36:*effect=OPTICS_EFFECT_DRAW;break;
        case 0x2f:*effect=OPTICS_EFFECT_VERTEX_ARRAYS;break;
        case 0x33:*effect=OPTICS_EFFECT_INDEX_BUFFER;break; /* NOT an indirect command buffer. */
        case 0x32:case 0x37:case 0x38:case 0x91:case 0x92:case 0x94:case 0x9b:*effect=OPTICS_EFFECT_2D;break;
        default:return OPTICS_STATE_UNSUPPORTED;
        }return OPTICS_STATE_EXTERNAL;
    }
    if(p->type>1)return OPTICS_STATE_UNSUPPORTED;
    uint32_t upload=s->upload;int valid=s->upload_valid;
    for(uint32_t i=0;i<p->words;i++) {
        uint32_t a,v;if(!pt_r2_pm4_write(p,i,&a,&v))return OPTICS_STATE_RANGE;
        if(external(a)){*effect=a==0x1720?OPTICS_EFFECT_BARRIER:OPTICS_EFFECT_CONTROL;return OPTICS_STATE_EXTERNAL;}
        if(a==0x2200){if((v&~0x7ffu)||(v&0x7ffu)>=1536)return OPTICS_STATE_RANGE;upload=v*4;valid=1;}
        if(a==0x2208){if(!valid)return OPTICS_STATE_UNKNOWN;if(upload++>=PVS_WORDS)return OPTICS_STATE_RANGE;}
    }
    for(uint32_t i=0;i<p->words;i++){uint32_t a,v;pt_r2_pm4_write(p,i,&a,&v);if(optics_r300_write(s,a,v)!=OPTICS_STATE_OK)return OPTICS_STATE_RANGE;}
    return OPTICS_STATE_OK;
}
static OpticsStateResult append_reg(const OpticsR300State *s,OpticsR300Program *out,uint32_t a) {
    if(out->count>=PT_R2_SHADER_WORDS)return OPTICS_STATE_RANGE;uint8_t source=0;uint32_t value=0;
    OpticsStateResult r=optics_r300_read(s,a,&value,&source);if(r!=OPTICS_STATE_OK)return r;
    out->words[out->count]=value;out->source[out->count++]=source;return OPTICS_STATE_OK;
}
OpticsStateResult optics_r300_program(const OpticsR300State *s,PtR2ShaderStage stage,OpticsR300Program *out) {
    if(!s||!out)return OPTICS_STATE_RANGE;memset(out,0,sizeof *out);
    if(stage==PT_R2_SHADER_US) {
        uint32_t co;OpticsStateResult r=optics_r300_read(s,0x4608,&co,NULL);if(r!=OPTICS_STATE_OK)return r;
        unsigned first=co&63,count=((co>>6)&63)+1,tex=(co>>13)&31,nt=((co>>18)&31)+1;
        if(first+count>64||tex+nt>32)return OPTICS_STATE_RANGE;
        const uint32_t header[]={0x4600,0x4604,0x4608,0x4610,0x4614,0x4618,0x461c};
        for(unsigned i=0;i<7;i++)if((r=append_reg(s,out,header[i]))!=OPTICS_STATE_OK)return r;
        for(unsigned i=0;i<nt;i++)if((r=append_reg(s,out,0x4620+4*(tex+i)))!=OPTICS_STATE_OK)return r;
        for(unsigned b=0;b<4;b++)for(unsigned i=0;i<count;i++)if((r=append_reg(s,out,0x46c0+b*0x100+4*(first+i)))!=OPTICS_STATE_OK)return r;
        out->generation=s->us_generation;return OPTICS_STATE_OK;
    }
    if(stage==PT_R2_SHADER_PVS) {
        OpticsStateResult r=append_reg(s,out,0x22d0);if(r!=OPTICS_STATE_OK)return r;
        r=append_reg(s,out,0x22d8);if(r!=OPTICS_STATE_OK)return r;
        unsigned first=out->words[0]&1023,last=(out->words[0]>>20)&1023;if(last<first||last>=256)return OPTICS_STATE_RANGE;
        r=optics_r300_read(s,0x22dc,&out->flow,&out->flow_source);if(r!=OPTICS_STATE_OK)return r;
        for(unsigned i=first*4;i<(last+1)*4;i++) {
            if(!s->pvs_source[i])return OPTICS_STATE_UNKNOWN;
            out->words[out->count]=s->pvs[i];out->source[out->count++]=s->pvs_source[i];
        }
        out->generation=s->pvs_generation;return OPTICS_STATE_OK;
    }
    return OPTICS_STATE_UNSUPPORTED;
}
int optics_r300_memory_window(uint32_t location,uint64_t *base,uint64_t *length) {
    if(!base||!length)return 0;uint64_t start=(uint64_t)(location&65535)<<16,end=((uint64_t)(location>>16)+1)<<16;
    if(end<=start)return 0;*base=start;*length=end-start;return 1;
}
uint64_t optics_r300_generation(const OpticsR300State *s,PtR2ShaderStage stage) {
    return !s?0:stage==PT_R2_SHADER_US?s->us_generation:s->pvs_generation;
}
/* One PVS memory vector (instruction or constant slot); every word must have been observed. */
OpticsStateResult optics_r300_pvs_vector(const OpticsR300State *s,unsigned vector,uint32_t out[4]) {
    if(!s||!out||vector>=PVS_WORDS/4)return OPTICS_STATE_RANGE;
    for(unsigned c=0;c<4;c++){if(!s->pvs_source[vector*4+c])return OPTICS_STATE_UNKNOWN;out[c]=s->pvs[vector*4+c];}
    return OPTICS_STATE_OK;
}
