/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader_ir.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { char *data; size_t capacity, length; bool failed, fragment; } Writer;
static void put(Writer *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Writer *w, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    size_t remaining = w->length < w->capacity ? w->capacity - w->length : 0;
    int n = vsnprintf(remaining ? w->data + w->length : NULL, remaining, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n > SIZE_MAX - w->length) w->failed = true;
    else w->length += (size_t)n;
}
static const char component[] = "xyzw";
static bool pt_r2_relative(const PtR2ShaderIR *p)
{
    for (unsigned k = 0; k < p->step_count; ++k)
        if (p->step[k].kind == PT_R2_STEP_PVS)
            for (unsigned a = 0; a < 3; ++a) if (p->step[k].u.pvs.arg[a].relative) return true;
    return false;
}
static void operand(Writer *w, const PtR2ShaderOperand *r)
{
    const char *type = r->type == 1 ? "float" : r->type == 3 ? "float3" : "float4";
    put(w, "%s(", type);
    for (unsigned k = 0; k < r->type; ++k) {
        if (k) put(w, ",");
        put(w, "pt_sign(");
        unsigned c = r->swizzle[k];
        switch (r->bank) {
        case PT_R2_ZERO: put(w, "0.0f"); break;
        case PT_R2_ONE: put(w, "1.0f"); break;
        case PT_R2_HALF: put(w, "0.5f"); break;
        default:
            if (c == 6) put(w, "as%u.w", r->index);
            else if (c >= 4) put(w, "%s", c == 5 ? "1.0f" : "0.0f");
            else switch (r->bank) {
            case PT_R2_TEMP: put(w, "t%u.%c", r->index, component[c]); break;
            case PT_R2_ALT_TEMP: put(w, "at%u.%c", r->index, component[c]); break;
            case PT_R2_INPUT: put(w, "io.input[%u].%c", r->index, component[c]); break;
            case PT_R2_CONSTANT:
                if (r->relative)   /* outside 0..255 reads 0 (uint wraps a negative index past the bound); the load stays in bounds */
                    put(w, w->fragment ? "(uint(%u+a0.%c)<256u?pt_k[min(uint(%u+a0.%c),255u)].%c:0.0f)" : "(uint(%u+a0.%c)<256u?io.uniforms[min(uint(%u+a0.%c),255u)].%c:0.0f)",
                        r->index, component[r->relative - 1], r->index, component[r->relative - 1], component[c]);
                else put(w, w->fragment ? "pt_k[%u].%c" : "io.uniforms[%u].%c", r->index, component[c]);
                break;
            case PT_R2_CS: put(w, "cs%u.%c", r->index, component[c]); break;
            case PT_R2_AS: put(w, "as%u.%c", r->index, component[c]); break;
            case PT_R2_PRESUB_RGB: put(w, "ps.%c", component[c]); break;
            case PT_R2_PRESUB_ALPHA: put(w, "pa"); break;
            default: w->failed = true; break;
            }
            break;
        }
        put(w, ",%uu,%uu)", r->absolute, (r->negate >> k) & 1);
    }
    put(w, ")");
}
static void presub(Writer *w, unsigned op, const char *a, const char *b,const char *type)
{
    switch (op) {
    case 0: put(w, "pt_sub(%s(1.0f),pt_mul(%s(2.0f),%s))",type,type,a); break;
    case 1: put(w, "pt_sub(%s,%s)", b, a); break;
    case 2: put(w, "pt_add(%s,%s)", b, a); break;
    default: put(w, "pt_sub(%s(1.0f),%s)",type,a); break;
    }
}
static void arithmetic(Writer *w, unsigned op, unsigned lanes,
    const char *a, const char *b, const char *c)
{
    const char *type = lanes == 1 ? "float" : lanes == 3 ? "float3" : "float4";
    put(w,"pt_out(");
    switch (op) {
    case PT_R2_OP_NOP: put(w, "%s(0.0f)", type); break;
    case PT_R2_OP_MAD: put(w, "pt_add(pt_mul(%s,%s),%s)", a, b, c); break;
    case PT_R2_OP_DP3: case PT_R2_OP_DP4: case PT_R2_OP_D2A: put(w, "%s(dp)", type); break;
    case PT_R2_OP_MIN: case PT_R2_OP_MAX:
        if (lanes == 1) put(w, "pt_in(%s)%spt_in(%s)?pt_in(%s):pt_in(%s)", a, op == PT_R2_OP_MIN ? "<" : ">", b, a, b);
        else put(w, "select(pt_in(%s),pt_in(%s),pt_in(%s)%spt_in(%s))", b, a, a, op == PT_R2_OP_MIN ? "<" : ">", b);
        break;
    case PT_R2_OP_CND: case PT_R2_OP_CMP:
        if (lanes == 1) put(w, "pt_in(%s)%s?pt_in(%s):pt_in(%s)", c, op == PT_R2_OP_CND ? ">0.5f" : ">=0.0f", a, b);
        else put(w, "select(pt_in(%s),pt_in(%s),pt_in(%s)%s)", b, a, c, op == PT_R2_OP_CND ? ">0.5f" : ">=0.0f");
        break;
    case PT_R2_OP_FRC: put(w, "pt_sub(%s,pt_floor(%s))", a, a); break;
    case PT_R2_OP_SOP: put(w, "%s(ar)", type); break;
    case PT_R2_OP_EXP2: put(w, "pt_exp2(%s)", a); break;
    case PT_R2_OP_LOG2: put(w, "pt_log2(%s)", a); break;
    case PT_R2_OP_RCP: put(w, "pt_div(%s(1.0f),%s)",type,a); break;
    case PT_R2_OP_RSQ: put(w, "pt_div(%s(1.0f),pt_sqrt(%s))",type,a); break;
    case PT_R2_OP_RCP_DX: put(w, "pt_dx(pt_div(%s(1.0f),%s))",type,a); break;
    case PT_R2_OP_RSQ_DX: put(w, "pt_dx(pt_div(%s(1.0f),pt_sqrt(abs(%s))))",type,a); break;
    case PT_R2_OP_RCP_FF: put(w, "(pt_in(%s)==0.0f?0.0f:pt_div(1.0f,%s))",a,a); break;
    case PT_R2_OP_RSQ_FF: put(w, "(pt_in(%s)==0.0f?0.0f:pt_div(1.0f,pt_sqrt(abs(%s))))",a,a); break;
    case PT_R2_OP_EXPE: put(w, "pt_exp2(pt_mul(%s,%s(1.44269504f)))", a, type); break;
    case PT_R2_OP_POW: put(w, "(pt_in(%s)==0.0f&&pt_in(%s)==0.0f?%s(1.0f):pt_dx(pt_exp2(pt_mul(%s,pt_log2(abs(%s))))))", a, c, type, c, a); break;
    case PT_R2_OP_POW01: put(w, "(saturate(pt_in(%s))==0.0f&&pt_in(%s)==0.0f?%s(1.0f):saturate(pt_dx(pt_exp2(pt_mul(%s,pt_log2(saturate(%s)))))))", a, c, type, c, a); break;
    case PT_R2_OP_SIN: put(w, "sin(pt_in(%s))", a); break;
    case PT_R2_OP_COS: put(w, "cos(pt_in(%s))", a); break;
    case PT_R2_OP_MUL: put(w, "pt_mul(%s,%s)", a, b); break;
    case PT_R2_OP_ADD: put(w, "pt_add(%s,%s)", a, b); break;
    case PT_R2_OP_DST: put(w, "float4(1.0f,pt_mul(%s.y,%s.y),pt_in(%s.z),pt_in(%s.w))", a, b, a, b); break;
    case PT_R2_OP_SGE: case PT_R2_OP_SLT:
        put(w, "select(%s(0.0f),%s(1.0f),pt_in(%s)%spt_in(%s))", type, type, a, op == PT_R2_OP_SGE ? ">=" : "<", b); break;
    case PT_R2_OP_M2ADD: put(w, "pt_add(pt_mul(pt_mul(%s,%s),%s(2.0f)),%s)", a, b,type,c); break;
    case PT_R2_OP_ARL: put(w, "pt_floor(%s)", a); break;
    case PT_R2_OP_ARR: put(w, "pt_floor(pt_add(%s,%s(0.5f)))", a, type); break;
    default: w->failed = true; break;
    }
    put(w,")");
}
static void arguments(Writer *w, uint32_t mask, const PtR2ShaderInterface *i, bool declaration, bool bindings)
{
    static const char *dims[] = {"2d", "3d", "cube"};
    for (unsigned u = 0; u < 16; ++u) if (mask & (1u << u)) {
        if (declaration) {
            put(w, ",texture%s<float> tx%u", dims[i->texture_dimension[u]], u);
            if (bindings) put(w, " [[texture(%u)]]", u);
            put(w, ",sampler sp%u", u);
            if (bindings) put(w, " [[sampler(%u)]]", u);
        } else put(w, ",tx%u,sp%u", u, u);
    }
}
static void us(Writer *w, const PtR2ShaderUS *u)
{
    put(w, "{\n");
    for (unsigned j = 0; j < 3; ++j) {
        put(w, "float4 cs%u=", j); operand(w, &u->rgb_source[j]); put(w, ";\n");
        put(w, "float4 as%u=", j); operand(w, &u->alpha_source[j]); put(w, ";\n");
    }
    put(w, "float3 ps="); presub(w, u->rgb_presub, "cs0.xyz", "cs1.xyz","float3");
    put(w, ";float pa="); presub(w, u->alpha_presub, "as0.w", "as1.w","float"); put(w, ";\n");
    for (unsigned j = 0; j < 3; ++j) {
        put(w, "float3 r%c=", 'A' + j); operand(w, &u->rgb_arg[j]); put(w, ";\n");
        put(w, "float a%c=", 'A' + j); operand(w, &u->alpha_arg[j]); put(w, ";\n");
    }
    if (u->rgb_op == PT_R2_OP_D2A) put(w, "float dp=pt_add(pt_add(pt_mul(rA.x,rB.x),pt_mul(rA.y,rB.y)),rC.z);\n");
    else {
        put(w, "float dp=pt_add(pt_add(pt_mul(rA.x,rB.x),pt_mul(rA.y,rB.y)),pt_mul(rA.z,rB.z));\n");
        if (u->rgb_op == PT_R2_OP_DP4) put(w, "dp=pt_add(dp,pt_mul(aA,aB));\n");
    }
    put(w, "float ar="); arithmetic(w, u->alpha_op, 1, "aA", "aB", "aC");
    put(w, ";float3 rr="); arithmetic(w, u->rgb_op, 3, "rA", "rB", "rC"); put(w, ";\n");
    static const char *scale[7] = {"0.125f", "0.25f", "0.5f", "1.0f", "2.0f", "4.0f", "8.0f"};
    put(w, "rr=pt_mul(rr,float3(%s));ar=pt_mul(ar,%s);\n", scale[u->rgb_scale + 3], scale[u->alpha_scale + 3]);
    if (u->rgb_clamp) put(w, "rr=float3(pt_sat(rr.x),pt_sat(rr.y),pt_sat(rr.z));\n");
    if (u->alpha_clamp) put(w, "ar=pt_sat(ar);\n");
    for (unsigned j = 0; j < 3; ++j) {
        if (u->rgb_mask & (1u << j)) put(w, "t%u.%c=rr.%c;\n", u->rgb_temp, component[j], component[j]);
        if (u->rgb_output_mask & (1u << j)) put(w, "io.output[%u].%c=rr.%c;\n", u->rgb_output, component[j], component[j]);
    }
    if (u->alpha_mask) put(w, "t%u.w=ar;\n", u->alpha_temp);
    if (u->alpha_output_mask) put(w, "io.output[%u].w=ar;\n", u->alpha_output);
    if (u->depth_mask) put(w, "io.depth=ar;\n");
    put(w, "}\n");
}
static PtR2ShaderStatus emit(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *i, char *text, size_t capacity, size_t *needed,
    PtR2ShaderError *error, bool fragment)
{
    if (needed) *needed = 0;
    if (capacity && text) text[0] = 0;
    PtR2ShaderStatus s = pt_r2_shader_validate(p, i, error);
    if (s != PT_R2_SHADER_OK) return s;
    Writer w = {text, text ? capacity : 0, 0, false, fragment};
    put(&w,"// Radeon2 P1a precision profile %u\n#define PT_FP_FTZ %uu\n",p->precision,p->precision==PT_R2_SHADER_FP32_FTZ0?1u:0u);
    put(&w, "// Normalized float IO, separate arithmetic boundaries\n"
        "#include <metal_stdlib>\nusing namespace metal;\n#pragma clang fp contract(off)\n"
        "%s"
        "float pt_sign(float x,uint ab,uint neg){uint u=as_type<uint>(x);"
        "if(ab)u&=0x7fffffffu;if(neg)u^=0x80000000u;return as_type<float>(u);}\n"
        "float pt_sat(float x){return x<0.0f?0.0f:x>1.0f?1.0f:x;}\n"
        "float pt_dx(float r){return isinf(r)?copysign(as_type<float>(0x7f7fffffu),r):r;}\n"
        "float pt_in(float x){uint b=as_type<uint>(x);if(PT_FP_FTZ&&(b&0x7f800000u)==0u&&(b&0x7fffffu)!=0u)b=0u;return as_type<float>(b);}\n"
        "float pt_out(float x){uint b=as_type<uint>(x);if(PT_FP_FTZ&&(b&0x7f800000u)==0u)b=0u;return as_type<float>(b);}\n"
        "float3 pt_in(float3 x){return float3(pt_in(x.x),pt_in(x.y),pt_in(x.z));}\n"
        "float4 pt_in(float4 x){return float4(pt_in(x.x),pt_in(x.y),pt_in(x.z),pt_in(x.w));}\n"
        "float3 pt_out(float3 x){return float3(pt_out(x.x),pt_out(x.y),pt_out(x.z));}\n"
        "float4 pt_out(float4 x){return float4(pt_out(x.x),pt_out(x.y),pt_out(x.z),pt_out(x.w));}\n"
        "template<typename T>T pt_add(T a,T b){return pt_out(pt_in(a)+pt_in(b));}\n"
        "template<typename T>T pt_sub(T a,T b){return pt_out(pt_in(a)-pt_in(b));}\n"
        "template<typename T>T pt_mul(T a,T b){return pt_out(pt_in(a)*pt_in(b));}\n"
        "template<typename T>T pt_div(T a,T b){return pt_out(pt_in(a)/pt_in(b));}\n"
        "template<typename T>T pt_floor(T a){return pt_out(floor(pt_in(a)));}\n"
        "template<typename T>T pt_sqrt(T a){return pt_out(precise::sqrt(pt_in(a)));}\n"
        "template<typename T>T pt_exp2(T a){return pt_out(precise::exp2(pt_in(a)));}\n"
        "template<typename T>T pt_log2(T a){return pt_out(precise::log2(pt_in(a)));}\n"
        "%svoid pt_shader_body(%s",
        fragment ? "struct PtIO {float4 input[32];float4 output[32];float depth;uint killed;};\n"
                 : "struct PtIO {float4 input[32];float4 uniforms[256];float4 output[32];float depth;uint killed;uint2 pad;};\n",
        /* a thread PtIO left behind a call is 1 KB of private memory per fragment */
        fragment ? "static inline __attribute__((always_inline)) " : "",
        fragment ? "thread PtIO &io,constant float4 *pt_k,PT_BODY_ARGS" : "device PtIO &io");
    arguments(&w, p->textures, i, true, false);
    if (fragment) {
        /* Constant indices only. With the loops below (a variable index over the 32-entry thread arrays) HL fragments
         * took about 2.8 times the GPU time. Unverified: private-memory placement is the likely reason (not yet seen
         * in a GPU profile). */
        put(&w, "){\nio.killed=0;\n");
        for (unsigned j = 0; j < i->output_count; ++j)
            put(&w, "io.output[%u]=float4(0.0f,0.0f,0.0f,%s);\n", j, p->stage == PT_R2_SHADER_US && j < 4 ? "1.0f" : "0.0f");
    } else {
        put(&w, "){\nfor(uint j=0;j<32;j++)io.output[j]=float4(0.0f);io.killed=0;\n");
        if (p->stage == PT_R2_SHADER_US) put(&w, "for(uint j=0;j<4;j++)io.output[j].w=1.0f;\n");
    }
    /* Absolute addressing permits scalar register locals. Do not impose a
     * dynamically indexed 128-vector private array on every GPU invocation. */
    bool temp[128] = {false}, alt[20] = {false}, addr = false;
    for (unsigned k = 0; k < p->step_count; ++k) {
        const PtR2ShaderStep *st = &p->step[k];
        if (st->kind == PT_R2_STEP_TEX) {
            temp[st->u.tex.src] = true;
            if (st->u.tex.op != 2) temp[st->u.tex.dst] = true;
        } else if (st->kind == PT_R2_STEP_US) {
            const PtR2ShaderUS *u = &st->u.us;
            for (unsigned a = 0; a < 3; ++a) {
                if (u->rgb_source[a].bank == PT_R2_TEMP) temp[u->rgb_source[a].index] = true;
                if (u->alpha_source[a].bank == PT_R2_TEMP) temp[u->alpha_source[a].index] = true;
            }
            if (u->rgb_mask) temp[u->rgb_temp] = true;
            if (u->alpha_mask) temp[u->alpha_temp] = true;
        } else {
            const PtR2ShaderPVS *v = &st->u.pvs;
            for (unsigned a = 0; a < 3; ++a) {
                if (v->arg[a].bank == PT_R2_TEMP) temp[v->arg[a].index] = true;
                if (v->arg[a].bank == PT_R2_ALT_TEMP) alt[v->arg[a].index] = true;
            }
            if (v->dst_bank == PT_R2_ADDR) addr = true;
            if (v->mask && v->dst_bank == PT_R2_TEMP) temp[v->dst] = true;
            if (v->mask && v->dst_bank == PT_R2_ALT_TEMP) alt[v->dst] = true;
            if (v->dual_op != PT_R2_OP_NOP) alt[v->dual_dst] = true;
        }
    }
    for (unsigned t = 0; t < 128; ++t) if (temp[t]) {
        if (p->stage == PT_R2_SHADER_US && t < i->input_count) put(&w, "float4 t%u=io.input[%u];\n", t, t);
        else put(&w, "float4 t%u=float4(0.0f);\n", t);
    }
    for (unsigned t = 0; t < 20; ++t) if (alt[t]) put(&w, "float4 at%u=float4(0.0f);\n", t);
    if (addr || pt_r2_relative(p)) put(&w, "int4 a0=int4(0);\n");
    for (unsigned k = 0; k < p->step_count; ++k) {
        const PtR2ShaderStep *st = &p->step[k];
        put(&w, "// decoded step %u, raw word %u\n", k, st->word);
        if (st->kind == PT_R2_STEP_US) us(&w, &st->u.us);
        else if (st->kind == PT_R2_STEP_TEX) {
            unsigned op = st->u.tex.op, u = st->u.tex.unit;
            if (op == 2) put(&w, "if(any(pt_in(t%u)<float4(0.0f))){io.killed=1;return;}\n", st->u.tex.src);
            else {
                put(&w, "{float4 c=pt_in(t%u);", st->u.tex.src);
                if (fragment) { put(&w, "t%u=PT_TEX(%uu,tx%u,sp%u,c,%uu);}\n", st->u.tex.dst, u, u, u, op); continue; }
                if (op == 3) put(&w, "c.xyz=pt_div(c.xyz,float3(c.w));");
                put(&w, "t%u=tx%u.sample(sp%u,c.%s,level(%s));}\n", st->u.tex.dst, u, u,
                    i->texture_dimension[u] == 0 ? "xy" : "xyz", op == 4 ? "c.w" : "0.0f");
            }
        } else {
            const PtR2ShaderPVS *v = &st->u.pvs; unsigned lanes = v->arg[0].type, out = v->op == PT_R2_OP_LIT ? 4 : lanes;
            put(&w, "{\n");
            for (unsigned j = 0; j < 3; ++j) {
                put(&w, "%s %c=", lanes == 1 ? "float" : "float4", 'A' + j);
                operand(&w, &v->arg[j]); put(&w, ";\n");
            }
            if (lanes == 1 && (v->op == PT_R2_OP_LOG2 || v->op == PT_R2_OP_RSQ)) put(&w, "A=abs(A);\n");
            if (lanes == 4) put(&w, "float dp=pt_add(pt_add(pt_add(pt_mul(A.x,B.x),pt_mul(A.y,B.y)),pt_mul(A.z,B.z)),pt_mul(A.w,B.w));\n");
            put(&w, "%s r=", out == 1 ? "float" : "float4");
            if (v->op == PT_R2_OP_LIT)
                put(&w, "float4(1.0f,pt_in(B)>0.0f?pt_in(B):0.0f,pt_in(B)>0.0f?pt_out(precise::pow(pt_in(A)>0.0f?pt_in(A):0.0f,"
                    "clamp(pt_in(C),-128.0f,128.0f))):0.0f,1.0f)");
            else arithmetic(&w, v->op, lanes, "A", "B", "C");
            put(&w, ";\n");
            if (v->clamp) {
                if (out == 1) put(&w, "r=pt_sat(r);\n");
                else put(&w, "r=float4(pt_sat(r.x),pt_sat(r.y),pt_sat(r.z),pt_sat(r.w));\n");
            }
            if (v->dual_op != PT_R2_OP_NOP) {
                put(&w, "float m="); arithmetic(&w, v->dual_op, 1, "C.x", "C.y", "C.y"); put(&w, ";\n");
                if (v->dual_clamp) put(&w, "m=pt_sat(m);\n");
            }
            /* A0 as the interpreter: NaN and beyond +-2^20 saturate (Metal's clamp takes the bound for NaN). */
            if (v->dst_bank == PT_R2_ADDR) for (unsigned j = 0; j < 4; ++j) if (v->mask & (1u << j))
                put(&w, "a0.%c=int(clamp(r.%c,-1048576.0f,1048576.0f));\n", component[j], component[j]);
            for (unsigned j = 0; j < 4; ++j) if (v->mask & (1u << j) && v->dst_bank != PT_R2_ADDR) {
                if (v->dst_bank == PT_R2_TEMP) put(&w, "t%u.%c=r", v->dst, component[j]);
                else if (v->dst_bank == PT_R2_ALT_TEMP) put(&w, "at%u.%c=r", v->dst, component[j]);
                else put(&w, "io.output[%u].%c=r", v->dst, component[j]);
                if (out == 4) put(&w, ".%c", component[v->replicate_x ? 0 : j]);
                put(&w, ";\n");
            }
            if (v->dual_op != PT_R2_OP_NOP) put(&w, "at%u.%c=m;\n", v->dual_dst, component[v->dual_lane]);
            put(&w, "}\n");
        }
    }
    put(&w, "}\n");
    if (!fragment) {
        put(&w, "kernel void pt_shader_test(device PtIO *data [[buffer(0)]],"
            "constant uint &count [[buffer(1)]],uint id [[thread_position_in_grid]]");
        arguments(&w, p->textures, i, true, true);
        put(&w, "){if(id<count)pt_shader_body(data[id]");
        arguments(&w, p->textures, i, false, false); put(&w, ");}\n");
    }
    if (needed) *needed = w.length + 1;
    if (w.failed || !text || w.length >= capacity) {
        if (text && capacity) text[0] = 0;
        if (error) { memset(error, 0, sizeof(*error));
            error->status = w.failed ? PT_R2_SHADER_IR : PT_R2_SHADER_BUFFER;
            snprintf(error->reason, sizeof(error->reason), "%s", w.failed ? "MSL formatting failed" : "MSL buffer too small"); }
        return w.failed ? PT_R2_SHADER_IR : PT_R2_SHADER_BUFFER;
    }
    return PT_R2_SHADER_OK;
}
PtR2ShaderStatus pt_r2_shader_msl(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *i, char *text, size_t capacity, size_t *needed,
    PtR2ShaderError *error)
{
    return emit(p, i, text, capacity, needed, error, false);
}
PtR2ShaderStatus pt_r2_shader_msl_body(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *i, char *text, size_t capacity, size_t *needed,
    PtR2ShaderError *error)
{
    return emit(p, i, text, capacity, needed, error, true);
}
