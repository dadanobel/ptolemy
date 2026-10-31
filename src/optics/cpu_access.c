/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_access.h"
#include <string.h>
int optics_a64_memory_decode(uint32_t in,OpticsA64Memory *out) {
    if(!out)return 0;OpticsA64Memory m={.instruction=in,.rn=(in>>5)&31,.rm=31,.rt=in&31};
    unsigned size=in>>30,op=(in>>22)&3;m.simd=(in>>26)&1;
    if(m.simd) {
        if(op>=2&&size)return 0;
        m.bytes=(uint8_t)(op>=2?16:1u<<size);m.load=op&1;
    } else {
        if((op==2&&size==3)||(op==3&&size>=2))return 0;
        m.bytes=(uint8_t)(1u<<size);m.load=op!=0;m.sign=op>=2;m.destination64=op==2||size==3;
    }
    if((in&0x3b000000u)==0x39000000u) {m.mode=0;m.immediate=((in>>10)&4095)*m.bytes;}
    else if((in&0x3b200c00u)==0x38000000u) {m.mode=1;m.immediate=(int32_t)(in<<11)>>23;}
    else if((in&0x3b200c00u)==0x38200800u) {
        m.mode=2;m.rm=(in>>16)&31;m.extend=(in>>13)&7;
        if(m.extend!=2&&m.extend!=3&&m.extend!=6&&m.extend!=7)return 0;
        if(in&(1u<<12)){while((1u<<m.shift)<m.bytes)m.shift++;}
    } else return 0;
    *out=m;return 1;
}
OpticsCpuMemoryResult optics_a64_memory_step(const OpticsA64Memory *m,const OpticsCpuMemoryOps *o) {
    if(!m||!o||!o->get_x||!o->set_x||!o->memory)return OPTICS_CPU_FAILED;
    OpticsA64Memory decoded;if(!optics_a64_memory_decode(m->instruction,&decoded))return OPTICS_CPU_UNSUPPORTED;
    m=&decoded; /* Instruction is authoritative; never compare C padding bytes. */
    uint64_t base=0,offset=(uint64_t)m->immediate;
    if(!o->get_x(o->context,m->rn,1,&base))return OPTICS_CPU_FAILED;
    if(m->mode==2) {
        offset=0;if(m->rm!=31&&!o->get_x(o->context,m->rm,0,&offset))return OPTICS_CPU_FAILED;
        if(m->extend==2)offset=(uint32_t)offset;else if(m->extend==6)offset=(uint64_t)(int64_t)(int32_t)offset;
        offset<<=m->shift;
    }
    uint8_t data[16]={0};
    if(!m->load) {
        if(m->simd){if(!o->get_q||!o->get_q(o->context,m->rt,data))return OPTICS_CPU_FAILED;}
        else {uint64_t value=0;if(m->rt!=31&&!o->get_x(o->context,m->rt,0,&value))return OPTICS_CPU_FAILED;
            for(unsigned i=0;i<m->bytes;i++)data[i]=(uint8_t)(value>>(8*i));}
    }
    PtR2IoResult r=o->memory(o->context,base+offset,data,m->bytes,!m->load);
    if(r==PT_R2_IO_BUSY)return OPTICS_CPU_WAIT;if(r!=PT_R2_IO_OK)return OPTICS_CPU_FAILED;
    if(m->load) {
        if(m->simd){if(!o->set_q||!o->set_q(o->context,m->rt,data))return OPTICS_CPU_FAILED;}
        else if(m->rt!=31) {
            uint64_t value=0;for(unsigned i=0;i<m->bytes;i++)value|=(uint64_t)data[i]<<(8*i);
            if(m->sign)value=(uint64_t)((int64_t)(value<<(64-m->bytes*8))>>(64-m->bytes*8));
            if(!m->destination64)value=(uint32_t)value;
            if(!o->set_x(o->context,m->rt,value))return OPTICS_CPU_FAILED;
        }
    }return OPTICS_CPU_DONE;
}
