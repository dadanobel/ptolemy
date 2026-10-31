/* SPDX-License-Identifier: GPL-2.0-or-later */
/* R300 3D draw -> normalized OpticsDraw. Register facts: AMD R3xx references and Linux/Mesa r300_reg.h for VAP, VTE,
 * SE_VPORT, scissor (offset 1440), TX_FORMAT component selects (A/R/G/B shifts 9/12/15/18), TX_OFFSET, blend factors.
 * Unverified: the RS_IP/RS_INST field layout has not been checked against a primary R300 document. Vertex stream data
 * types (FLOAT_1..4, BYTE, D3DCOLOR, SHORT_2/4 with SIGNED/NORMALIZE), TX formats 0x0/0x1/0x3/0x6/0xA/0xB/0xC/0xE/
 * 0xF-0x11 and the RB3D_CBLEND/ABLEND factor and function codes follow r300_reg.h.
 * Refused (with the code in the reason): other texture formats, blend factor codes outside the GL set, colour formats
 * other than those listed at the colour target. */
#include "draw.h"
#include "r300_state.h"
#include "pixel.h"
#include "raw_vertex.h"
#include "shader_failure.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    VAP_CNTL_STATUS=0x2140,VAP_OUT_VTX_FMT_0=0x2090,VAP_OUT_VTX_FMT_1=0x2094,SE_VTE_CNTL=0x20b0,VAP_VTX_SIZE=0x20b4,
    VAP_PROG_STREAM_CNTL_0=0x2150,VAP_PROG_STREAM_CNTL_EXT_0=0x21e0,VAP_PSC_SGN_NORM_CNTL=0x21dc,SE_VPORT_XSCALE=0x1d98,
    RS_COUNT=0x4300,RS_INST_COUNT=0x4304,RS_IP_0=0x4310,RS_INST_0=0x4330,SC_SCISSORS_TL=0x43e0,SC_SCISSORS_BR=0x43e4,
    TX_ENABLE=0x4104,TX_FILTER0_0=0x4400,TX_FILTER1_0=0x4440,TX_FORMAT0_0=0x4480,TX_FORMAT1_0=0x44c0,TX_FORMAT2_0=0x4500,TX_OFFSET_0=0x4540,
    US_ALU_CONST_0=0x4c00,RB3D_BLENDCNTL=0x4e04,RB3D_BLEND_COLOR=0x4e10,RB3D_COLOR_CHANNEL_MASK=0x4e0c,RB3D_COLOROFFSET0=0x4e28,
    RB3D_ABLENDCNTL=0x4e08,RB3D_COLORPITCH0=0x4e38,US_OUT_FMT_0=0x46a4,ZB_CNTL=0x4f00,ZB_DEPTHOFFSET=0x4f20,SCISSOR_OFFSET=1440,GA_POINT_S0=0x4200,GA_POINT_SIZE=0x421c,
};
#define R(a) (reg[(a)/4])
typedef struct {uint32_t type,dwords,skip,dst,ext;} Stream;
static int refuse(const char **reason,const char *why) { if(reason)*reason=why; return 0; }
/* ZB_DEPTHCLEARVALUE by ZB_FORMAT: 24-bit formats hold Z in bits 31:8 (stencil 7:0), 16-bit formats in 15:0. */
float optics_draw_depth_clear(uint32_t value,uint32_t format) {
    return (format&15)==2?(float)(value>>8)/16777215.0f:(float)(value&0xffff)/65535.0f;
}
/* ZMASK=0 in D24 mode updates stencil even when stencil testing is disabled. */
unsigned optics_draw_clear_mask(unsigned planes,uint32_t format) {
    return (planes&2) && (format&15)==2 ? planes|4 : planes;
}
static float f32(uint32_t v) { float f; memcpy(&f,&v,4); return f; }
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__!=__ORDER_LITTLE_ENDIAN__
#error "vertex array words are read with host loads: little-endian hosts only"
#endif
/* R300 fragment constants are 24-bit floats: sign, 7-bit exponent (bias 63), 16-bit mantissa. */
static float f24(uint32_t v) {
    if(!(v&0x7fffff)) return 0.0f;
    uint32_t s=v>>23&1,e=v>>16&0x7f,m=v&0xffff; uint32_t bits=s<<31|(e-63+127)<<23|m<<7; return f32(bits);
}
/* One stream element from its dwords. type: data type 3:0 | SIGNED bit 14 | NORMALIZE bit 15. BYTE: x = bits 7:0 of
 * the (swapped) dword; D3DCOLOR: x,y,z,w = bytes 2,1,0,3; SHORT: x = bits 15:0. Absent components read (0,0,0,1). */
/* VAP_CNTL_STATUS[1:0] array byte swap, before component extraction (AMD R3xx guide): 0 none, 1 bytes
 * within 16-bit halves, 2 full dword reverse, 3 exchange the 16-bit halves. */
static uint32_t vap_swap(uint32_t x,unsigned mode) {
    return mode==2?x>>24|(x>>8&0xff00)|(x<<8&0xff0000)|x<<24:mode==1?(x>>8&0x00ff00ff)|(x<<8&0xff00ff00):mode==3?x>>16|x<<16:x;
}
void optics_texture_unswap(const OpticsTexture *t,uint8_t *p,size_t n) {
    if(t->swap)for(size_t i=0;i+4<=n;i+=4){uint32_t x=vap_swap((uint32_t)p[i]|(uint32_t)p[i+1]<<8|(uint32_t)p[i+2]<<16|(uint32_t)p[i+3]<<24,t->swap);
        p[i]=(uint8_t)x;p[i+1]=(uint8_t)(x>>8);p[i+2]=(uint8_t)(x>>16);p[i+3]=(uint8_t)(x>>24);}
}
/* Signed method 2 follows AMD's endpoint/zero examples and OpenGL 1.5/2.1
 * table 2.9: (2*c+1)/(2^n-1). Unverified with a guest, so opt-in (OPTICS_VAP_NORM2=1). */
static void decode_stream(uint32_t t,unsigned method,const uint32_t w[4],float raw[4]) {
    unsigned type=t&15,sign=t>>14&1,norm=t>>15&1; raw[0]=raw[1]=raw[2]=0; raw[3]=1;
    if(type<4){ for(unsigned c=0;c<=type;c++) raw[c]=f32(w[c]); return; }
    unsigned n=type==6?2:4,bits=type<6?8:16;
    for(unsigned c=0;c<n;c++) {
        unsigned k=type==5?(c<3?2-c:3):c; uint32_t x=type<6?w[0]>>(8*k)&255:w[k/2]>>(16*(k&1))&0xffff;
        float m=(float)((1u<<(bits-sign))-1),f=sign&&x>>(bits-1)?(float)x-(float)(1u<<bits):(float)x;
        raw[c]=norm?(sign&&method==2?(2.0f*f+1.0f)/(float)((1u<<bits)-1):method==1?fmaxf(f/m,-1.0f):f/m):f;
    }
}
/* Keep conversion in the native Metal vertex fetch for an exact common AoS subset.
 * Unsupported layouts use the normalized decoder; no new guest memory mapping. */
static int raw_snapshot(OpticsDraw *d,const Stream *stream,unsigned ns,const OpticsAos *aos,unsigned swap,OpticsFetch fetch,void *ctx) {
    static int enabled=-1;if(enabled<0){const char *v=getenv("OPTICS_RAW_VERTICES");enabled=v&&atoi(v)!=0;}
    if(!enabled||!d->vertices||swap==1||swap>=3)return 0;
    if(aos->array[0].stride>64)return 0;
    uint32_t stride=aos->array[0].stride*4,mask=0;uint64_t base=UINT64_MAX;unsigned extent=0;
    if(!stride||stride>256)return 0;
    for(unsigned s=0;s<ns;s++)if(aos->array[s].offset<base)base=aos->array[s].offset;
    for(unsigned s=0;s<ns;s++){
        uint32_t type=stream[s].type&15,dst=stream[s].dst;
        if((type>3&&stream[s].type!=0x8004)||stream[s].ext!=0xf688||aos->array[s].components<stream[s].dwords||
           aos->array[s].stride!=stride/4||(mask&(1u<<dst))||(type==4&&swap))return 0;
        uint64_t off=aos->array[s].offset-base;unsigned n=stream[s].dwords*4;
        if(off>stride||n>stride-off)return 0;
        d->raw_attribute[dst]=(OpticsRawAttribute){(uint16_t)off,(uint8_t)(type<4?type+1:4),(uint8_t)(type<4?swap==2:2)};
        if(off+n>extent)extent=(unsigned)off+n;mask|=1u<<dst;
    }
    if(mask!=(1u<<d->inputs)-1)return 0;
    size_t size=(size_t)d->vertices*stride,span=size-stride+extent;
    uint8_t *snapshot=calloc(size,1);if(!snapshot)return 0;
    if(!fetch(ctx,base,snapshot,span)){free(snapshot);return 0;}
    d->raw_vertex=snapshot;d->raw_stride=(uint16_t)stride;return 1;
}

/* Separate arrays are packed as bytes, never expanded to float4 on the CPU.
 * Each source is checked and snapshotted before a render worker sees the draw. */
static int raw_stream_snapshot(OpticsDraw *d,const Stream *stream,unsigned ns,const OpticsAos *aos,
    const uint32_t *immediate,unsigned vsize,unsigned swap,uint32_t normalization,OpticsFetch fetch,void *ctx) {
    static int enabled=-1;if(enabled<0){const char *a=getenv("OPTICS_RAW_VERTICES"),*b=getenv("OPTICS_RAW_STREAMS");enabled=a&&atoi(a)!=0&&b&&atoi(b)!=0;}
    if(!enabled||!d->vertices||swap>3)return 0;
    uint32_t mask=0,packed=0;uint64_t base=UINT64_MAX;unsigned extent=0;
    for(unsigned j=0;j<16;j++){d->raw_attribute[j]=(OpticsRawAttribute){0,4,OPTICS_RAW_DEFAULT};d->raw_select[j]=0;}
    for(unsigned s=0;s<ns;s++){
        unsigned dst=stream[s].dst,type=stream[s].type&15,method=normalization>>(2*s)&3;
        if(mask&(1u<<dst))return 0;mask|=1u<<dst;
        unsigned format=type<4?OPTICS_RAW_FLOAT+type+4*swap:OPTICS_RAW_INT+(type-4)+((stream[s].type>>14&1)<<2)+
            ((stream[s].type>>15&1)<<3)+((method==1)<<4)+(swap<<5);
        if(type>=4&&(stream[s].type&0xc000)==0xc000&&method==2)format=OPTICS_RAW_NORM2+(type-4)+4*swap;
        d->raw_attribute[dst]=(OpticsRawAttribute){(uint16_t)packed,(uint8_t)(type<4?type+1:type==6?2:4),(uint8_t)format};
        d->raw_select[dst]=(uint16_t)stream[s].ext;packed+=stream[s].dwords*4;
        if(!immediate&&aos->array[s].offset<base)base=aos->array[s].offset;
    }
    if(!packed||packed>256)return 0;
    if(immediate){
        unsigned at=0;for(unsigned s=0;s<ns;s++){d->raw_attribute[stream[s].dst].offset=(uint16_t)(4*at);extent=4*(at+stream[s].dwords);at+=stream[s].dwords+stream[s].skip;}
        unsigned stride=4*vsize;uint8_t *bytes=calloc(d->vertices,stride);if(!bytes)return 0;
        memcpy(bytes,immediate,(size_t)(d->vertices-1)*stride+extent);
        d->raw_vertex=bytes;d->raw_stride=(uint16_t)stride;d->raw_general=1;return 1;
    }
    /* A common interleaved row can be copied once, even for integer formats,
     * gaps and component routing. Partial attributes need the zero-filled pack. */
    uint64_t stride=(uint64_t)aos->array[0].stride*4;int interleaved=stride&&stride<=256;
    for(unsigned s=0;s<ns;s++){
        uint64_t offset=aos->array[s].offset-base;unsigned size=stream[s].dwords*4;
        if(aos->array[s].stride!=aos->array[0].stride||aos->array[s].components<stream[s].dwords||offset>stride||size>stride-offset)interleaved=0;
        if(interleaved&&offset+size>extent)extent=(unsigned)offset+size;
    }
    if(interleaved){
        uint8_t *bytes=calloc(d->vertices,(size_t)stride);if(!bytes)return 0;
        if(fetch(ctx,base,bytes,(size_t)(d->vertices-1)*stride+extent)){
            for(unsigned s=0;s<ns;s++)d->raw_attribute[stream[s].dst].offset=(uint16_t)(aos->array[s].offset-base);
            d->raw_vertex=bytes;d->raw_stride=(uint16_t)stride;d->raw_general=1;return 1;
        }free(bytes); /* A combined mapping may cross an inaccessible hole. */
    }
    uint8_t *snapshot=calloc((size_t)d->vertices,packed);if(!snapshot)return 0;
    for(unsigned s=0;s<ns;s++){
        unsigned n=stream[s].dwords<aos->array[s].components?stream[s].dwords:aos->array[s].components;
        if(!n)continue;uint64_t step=(uint64_t)aos->array[s].stride*4,span=(uint64_t)(d->vertices-1)*step+4u*n;
        if(span>(64u<<20)){free(snapshot);return 0;}
        uint8_t *array=malloc((size_t)span);if(!array){free(snapshot);return 0;}
        if(!fetch(ctx,aos->array[s].offset,array,(size_t)span)){free(array);free(snapshot);return 0;}
        for(unsigned v=0;v<d->vertices;v++)memcpy(snapshot+(size_t)v*packed+d->raw_attribute[stream[s].dst].offset,array+(size_t)v*step,4u*n);
        free(array);
    }
    d->raw_vertex=snapshot;d->raw_stride=(uint16_t)packed;d->raw_general=1;return 1;
}

/* Decoded programs by exact source words: a draw neither decodes nor re-validates a program seen before (the state
 * generation is the fast path). Decoded IR is canonical by construction, so per draw only the interface is checked.
 * Limit: 16 entries per stage, LRU, static (one CP thread); grow it when a title cycles through more programs. */
typedef struct { uint64_t id,used; uint32_t count,flow,constants,inputs; const char *fail; uint32_t words[PT_R2_SHADER_WORDS]; PtR2ShaderIR ir; } Program;
static Program *programs[2][16]; static const Program *last[2]; static const struct OpticsR300State *last_state[2];
static uint64_t last_generation[2],program_clock,program_ids=1;
/* Program ids by exact content. A program evicted from the 16-entry LRU and decoded again keeps its id, so the Metal
 * program cache (keyed by ids) does not refill with new keys: with fresh ids Doom 3, which cycles through more
 * programs, reached the cache's 512-program admission limit. Limit: 4096 per stage; past that a fresh id each time. */
typedef struct { uint64_t hash,id; uint32_t count,flow,*words; } Known;
static Known known[2][4096];
static uint64_t program_id(unsigned k,const uint32_t *w,uint32_t count,uint32_t flow) {
    uint64_t h=14695981039346656037ull^count^(uint64_t)flow<<32;
    for(uint32_t i=0;i<count;i++){h^=w[i];h*=1099511628211ull;}
    for(uint32_t i=(uint32_t)(h&4095),n=0;n<4096;i=(i+1)&4095,n++) {
        Known *e=&known[k][i];
        if(!e->words) {
            if(!(e->words=malloc(4u*count))) break;
            memcpy(e->words,w,4u*count); e->hash=h; e->count=count; e->flow=flow; e->id=++program_ids; return e->id;
        }
        if(e->hash==h && e->count==count && e->flow==flow && !memcmp(e->words,w,4u*count)) return e->id;
    }
    return ++program_ids;
}
/* Highest PVS register of a bank read, plus one (constants, inputs). */
static unsigned bank_used(const PtR2ShaderIR *ir,unsigned bank) {
    unsigned n=0;
    for(unsigned k=0;k<ir->step_count;k++) if(ir->step[k].kind==PT_R2_STEP_PVS) for(int a=0;a<3;a++) {
        const PtR2ShaderOperand *r=&ir->step[k].u.pvs.arg[a]; unsigned m=r->relative?256u:r->index+1u;   /* relative: any constant */
        if(r->bank==bank && m>n) n=m;
    }
    return n;
}
static const PtR2ShaderIR *program(const struct OpticsR300State *st,PtR2ShaderStage stage,uint64_t *id,unsigned *constants,unsigned *inputs,const char **reason) {
    unsigned k=stage==PT_R2_SHADER_PVS; uint64_t g=optics_r300_generation(st,stage); const Program *hit=NULL;
    if(last[k] && last_state[k]==st && last_generation[k]==g) hit=last[k];
    else {
        static OpticsR300Program p;
        if(optics_r300_program(st,stage,&p)!=OPTICS_STATE_OK){refuse(reason,k?"PVS program state incomplete":"US program state incomplete");return NULL;}
        /* US_CODE_OFFSET's ALU_OFFSET (5:0) and TEX_OFFSET (17:13) only place the program in the US instruction
         * memory; the captured words are its active ranges and node addresses are relative to them. Without them one
         * program at another place is the same program (with them Doom 3 gave 1 082 US ids and filled the Metal
         * program cache). */
        if(!k && p.count>2) p.words[2]&=~(63u|31u<<13);
        Program **slot=NULL;
        for(unsigned i=0;i<16 && !hit;i++) {
            Program *c=programs[k][i];
            if(c && c->count==p.count && c->flow==p.flow && !memcmp(c->words,p.words,4*p.count)) hit=c;
            else if(!c){ if(!slot || *slot) slot=&programs[k][i]; }
            else if(!slot || (*slot && c->used<(*slot)->used)) slot=&programs[k][i];
        }
        if(!hit) {
            if(!*slot && !(*slot=malloc(sizeof **slot))){refuse(reason,"out of memory");return NULL;}
            Program *c=*slot; PtR2ShaderError e;
            c->count=(uint32_t)p.count; c->flow=p.flow; memcpy(c->words,p.words,4*p.count); c->id=program_id(k,p.words,(uint32_t)p.count,p.flow);
            c->fail=pt_r2_shader_decode(stage,PT_R2_SHADER_FP32_SEPARATE,p.words,p.count,k?p.flow:0,&c->ir,&e)==PT_R2_SHADER_OK?NULL
                :k?"PVS program not decodable":"US program not decodable";
            if(c->fail){ static unsigned shown; optics_shader_failure_dump(stage,p.words,p.count,k?p.flow:0,&e);
                /* the reason, not just "not decodable", in every session log; 32 programs at most */
                if(shown<32){ shown++; fprintf(stderr,"optics-draw: %s: %s (word %u: %08x)\n",c->fail,e.reason,e.word,e.raw_count?e.raw[0]:0u); } }
            c->constants=c->fail?0:bank_used(&c->ir,PT_R2_CONSTANT); c->inputs=c->fail?0:bank_used(&c->ir,PT_R2_INPUT); hit=c;
        }
        last[k]=hit; last_state[k]=st; last_generation[k]=g;
    }
    ((Program *)hit)->used=++program_clock;
    if(hit->fail){refuse(reason,hit->fail);return NULL;}
    *id=hit->id; *constants=hit->constants; *inputs=hit->inputs; return &hit->ir;
}
/* Whether the US program reads temporary t: an RS write that nothing reads cannot be observed. */
static int us_reads(const PtR2ShaderIR *us,unsigned t) {
    for(unsigned k=0;k<us->step_count;k++) { const PtR2ShaderStep *s=&us->step[k];
        if(s->kind==PT_R2_STEP_TEX && s->u.tex.src==t) return 1;
        if(s->kind==PT_R2_STEP_US) for(int j=0;j<3;j++) {
            const PtR2ShaderOperand *a=&s->u.us.rgb_source[j],*b=&s->u.us.alpha_source[j];
            if((a->bank==PT_R2_TEMP && a->index==t) || (b->bank==PT_R2_TEMP && b->index==t)) return 1;
        }
    }
    return 0;
}
/* A generated PVS program out[k]=in[k] for k<n: VE_ADD with a zero second operand. */
int optics_draw_passthrough(unsigned n,PtR2ShaderIR *ir) {
    uint32_t w[2+4*16]; PtR2ShaderError e; if(!n||n>16) return 0;
    w[0]=(n-1)<<20; w[1]=0;
    for(unsigned k=0;k<n;k++) {
        w[2+4*k]=3u|2u<<8|k<<13|0xfu<<20;
        w[3+4*k]=1u|k<<5|0u<<13|1u<<16|2u<<19|3u<<22;
        w[4+4*k]=w[5+4*k]=1u|k<<5|4u<<13|4u<<16|4u<<19|4u<<22;
    }
    return pt_r2_shader_decode(PT_R2_SHADER_PVS,PT_R2_SHADER_FP32_SEPARATE,w,2+4*n,0,ir,&e)==PT_R2_SHADER_OK;
}
static int draw_build(OpticsDraw *d,const uint32_t *reg,const struct OpticsR300State *st,uint8_t opcode,const uint32_t *payload,uint32_t words,
    const OpticsAos *aos,OpticsFetch fetch,void *fetch_context,const char **reason) {
    /* Constants are filled below; do not clear 4.5 KiB that this draw will not read. */
    memset(d,0,offsetof(OpticsDraw,pvs_constant));
    memset((uint8_t *)d+offsetof(OpticsDraw,inputs),0,sizeof *d- offsetof(OpticsDraw,inputs));
    if(opcode!=0x35 && opcode!=0x34 && opcode!=0x36) return refuse(reason,"draw opcode other than IMMD_2/VBUF_2/INDX_2");
    if(!words) return refuse(reason,"empty draw");
    uint32_t vf=payload[0],prim=vf&15,walk=vf>>4&3,count=vf>>16;
    /* walk 3: vertex data in the packet (IMMD_2); 2: vertex list from the arrays (VBUF_2); 1: indices (INDX_2). */
    if((opcode==0x35 && walk!=3) || (opcode==0x34 && walk!=2) || (opcode==0x36 && walk!=1)) return refuse(reason,"vertex walk inconsistent with the draw packet");
    if(opcode!=0x35 && (!aos || !fetch || !aos->count)) return refuse(reason,"array draw without 3D_LOAD_VBPNTR arrays");
    /* Colour target: COLORFORMAT (r300_reg.h) 3 ARGB1555, 4 RGB565, 6 ARGB8888, 9 I8, 10 ARGB16161616, 13 UV88, 15 ARGB4444.
     * Tiger 10.4's Core Image renders into ARGB16161616 intermediates (Preview); SimCity 4 draws its city map into
     * formats 15 and 13. Approximation: every format is rendered at 8 bits per channel and converted at import and
     * write-back (OPTICS_APPROX_WIDE: no quantization between draws into a narrow target); use a matching Metal target when
     * banding or blending shows the difference. */
    uint32_t pitch=R(RB3D_COLORPITCH0),format=pitch>>21&15;
    static const uint8_t cbytes[16]={0,0,0,2,2,0,4,0,0,1,8,0,0,2,0,2};
    if(!cbytes[format]){static char text[16][40];if(!text[format][0])snprintf(text[format],sizeof text[0],"colour format %u not modelled",format);return refuse(reason,text[format]);}
    if(format!=6)d->approx|=OPTICS_APPROX_WIDE;
    /* SU_CULL_MODE 0x42b8 bit 0 cull front, 1 cull back, 2 front face clockwise; SU_POLY_OFFSET_ENABLE 0x42b4 bits 0/1
     * front/back, FRONT_SCALE/FRONT_OFFSET 0x42a4/0x42a8 floats of GL factor x 12 and units x 4 (r300_reg.h readings;
     * Doom 3 sets both, and its r_offsetUnits -600 arrives as -2400). Lines and sprites are set below. */
    { uint32_t cm=R(0x42b8); d->cull=(uint8_t)(cm&3); d->front_cw=(uint8_t)(cm>>2&1);
      if(R(0x42b4)&3){ d->depth_bias=f32(R(0x42a8))/4.0f; d->depth_slope=f32(R(0x42a4))/12.0f; } }
    {   /* Log the raster state Tiger's driver programs, once per distinct value (r300_reg addresses: SU_CULL_MODE,
         * SU_POLY_OFFSET_ENABLE/FRONT_SCALE/FRONT_OFFSET, GA_POLY_MODE, FG_FOG_BLEND). Polygon mode and fog are not
         * applied. */
        static uint32_t last[6],seen[16][6]; static unsigned n; uint32_t k[6]={R(0x42b8),R(0x42b4),R(0x42a4),R(0x42a8),R(0x4288),R(0x4bc0)};
        if(memcmp(k,last,sizeof k)){ memcpy(last,k,sizeof k); unsigned i=0; while(i<n && memcmp(seen[i],k,sizeof k)) i++;
            if(i==n && n<16){ memcpy(seen[n++],k,sizeof k); fprintf(stderr,"optics-draw: unmodelled raster state: cull %#x, polygon offset %#x (front %g, %g), polygon mode %#x, fog %#x\n",
                k[0],k[1],f32(k[2]),f32(k[3]),k[4],k[5]); } }
    }
    d->cb_bpp=cbytes[format]; d->cb_format=(uint8_t)format; d->cb_select=(uint8_t)(R(US_OUT_FMT_0)>>8);
    {   /* Log whether Tiger's driver programs the output selects, once per format and value (I8, UV88 readings unverified) */
        static uint32_t seen[16]; static unsigned n; uint32_t k=format<<16|(R(US_OUT_FMT_0)&0xffff); unsigned i=0; while(i<n && seen[i]!=k) i++;
        if(i==n && n<16){ seen[n++]=k; fprintf(stderr,"optics-draw: colour format %u, US_OUT_FMT_0 %#x (first: target %#x, pitch %u)\n",format,R(US_OUT_FMT_0),R(RB3D_COLOROFFSET0),pitch&0x3ffe); }
    }
    /* ZB_CNTL: bit 0 stencil, 1 Z test, 2 Z write, 4 separate back-face stencil. ZB_ZSTENCILCNTL: ZFUNC 2:0, then
     * stencil func/fail/zpass/zfail at 5:3, 8:6, 11:9, 14:12 and the back face at 17:15.. 26:24. R300 compare order
     * NEVER LESS LEQUAL EQUAL GEQUAL GREATER NOTEQUAL ALWAYS is mapped to Metal's; stencil ops share Metal's order. */
    static const uint8_t cmp[8]={0,1,3,2,6,4,5,7};
    uint32_t zc=R(ZB_CNTL),zs=R(ZB_CNTL+4),ref=R(ZB_CNTL+8);
    d->zb_offset=R(ZB_DEPTHOFFSET)&~31u; d->depth_enable=zc>>1&1; d->depth_write=(zc>>2&1)&d->depth_enable; d->depth_compare=cmp[zs&7];
    /* Diagnostic switch: OPTICS_DEPTH_EQUAL_AS_LEQUAL=1 draws EQUAL depth tests as LEQUAL (does an exact depth match
     * between passes fail where the R300's 24-bit depth would match?). */
    { static int eq=-1; if(eq<0){const char *v=getenv("OPTICS_DEPTH_EQUAL_AS_LEQUAL"); eq=v&&atoi(v)!=0;} if(eq && d->depth_compare==2) d->depth_compare=3; }
    d->stencil_enable=zc&1; d->stencil_ref=(uint8_t)ref; d->stencil_mask=(uint8_t)(ref>>8); d->stencil_writemask=(uint8_t)(ref>>16);
    d->stencil_op[0]=cmp[zs>>3&7]; d->stencil_op[1]=zs>>6&7; d->stencil_op[2]=zs>>12&7; d->stencil_op[3]=zs>>9&7;
    if(zc>>4&1){d->stencil_back[0]=cmp[zs>>15&7];d->stencil_back[1]=zs>>18&7;d->stencil_back[2]=zs>>24&7;d->stencil_back[3]=zs>>21&7;}
    else memcpy(d->stencil_back,d->stencil_op,4);
    static int exact_pitch=-1;
    if(exact_pitch<0) { const char *v=getenv("OPTICS_EXACT_PITCH"); exact_pitch=v && atoi(v)!=0; }
    /* R3xx CB:COLORPITCH 13:1 encodes two-pixel units; bits 2:1 are not padding. */
    d->cb_offset=R(RB3D_COLOROFFSET0)&~31u; d->cb_pitch=pitch&(exact_pitch?0x3ffeu:0x1ff8u);
    if(!d->cb_pitch) return refuse(reason,"zero colour pitch");
    uint32_t tl=R(SC_SCISSORS_TL),br=R(SC_SCISSORS_BR);
    int32_t x0=(int32_t)(tl&0x1fff)-SCISSOR_OFFSET,y0=(int32_t)(tl>>13&0x1fff)-SCISSOR_OFFSET;
    int32_t x1=(int32_t)(br&0x1fff)-SCISSOR_OFFSET+1,y1=(int32_t)(br>>13&0x1fff)-SCISSOR_OFFSET+1;
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>(int32_t)d->cb_pitch)x1=(int32_t)d->cb_pitch;
    d->vte=R(SE_VTE_CNTL); for(int i=0;i<6;i++) d->viewport[i]=f32(R(SE_VPORT_XSCALE+4*i));
    /* No register holds the colour buffer's height; the scissor gives it. Doom 3's full-view scissor ends a row past its
     * 640x480 buffer, so unclamped the surface takes the first 2560 bytes of the next VRAM object: every read of that
     * object then writes the whole surface back after a GPU wait (measured: 8.7k write-backs of ~2 ms, 54 -> 40-44 fps),
     * and a clear of the surface reaches into it. With the viewport transform on, a draw covers no pixel centre below
     * the viewport. */
    if((d->vte&0xc)==0xc) { int32_t vb=(int32_t)ceilf(fabsf(d->viewport[2])+d->viewport[3]-0.5f); if(vb>y0 && vb<y1) y1=vb; }
    if(x1<=x0 || y1<=y0) return refuse(reason,"empty scissor");
    d->width=d->cb_pitch; d->height=(uint32_t)y1; d->scissor[0]=x0; d->scissor[1]=y0; d->scissor[2]=x1; d->scissor[3]=y1;
    d->color_mask=R(RB3D_COLOR_CHANNEL_MASK)&15;
    d->color_clear=R(0x4e14); d->depth_clear_word=R(0x4f28); d->depth_clear_format=R(0x4f10);
    d->depth_clear=optics_draw_depth_clear(d->depth_clear_word,d->depth_clear_format);
    uint32_t af=R(0x4bd4);   /* FG_ALPHA_FUNC: reference 7:0, function 10:8, enable 11 */
    d->alpha_test=af>>11&1; d->alpha_func=af>>8&7; d->alpha_ref=(float)(af&255)/255.0f;
    d->resolve=R(0x4e88)&1; d->resolve_offset=R(0x4e80)&~31u; d->resolve_pitch=R(0x4e84)&0x3ffe;   /* AARESOLVE_CTL/OFFSET/PITCH */
    /* Blend: RB3D_CBLEND bit 0 enable, bit 1 separate alpha (else alpha uses the colour factors, as glBlendFunc),
     * function 14:12, src 21:16, dst 29:24; RB3D_ABLEND has the same fields for alpha. Factor codes 32+n, n = ZERO ONE
     * SRC_COLOR 1-SRC_COLOR DST_COLOR 1-DST_COLOR SRC_ALPHA 1-SRC_ALPHA DST_ALPHA 1-DST_ALPHA SRC_ALPHA_SATURATE, then
     * CONST_COLOR 1-CONST_COLOR CONST_ALPHA 1-CONST_ALPHA (RB3D_BLEND_COLOR, Metal's blend colour). Functions:
     * ADD, ADD_NOCLAMP, SUB, SUB_NOCLAMP, MIN, MAX, RSUB, RSUB_NOCLAMP (an 8-bit target clamps either way). Encoded for the
     * executor in Metal's factor/operation enum order, see draw.h. */
    uint32_t blend=R(RB3D_BLENDCNTL);
    if(blend&1) {
        static const uint8_t factor[15]={0,1,2,3,6,7,4,5,8,9,10,11,12,13,14},op[8]={0,0,1,1,3,4,2,2};
        uint32_t a=blend>>1&1?R(RB3D_ABLENDCNTL):blend,f[4]={blend>>16&63,blend>>24&63,a>>16&63,a>>24&63};
        for(int i=0;i<4;i++) if(f[i]<32 || f[i]>46) return refuse(reason,"blend factor code outside the GL set");
        d->blend_color=R(RB3D_BLEND_COLOR);
        d->blend=1u<<31|factor[f[0]-32]|factor[f[1]-32]<<4|(uint32_t)op[blend>>12&7]<<8
            |factor[f[2]-32]<<12|factor[f[3]-32]<<16|(uint32_t)op[a>>12&7]<<20;
    }
    /* Programs */
    /* VAP_CNTL_STATUS bit 8: PVS bypass, the vertex stream vectors are the output vertex. Expressed as a generated
     * pass-through PVS program (output k = input k), so every executor runs one program form. */
    int bypass=R(VAP_CNTL_STATUS)>>8&1; unsigned pvs_constants=0,us_constants,pvs_inputs=16,us_inputs;
    if(bypass) {
        static PtR2ShaderIR *pass16; if(!pass16 && (pass16=malloc(sizeof *pass16)) && !optics_draw_passthrough(16,pass16)){free(pass16);pass16=NULL;}
        if(!pass16) return refuse(reason,"PVS bypass program"); d->pvs=pass16; d->pvs_id=1;
    } else if(!(d->pvs=program(st,PT_R2_SHADER_PVS,&d->pvs_id,&pvs_constants,&pvs_inputs,reason))) return 0;
    if(!(d->us=program(st,PT_R2_SHADER_US,&d->us_id,&us_constants,&us_inputs,reason))) return 0;
    d->pvs_constants=pvs_constants; d->pvs_inputs=pvs_inputs;
    /* Vertex streams: two 16-bit descriptors per register: type 3:0 (FLOAT_1..4, BYTE, D3DCOLOR, SHORT_2, SHORT_4),
     * skip dwords 7:4, PVS input 12:8, LAST_VEC 13, SIGNED 14, NORMALIZE 15. */
    uint32_t vsize=R(VAP_VTX_SIZE)&0x7f,used=0; Stream stream[16]; unsigned ns=0,field[16],vectors=0;
    for(unsigned i=0;i<16;i++) {
        uint32_t s=R(VAP_PROG_STREAM_CNTL_0+4*(i/2))>>(16*(i&1))&0xffff,e=R(VAP_PROG_STREAM_CNTL_EXT_0+4*(i/2))>>(16*(i&1))&0xffff;
        uint32_t type=s&15,dwords=type<4?type+1:type==7?2:1;
        if(type>7) return refuse(reason,"vertex stream type VECTOR_3 or reserved");
        /* Unverified: in bypass the stream vectors in order are the output vertex, whatever their PVS input field;
         * streams sharing a field still fill one vector. Halo's intro movies are YUV draws in PVS bypass whose texture
         * coordinate reads (0,0,0,1) if routed by the input field. Diagnostic switch: OPTICS_BYPASS_DST=1 routes that way. */
        static int by_dst=-1; if(by_dst<0){const char *v=getenv("OPTICS_BYPASS_DST");by_dst=v&&atoi(v)!=0;}
        unsigned dst=s>>8&31;
        if(bypass && !by_dst){ unsigned k=0; while(k<ns && field[k]!=dst) k++; field[ns]=dst; dst=k<ns?stream[k].dst:vectors++; }
        stream[ns++]=(Stream){s&0xc00f,dwords,s>>4&15,dst,e}; used+=dwords+(s>>4&15);
        if(s&0x2000) break;
        if(i==15) return refuse(reason,"vertex stream list without LAST_VEC");
    }
    /* Index list for INDX_2: 16-bit pairs (low half first) or 32-bit, after the VF_CNTL word. */
    uint32_t *indices=NULL,nvert=count;
    if(opcode==0x36) {
        int wide=vf>>11&1; uint32_t need=wide?count:(count+1)/2;
        if(1+need>words) return refuse(reason,"index data shorter than the draw");
        indices=malloc((count?count:1)*sizeof *indices); if(!indices) return refuse(reason,"out of memory");
        nvert=0; for(uint32_t i=0;i<count;i++){uint32_t w=payload[1+(wide?i:i/2)];indices[i]=wide?w:(i&1?w>>16:w&0xffff);
            if(indices[i]>=OPTICS_DRAW_MAX_VERTICES){free(indices);return refuse(reason,"vertex index outside the supported range");}
            if(indices[i]+1>nvert)nvert=indices[i]+1;}
    }
    if(nvert>OPTICS_DRAW_MAX_VERTICES || (opcode==0x35 && (!vsize || used>vsize || (uint64_t)count*vsize+1>words))){free(indices);return refuse(reason,"vertex size/count inconsistent with the packet");}
    if(opcode!=0x35 && aos->count<ns){free(indices);return refuse(reason,"fewer vertex arrays than streams");}
    d->vertices=nvert;
    for(unsigned s=0;s<ns;s++) if(stream[s].dst+1>d->inputs) d->inputs=stream[s].dst+1;
    if(d->inputs>OPTICS_DRAW_INPUTS){free(indices);return refuse(reason,"PVS input index beyond 15");}
    /* Array data endian (vap_swap). VC_SWAP swaps what the VAP fetches from system memory (GART/AGP), as
     * CP_RB_CNTL.BUF_SWAP is documented to; arrays in local VRAM are read as stored. Observed: Tiger's driver keeps
     * VC_SWAP 2 for every draw, its GART arrays hold big-endian floats and Doom 3's vertex buffers in VRAM little-endian
     * ones (the CPU wrote them through aperture 0's 32-bit swap); swapping those too gave garbage positions. swap 4: mixed
     * draw, per array in the decoder below, not in a raw snapshot. */
    unsigned vc_swap=R(VAP_CNTL_STATUS)&3,local=0,all=0;
    if(opcode!=0x35) for(unsigned s=0;s<ns;s++){ all|=1u<<s; if(aos->local>>s&1) local|=1u<<s; }
    unsigned swap=!local?vc_swap:local==all||!vc_swap?0:4;
    d->diag_vap_cntl_status=R(VAP_CNTL_STATUS); d->diag_array0=opcode!=0x35&&aos->count?(uint32_t)aos->array[0].offset:0;
    static int norm2=-1;if(norm2<0){const char *v=getenv("OPTICS_VAP_NORM2");norm2=v&&atoi(v)!=0;}
    for(unsigned s=0;s<ns;s++)if((stream[s].type&0xc00f)>>14==3&&(stream[s].type&15)>=4){unsigned method=R(VAP_PSC_SGN_NORM_CNTL)>>(2*s)&3;
        if(method==3||(method==2&&!norm2)){free(indices);return refuse(reason,"signed normalization method 2 disabled or method 3 reserved");}}
    int raw=opcode!=0x35&&prim!=1&&raw_snapshot(d,stream,ns,aos,swap,fetch,fetch_context);
    if(!raw&&prim!=1)raw=raw_stream_snapshot(d,stream,ns,aos,opcode==0x35?payload+1:NULL,vsize,opcode==0x35?0:swap,R(VAP_PSC_SGN_NORM_CNTL),fetch,fetch_context);
    if(!raw){
    d->vertex=malloc((nvert?nvert:1)*sizeof *d->vertex);if(!d->vertex){free(indices);return refuse(reason,"out of memory");}
    /* One fetch per array for the vertex range (not one translation and ownership check per attribute per vertex). */
    uint8_t *array[16]={0};
    if(opcode!=0x35 && nvert) for(unsigned s=0;s<ns;s++) {
        unsigned n=stream[s].dwords<aos->array[s].components?stream[s].dwords:aos->array[s].components;
        size_t span=(size_t)(nvert-1)*aos->array[s].stride*4+4u*n;
        if(!(array[s]=malloc(span?span:1)) || !fetch(fetch_context,aos->array[s].offset,array[s],span)) {
            for(unsigned k=0;k<=s;k++) free(array[k]); free(indices); return refuse(reason,"vertex array outside GPU memory");
        }
    }
    for(uint32_t v=0;v<nvert;v++) {
        const uint32_t *in=opcode==0x35?payload+1+(size_t)v*vsize:NULL; unsigned at=0;
        for(unsigned k=0;k<OPTICS_DRAW_INPUTS;k++){d->vertex[v][k][0]=d->vertex[v][k][1]=d->vertex[v][k][2]=0;d->vertex[v][k][3]=1;}
        for(unsigned s=0;s<ns;s++) {
            uint32_t w[4]={0,0,0,0};
            if(in) { for(unsigned c=0;c<stream[s].dwords;c++) w[c]=in[at+c]; at+=stream[s].dwords+stream[s].skip; }
            else {
                unsigned n=stream[s].dwords<aos->array[s].components?stream[s].dwords:aos->array[s].components;
                const uint8_t *b=array[s]+(size_t)v*aos->array[s].stride*4;
                unsigned sw=local>>s&1?0:vc_swap;
                for(unsigned c=0;c<n;c++){ uint32_t x; memcpy(&x,b+4*c,4); w[c]=vap_swap(x,sw); }   /* LE host: one load, not four (measured: the CP's hottest loop) */
            }
            float raw[4]; decode_stream(stream[s].type,R(VAP_PSC_SGN_NORM_CNTL)>>(2*s)&3,w,raw);
            for(unsigned c=0;c<4;c++) {
                if(!(stream[s].ext>>(12+c)&1)) continue;
                unsigned sel=stream[s].ext>>(3*c)&7;
                d->vertex[v][stream[s].dst][c]=sel<4?raw[sel]:sel==5?1.0f:0.0f;
            }
        }
    }
    for(unsigned s=0;s<ns;s++) free(array[s]);
    }
    /* Lines (2), line strip (3) and line loop (12) as Metal one-pixel lines (Cinebench draws GL_LINES). Approximation:
     * GA_LINE_CNTL width and stipple are not modelled; draw quads when wide lines show up. */
    if(prim==2||prim==3||prim==12) {
        uint32_t n=prim==2?count/2:count<2?0:prim==3?count-1:count;
        d->lines=1; d->cull=0; d->depth_bias=d->depth_slope=0; d->indices=n*2; d->index=malloc((n?n:1)*2*sizeof *d->index); if(!d->index){free(indices);return refuse(reason,"out of memory");}
        for(uint32_t l=0;l<n;l++){ uint32_t a=prim==2?2*l:l,b=prim==2?2*l+1:l+1==count?0:l+1;
            d->index[2*l]=indices?indices[a]:a; d->index[2*l+1]=indices?indices[b]:b; }
        free(indices); indices=NULL; prim=0;
    }
    /* Triangles */
    if(prim==1 && opcode==0x36){free(indices);return refuse(reason,"indexed point sprites");}
    if(prim==1) {   /* points: expand into sprite quads */
        uint32_t ps=R(GA_POINT_SIZE); d->sprite=1; d->cull=0; d->depth_bias=d->depth_slope=0;
        d->point_size[0]=(float)(ps>>16)/6.0f; d->point_size[1]=(float)(ps&0xffff)/6.0f;   /* 1/12-pixel radius units */
        for(int i=0;i<4;i++) d->sprite_st[i]=f32(R(GA_POINT_S0+4*i));
        float (*quad)[OPTICS_DRAW_INPUTS][4]=calloc(count*4u?count*4u:1,sizeof *quad); d->corner=calloc(count*4u?count*4u:1,1);
        if(!quad || !d->corner){free(quad);return refuse(reason,"out of memory");}
        static const uint8_t ring[4]={0,1,3,2};   /* quad-list vertices go around: TL, TR, BR, BL */
        for(uint32_t v=0;v<count;v++) for(int c=0;c<4;c++){memcpy(quad[4*v+c],d->vertex[v],sizeof quad[0]);d->corner[4*v+c]=ring[c];}
        free(d->vertex); d->vertex=quad; d->vertices=count*4; count*=4; prim=13;
    }
    if(!d->lines) {
        uint32_t tri=prim==4?count/3:prim==5||prim==6?(count>=3?count-2:0):prim==13?count/4*2:prim==14?(count>=4?(count-2)/2*2:0):prim==15?(count>=3?count-2:0):UINT32_MAX;
        if(tri==UINT32_MAX){free(indices);return refuse(reason,"primitive type other than triangles/fans/strips/quads/polygon");}
        d->indices=tri*3; d->index=malloc((tri?tri:1)*3*sizeof *d->index); if(!d->index){free(indices);return refuse(reason,"out of memory");}
        for(uint32_t t=0;t<tri;t++) {
            uint32_t *o=d->index+3*t;
            if(prim==4){o[0]=3*t;o[1]=3*t+1;o[2]=3*t+2;}
            else if(prim==5||prim==15){o[0]=0;o[1]=t+1;o[2]=t+2;}
            else if(prim==6){o[0]=t;o[1]=t&1?t+2:t+1;o[2]=t&1?t+1:t+2;}
            else if(prim==13){uint32_t q=t/2*4;if(t&1){o[0]=q;o[1]=q+2;o[2]=q+3;}else{o[0]=q;o[1]=q+1;o[2]=q+2;}}
            else {uint32_t q=t/2*2;if(t&1){o[0]=q+1;o[1]=q+3;o[2]=q+2;}else{o[0]=q;o[1]=q+1;o[2]=q+3;}}
            if(indices) for(int k=0;k<3;k++) o[k]=indices[o[k]];
        }
        free(indices); indices=NULL;
    }
    /* VAP output vertex: position, colours 0..3, then texture coordinate sets 0..7 in order; flattened scalar slots
     * of the texture coordinates are what RS_IP texture pointers index.
     * The rasterizer input packet streams the VAP output after the position - RS_COUNT IC_COUNT colours, then the
     * texture components - so VAP colours beyond IC_COUNT carry texture components. Cinebench R10 (1.2% of its draws):
     * VAP colours 0-3, no texture coordinates, IC_COUNT 2, IT_COUNT 3, the PVS writes position, colour 0 and colour 2,
     * and RS_IP7 takes S, T, R from texture components 0-2 (Unverified: that is colour 2). Applied only where a texture
     * pointer reaches past the texture coordinates (otherwise refused), so no other draw changes. */
    uint32_t f0=R(VAP_OUT_VTX_FMT_0),f1=R(VAP_OUT_VTX_FMT_1);
    d->diag_out_fmt[0]=f0; d->diag_out_fmt[1]=f1;
    if(!(f0&1)) return refuse(reason,"VAP output without position");
    unsigned slot=1,color_slot[4]={~0u,~0u,~0u,~0u},scal_out[64],scal_comp[64],nscal=0,ic=R(RS_COUNT)>>7&15,ncol=0,ntex=0,reach=0;
    for(unsigned c=0;c<4;c++) if(f0>>(1+c)&1) color_slot[c]=slot++;
    for(unsigned t=0;t<8;t++) ntex+=f1>>(3*t)&7;
    for(unsigned i=0,n=(R(RS_INST_COUNT)&15)+1;i<n;i++) { uint32_t in=R(RS_INST_0+4*i),ip=R(RS_IP_0+4*(in&7));
        if(in>>3&1) for(unsigned c=0;c<4;c++) if((ip>>(13+3*c)&7)<4 && (ip&63)+(ip>>(13+3*c)&7)+1>reach) reach=(ip&63)+(ip>>(13+3*c)&7)+1; }
    if(reach>ntex && !d->sprite) for(unsigned c=0;c<4;c++) if(color_slot[c]!=~0u && ncol++>=ic) for(unsigned q=0;q<4;q++){scal_out[nscal]=color_slot[c];scal_comp[nscal++]=q;}
    for(unsigned t=0;t<8;t++) {
        unsigned n=f1>>(3*t)&7; if(!n) continue; if(n>4) return refuse(reason,"texture coordinate set wider than 4");
        for(unsigned c=0;c<n;c++){scal_out[nscal]=slot;scal_comp[nscal++]=c;}
        slot++;
    }
    d->position_output=0;
    if(slot>16) return refuse(reason,"too many VAP outputs");
    /* RS: instruction i routes a texture interpolator and/or a colour interpolator into US temporaries. */
    unsigned ninst=(R(RS_INST_COUNT)&15)+1;
    for(unsigned i=0;i<ninst;i++) {
        uint32_t inst=R(RS_INST_0+4*i);
        for(int tex=1;tex>=0;tex--) {
            int write=tex?inst>>3&1:inst>>14&1; unsigned ip=tex?inst&7:inst>>11&7,addr=tex?inst>>6&31:inst>>17&31;
            if(!write) continue;
            if(d->varyings==OPTICS_DRAW_VARYINGS) return refuse(reason,"more than 8 rasterized varyings");
            uint32_t ipv=R(RS_IP_0+4*ip); unsigned k=d->varyings;
            if(tex) {
                unsigned ptr=ipv&63,out=~0u;
                for(unsigned c=0;c<4;c++) {
                    unsigned sel=ipv>>(13+3*c)&7;
                    if(sel<4 && ptr+sel>=nscal) {
                        /* Unverified: a component past the VAP output reads as GL's default texture coordinate (0,0,0,1).
                         * Some of Halo's fragment programs read texture coordinates 5 and 6 that the vertex program
                         * does not write; Tiger's driver points their RS_IP past the VAP output, at scalar 20 of 20. */
                        if(!d->sprite) { d->component[k][c]=(uint8_t)(c==3?5:4); continue; }
                        unsigned g=ptr+sel-nscal; d->sprite_varying[k]=1; d->component[k][c]=(uint8_t)(g<2?g:g==3?5:4);
                    } else if(sel<4) {
                        if(d->sprite_varying[k]) return refuse(reason,"RS varying mixes sprite and vertex coordinates");
                        if(out==~0u) out=scal_out[ptr+sel]; else if(out!=scal_out[ptr+sel]) return refuse(reason,"RS varying spans two VAP outputs");
                        d->component[k][c]=(uint8_t)scal_comp[ptr+sel];
                    } else d->component[k][c]=sel==5?5:4;
                }
                if(out==~0u) { /* constant-only varying: route position, every component replaced */ out=0; }
                d->pvs_output[k]=(uint8_t)out;
            } else {
                unsigned cp=ipv>>6&7,fmt=ipv>>9&15;
                static const int8_t table[16][4]={{0,1,2,3},{0,1,2,4},{0,1,2,5},{0},{4,4,4,3},{4,4,4,4},{4,4,4,5},{0},{5,5,5,3},{5,5,5,4},{5,5,5,5}};
                if(fmt==3||fmt==7||fmt>10) return refuse(reason,"RS colour format not modelled");
                /* Formats 5, 6, 9 and 10 are constants: they read no VAP colour (routed from position, every component
                 * replaced, as a constant-only texture varying). */
                bool reads=fmt<3||fmt==4||fmt==8;
                if(reads && (cp>=4 || color_slot[cp]==~0u)) {
                    static char text[8][16][112];
                    /* Its value matters only if a US step reads it and the US result can be seen: colour writes, the alpha
                     * test, a depth output or a KIL (Doom 3 draws with VAP output position only read colour 3). */
                    int seen=d->color_mask||d->alpha_test||d->us->writes_depth;
                    for(unsigned q=0;q<d->us->step_count && !seen;q++) seen=d->us->step[q].kind==PT_R2_STEP_TEX && d->us->step[q].u.tex.op==2;
                    if(seen && us_reads(d->us,addr)) {
                        if(!text[cp][fmt][0]) snprintf(text[cp][fmt],sizeof text[0][0],"RS colour %u (format %u) absent from the VAP output (VAP_OUT_VTX_FMT_0 %#x, _1 %#x), read by the US",cp,fmt,f0,f1);
                        return refuse(reason,text[cp][fmt]);
                    }
                    reads=false; fmt=5;   /* unobservable: a constant (0,0,0,0) */
                }
                d->pvs_output[k]=(uint8_t)(reads?color_slot[cp]:0); for(unsigned c=0;c<4;c++) d->component[k][c]=(uint8_t)table[fmt][c];
            }
            d->us_input[k]=(uint8_t)addr; d->varyings++;
        }
    }
    /* Interfaces */
    /* Unsupplied PVS inputs read the stream default (0,0,0,1); PVS outputs beyond the VAP output format are dropped. */
    unsigned outs=slot; for(unsigned o=0;o<32;o++) if(d->pvs->outputs>>o&1 && o+1>outs) outs=o+1;
    d->inputs=OPTICS_DRAW_INPUTS;
    d->vi.abi=d->fi.abi=PT_R2_SHADER_ABI; d->vi.input_count=OPTICS_DRAW_INPUTS; d->vi.output_count=outs; d->vi.constant_count=256;
    unsigned fin=1; for(unsigned k=0;k<d->varyings;k++) if(d->us_input[k]+1u>fin) fin=d->us_input[k]+1u;
    d->fi.input_count=fin; d->fi.output_count=4; d->fi.constant_count=32; d->fi.texture_mask=d->us->textures;
    d->color_output=0;
    PtR2ShaderError e;
    if(pt_r2_shader_check_interface(d->pvs,&d->vi,&e)!=PT_R2_SHADER_OK) return refuse(reason,"PVS program does not fit the VAP interface");
    if(pt_r2_shader_check_interface(d->us,&d->fi,&e)!=PT_R2_SHADER_OK) return refuse(reason,"US program does not fit the RS interface");
    /* Constants */
    for(unsigned i=0;i<(pvs_constants?pvs_constants:1);i++) {
        uint32_t w[4]={0}; if(optics_r300_pvs_vector(st,512+i,w)!=OPTICS_STATE_OK) memset(w,0,sizeof w);
        for(int c=0;c<4;c++) d->pvs_constant[i][c]=f32(w[c]);
    }
    for(unsigned i=0;i<32;i++) for(int c=0;c<4;c++) d->us_constant[i][c]=f24(R(US_ALU_CONST_0+16*i+4*c));
    /* Textures used by the fragment program */
    uint32_t enabled=R(TX_ENABLE);
    for(unsigned u=0;u<OPTICS_DRAW_TEXTURES;u++) {
        if(!(d->us->textures>>u&1)) continue;
        if(!(enabled>>u&1)) return refuse(reason,"fragment program samples a disabled texture unit");
        uint32_t f0t=R(TX_FORMAT0_0+4*u),f1t=R(TX_FORMAT1_0+4*u),f2t=R(TX_FORMAT2_0+4*u),off=R(TX_OFFSET_0+4*u),flt=R(TX_FILTER0_0+4*u);
        OpticsTexture *t=&d->texture[u];
        t->width=(f0t&0x7ff)+1; t->height=(f0t>>11&0x7ff)+1; t->pitch=f0t>>31?(f2t&0x3fff)+1:t->width;
        t->format=f1t&31; t->offset=off&~31u;
        /* Decoded through the renderer's linear layouts: X8 -> R8, Y8X8 -> RG8, W8Z8Y8X8 -> RGBA8 (X first), the 16-bit
         * formats (X in the low bits) -> 565/4444/1555 whose RGBA decode is Z,Y,X,W. */
        static const uint8_t to_rgba[4]={2,1,0,3};
        const uint8_t *map=NULL;
        if(t->format==0x0){t->layout=OPTICS_PIXEL_R8;t->bpp=1;} else if(t->format==0xc){t->layout=OPTICS_PIXEL_RGBA8;t->bpp=4;}
        else if(t->format==0x3){t->layout=OPTICS_PIXEL_RG8;t->bpp=2;}   /* Y8X8: X byte first */
        else if(t->format==0xe){t->layout=OPTICS_PIXEL_RGBA8;t->bpp=8;d->approx|=OPTICS_APPROX_WIDE;}   /* W16Z16Y16X16: the executor keeps each high byte */
        else if(t->format==0x1){t->layout=OPTICS_PIXEL_R8;t->bpp=2;d->approx|=OPTICS_APPROX_WIDE;}     /* X16 (SimCity 4): the same */
        else if(t->format==0x6){t->layout=OPTICS_PIXEL_RGB565_LE;t->bpp=2;map=to_rgba;}
        else if(t->format==0xa){t->layout=OPTICS_PIXEL_ARGB4444_LE;t->bpp=2;map=to_rgba;}
        else if(t->format==0xb){t->layout=OPTICS_PIXEL_ARGB1555_LE;t->bpp=2;map=to_rgba;}
        /* Formats 0xf-0x11 (sampled by SimCity 4 and Halo): DXT1, DXT3, DXT5 (r300_reg.h), 4x4 blocks of little-endian
         * words as stored (observed: DXT5 alpha endpoints 255, 0 at byte 0), bpp the block size, levels in the tight
         * layout described at TX_FILTER0 below. Components by the selects Tiger's driver programs (observed in Doom 3):
         * DXT1 Z,Y,X,ONE, so X=B, Y=G, Z=R as ARGB8888; DXT3/5 W,Z,Y,X, so X=A, Y=B, Z=G, W=R. Read as X=R, the HUD came
         * out opaque cyan. */
        else if(t->format>=0xf&&t->format<=0x11){static const uint8_t dxt35[4]={3,2,1,0};
            t->layout=OPTICS_PIXEL_DXT1+(t->format-0xf);t->bpp=t->format==0xf?8:16;map=t->format==0xf?to_rgba:dxt35;}
        else if(t->format==0x14||t->format==0x15){
            /* On by default (the Setup Assistant movie draws YUV422); OPTICS_YUV422=0 turns it off. */
            const char *v=getenv("OPTICS_YUV422");if(v&&!strcmp(v,"0"))return refuse(reason,"YUV422 disabled (OPTICS_YUV422=0)");
            if((f1t>>22&3)!=1)return refuse(reason,"YUV422 requires clamped hardware CSC; raw/no-clamp needs separate float layout");
            if(t->width&1)return refuse(reason,"YUV422 needs complete chroma pairs");
            if(off&0x1c)return refuse(reason,"tiled YUV422 is not a linear upload");
            d->approx|=OPTICS_APPROX_YUV601; /* Unverified: BT.601 coefficients, not checked against Tiger or hardware. */
            t->layout=optics_pixel_r300_yuv(f1t);t->bpp=2;
        }
        else {
            static char text[32][40]; if(!text[t->format][0]) snprintf(text[t->format],sizeof text[0],"texture format 0x%02x not modelled",t->format);
            return refuse(reason,text[t->format]);
        }
        /* Tiger 10.4's driver keeps texture memory byte-swapped and lets the texture unit swap it back (TX_OFFSET
         * ENDIAN_SWAP); 10.4.11 does not. */
        t->swap=(uint8_t)(off&3);
        /* TEX_COORD_TYPE 26:25: 0 2D, 1 3D (sampled as 2D), 2 cube (Doom 3's normalization and light cube maps), 3 reserved. */
        if((f1t>>25&3)==3) return refuse(reason,"texture coordinate type 3 (reserved)");
        if((f1t>>25&3)==2) { if(t->width!=t->height) return refuse(reason,"cube map faces not square"); t->cube=1; d->fi.texture_dimension[u]=2; }
        static const unsigned shift[4]={12,15,18,9};   /* output R,G,B,A select fields */
        for(unsigned c=0;c<4;c++){unsigned s=t->format==0x14||t->format==0x15?c:f1t>>shift[c]&7;if(s>5)return refuse(reason,"texture component select CUT_Z/W");t->select[c]=(uint8_t)(map&&s<4?map[s]:s);}
        t->linear=(flt>>9&3)>=2;   /* 2 linear, 3 anisotropic */
        /* TX_FILTER0: CLAMP_S 2:0, CLAMP_T 5:3 (WRAP, MIRROR, CLAMP_TO_EDGE, MIRROR_ONCE_TO_EDGE, CLAMP, MIRROR_ONCE,
         * CLAMP_TO_BORDER, MIRROR_ONCE_TO_BORDER), MAG 10:9 and MIN 12:11 (1 point, 2 linear, 3 anisotropic), MIP 14:13
         * (0 none, 1 point, 2 linear), MAX_MIP_LEVEL 20:17 (the first level used). Approximation: CLAMP (GL_CLAMP) as edge
         * clamp, border modes with a zero border (TX_BORDER_COLOR not modelled).
         * Mip levels: NUM_LEVELS = TX_FORMAT0 29:26, levels - 1. Tiger's driver stores them one after another from the
         * largest, rows of exactly width * bytes per texel, a cube's six faces of a level together: in 461 of 517
         * mipmapped textures of HL and Doom 3 (a cube map among them) each level matched a 2x2 box filter of the level
         * above better in this layout than with rows padded to 32 bytes as R5xx Acceleration 4.1 describes for its parts,
         * in none worse. Anisotropy: MAG/MIN 3; bits 23:21 were 3 in every such texture there. Unverified: read as log2 of
         * the largest ratio (8:1). */
        static const uint8_t address[8]={2,3,0,1,0,1,4,1};
        unsigned mip=flt>>13&3,levels=f0t>>26&15,full=1,aniso=(flt>>9&3)==3 || (flt>>11&3)==3 ? flt>>21&7 : 0;
        for(unsigned m=t->width>t->height?t->width:t->height;m>1;m>>=1) full++;
        if(mip==3) return refuse(reason,"texture MIP_FILTER 3 (reserved)");
        t->levels=1;
        static int mips=-1; if(mips<0){ const char *v=getenv("OPTICS_MIP_LEVELS"); mips=v?atoi(v):1; }   /* diagnostic switch: 0 level 0 only, 2 no anisotropy */
        if(mips!=1) aniso=0;
        if(mip && levels && mips) {
            if(f0t>>31 || levels+1>full || (R(TX_FILTER1_0+4*u)>>3&0x3ff)) d->approx|=OPTICS_APPROX_MIP;   /* TXPITCH, chain, LOD bias */
            if(!(f0t>>31)) t->levels=(uint8_t)(levels+1<full?levels+1:full);
        }
        unsigned first=t->levels>1?(flt>>17&15):0; if(first>=t->levels) first=t->levels-1u;
        if(aniso>4){ aniso=4; d->approx|=OPTICS_APPROX_ANISO; }
        if((flt&7)>=4 || (flt>>3&7)>=4) {
            d->approx|=OPTICS_APPROX_BORDER;
            static int dump=-1; static unsigned shown; static uint64_t seen[32];
            if(dump<0){ const char *v=getenv("OPTICS_BORDER_DUMP"); dump=v&&atoi(v)!=0; }
            if(dump && shown<32) {   /* development: border modes and colours in use */
                uint64_t h=(uint64_t)(flt&0xffff)<<40^(uint64_t)R(0x45c0+4*u)<<8^t->format; unsigned j=0; while(j<shown && seen[j]!=h) j++;
                if(j==shown){ seen[shown++]=h; fprintf(stderr,"optics-border: unit %u filter0 %08x (S %u T %u) border %08x format %#x %ux%u levels %u\n",u,flt,flt&7,flt>>3&7,R(0x45c0+4*u),t->format,t->width,t->height,t->levels); }
            }
        }
        if(off&0x1c) d->approx|=OPTICS_APPROX_TILED;                             /* TXO_MACRO_TILE 2, TXO_MICRO_TILE 4:3 */
        t->sampler=address[flt&7]|address[flt>>3&7]<<3|((flt>>9&3)>=2)<<6|((flt>>11&3)>=2)<<7|(t->levels>1?mip:0)<<8|aniso<<10|first<<13;
        d->texture_mask|=1u<<u;
    }
    return 1;
}
/* Failed builds never transfer ownership of partial vertex/index allocations. */
int optics_draw_build(OpticsDraw *d,const uint32_t *reg,const struct OpticsR300State *st,uint8_t opcode,const uint32_t *payload,uint32_t words,
    const OpticsAos *aos,OpticsFetch fetch,void *context,const char **reason) {
    int ok=draw_build(d,reg,st,opcode,payload,words,aos,fetch,context,reason);
    if(!ok)optics_draw_free(d);
    return ok;
}
void optics_draw_free(OpticsDraw *d) { if(d){free(d->vertex);free(d->raw_vertex);free(d->index);free(d->corner);d->vertex=NULL;d->raw_vertex=NULL;d->raw_stride=0;d->raw_general=0;d->index=NULL;d->corner=NULL;} }
/* Vertex stage on the CPU with the foundation interpreter: window position (after SE_VTE, plus the sprite corner) and
 * the RS-selected varyings. Fine for compositor/2D-sized vertex counts; the Metal executor runs PVS+VTE+RS in its
 * vertex function instead when the draw allows it. */
void optics_draw_input(const OpticsDraw *d,uint32_t v,float out[OPTICS_DRAW_INPUTS][4]) {
    if(!d->raw_vertex){memcpy(out,d->vertex[v],sizeof(float)*4*OPTICS_DRAW_INPUTS);return;}
    for(unsigned j=0;j<OPTICS_DRAW_INPUTS;j++){out[j][0]=out[j][1]=out[j][2]=0;out[j][3]=1;}
    const uint8_t *row=d->raw_vertex+(size_t)v*d->raw_stride;
    for(unsigned j=0;j<d->inputs;j++){
        OpticsRawAttribute a=d->raw_attribute[j];const uint8_t *p=row+a.offset;
        if(a.format==OPTICS_RAW_DEFAULT)continue;
        if(a.format>=OPTICS_RAW_INT){
            uint32_t words[4]={};unsigned width=optics_raw_width(a.format,a.components),format=a.format-OPTICS_RAW_INT;
            int no_zero=a.format>=OPTICS_RAW_NORM2;int floating=a.format>=OPTICS_RAW_FLOAT&&a.format<OPTICS_RAW_DEFAULT;
            unsigned mode=no_zero?(a.format-OPTICS_RAW_NORM2)>>2:floating?(a.format-OPTICS_RAW_FLOAT)>>2:format>>5;
            for(unsigned k=0;k<width/4;k++){const uint8_t *q=p+k*4;words[k]=vap_swap((uint32_t)q[0]|(uint32_t)q[1]<<8|(uint32_t)q[2]<<16|(uint32_t)q[3]<<24,mode);}
            unsigned type=no_zero?(((a.format-OPTICS_RAW_NORM2)&3)+4)|0xc000:floating?(a.format-OPTICS_RAW_FLOAT)&3:((format&3)+4)|((format>>2&1)<<14)|((format>>3&1)<<15);
            float raw[4];decode_stream(type,no_zero?2:format>>4&1,words,raw);uint16_t select=d->raw_select[j];
            for(unsigned c=0;c<4;c++)if(select>>(12+c)&1){unsigned k=select>>(3*c)&7;out[j][c]=k<4?raw[k]:k==5?1.0f:0.0f;}
        }else for(unsigned c=0;c<a.components;c++){
            if(a.format==2)out[j][c]=p[c]/255.0f;
            else {const uint8_t *q=p+c*4;uint32_t x=(uint32_t)q[0]|(uint32_t)q[1]<<8|(uint32_t)q[2]<<16|(uint32_t)q[3]<<24;out[j][c]=f32(vap_swap(x,a.format==1?2:0));}
        }
    }
}
int optics_draw_vertices(const OpticsDraw *d,OpticsVertexOut *out,const char **reason) {
    PtR2ShaderIO *io=malloc(sizeof *io); PtR2ShaderError e; if(!io) return refuse(reason,"out of memory");
    memset(io,0,sizeof *io);
    if(d->pvs_constants>256){free(io);return refuse(reason,"PVS constant prefix outside the supported range");}
    memcpy(io->constant,d->pvs_constant,(size_t)d->pvs_constants*16);   /* (pvs, vi) checked by the build */
    for(uint32_t v=0;v<d->vertices;v++) {
        optics_draw_input(d,v,io->input);
        if(pt_r2_shader_execute(d->pvs,&d->vi,io,NULL,NULL,&e)!=PT_R2_SHADER_OK){free(io);return refuse(reason,"PVS interpreter failed");}
        float *p=io->output[d->position_output],w=p[3],x=p[0],y=p[1],z=p[2];
        if(!(d->vte&0x100)){x/=w;y/=w;} if(!(d->vte&0x200)) z/=w;
        if(d->vte&1)x*=d->viewport[0]; if(d->vte&2)x+=d->viewport[1]; if(d->vte&4)y*=d->viewport[2]; if(d->vte&8)y+=d->viewport[3];
        if(d->vte&16)z*=d->viewport[4]; if(d->vte&32)z+=d->viewport[5];
        if(d->sprite){int c=d->corner[v];x+=(c&1?0.5f:-0.5f)*d->point_size[0];y+=(c&2?0.5f:-0.5f)*d->point_size[1];w=1;}
        out[v].window[0]=x;out[v].window[1]=y;out[v].window[2]=z;out[v].window[3]=(d->vte&0x100)||d->sprite?1.0f:w;
        for(unsigned k=0;k<d->varyings;k++) for(unsigned q=0;q<4;q++) {
            unsigned sel=d->component[k][q];
            if(d->sprite_varying[k]) {   /* S0,T0 belong to the lower-left corner, S1,T1 to the upper-right */
                int c=d->corner[v]; float st[2]={c&1?d->sprite_st[2]:d->sprite_st[0],c&2?d->sprite_st[1]:d->sprite_st[3]};
                out[v].varying[k][q]=sel<2?st[sel]:sel==5?1.0f:0.0f;
            } else out[v].varying[k][q]=sel<4?io->output[d->pvs_output[k]][sel]:sel==5?1.0f:0.0f;
        }
    }
    free(io); return 1;
}
