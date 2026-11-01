/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "shader_ir.h"
#include "shader_fp.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static float sign_mod(float f, bool absolute, bool negative)
{
    uint32_t x; memcpy(&x, &f, 4);
    if (absolute) x &= 0x7fffffffu;
    if (negative) x ^= 0x80000000u;
    memcpy(&f, &x, 4); return f;
}
static void operand(const PtR2ShaderOperand *r, const PtR2ShaderIO *io,
    float temp[][4], float cs[3][4], float as[3][4], const float ps[4],
    const float pa[4], const int32_t a0[4], float result[4])
{
    const float *base = NULL; static const float zero[4];
    switch (r->bank) {
    case PT_R2_TEMP: base = temp[r->index]; break;
    case PT_R2_ALT_TEMP: base = temp[128 + r->index]; break;
    case PT_R2_INPUT: base = io->input[r->index]; break;
    case PT_R2_CONSTANT:
        if (r->relative) { int32_t k = r->index + a0[r->relative - 1]; base = k >= 0 && k < 256 ? io->constant[k] : zero; }
        else base = io->constant[r->index];
        break;
    case PT_R2_CS: base = cs[r->index]; break;
    case PT_R2_AS: base = as[r->index]; break;
    case PT_R2_PRESUB_RGB: base = ps; break;
    case PT_R2_PRESUB_ALPHA: base = pa; break;
    default: break;
    }
    for (unsigned k = 0; k < r->type; ++k) {
        unsigned sw = r->swizzle[k]; float x;
        if (!base) x = r->bank == PT_R2_ONE ? 1.0f : r->bank == PT_R2_HALF ? 0.5f : 0.0f;
        else if (sw == 6) x = as[r->index][3];
        else x = sw < 4 ? base[sw] : sw == 5 ? 1.0f : 0.0f;
        result[k] = sign_mod(x, r->absolute != 0, (r->negate & (1u << k)) != 0);
    }
}
static float presub(unsigned op, float a, float b, unsigned p)
{
    switch (op) {
    case 0: return pt_r2_fp_sub(1.0f,pt_r2_fp_mul(2.0f,a,p),p);
    case 1: return pt_r2_fp_sub(b,a,p);
    case 2: return pt_r2_fp_add(b,a,p);
    default: return pt_r2_fp_sub(1.0f,a,p);
    }
}
/* Deliberately ordered, separate float32 operations. Build this file with
 * -fno-fast-math -ffp-contract=off; Metal uses the same expression order. */
static float dot3(const float a[4], const float b[4], unsigned p)
{
    return pt_r2_fp_add(pt_r2_fp_add(pt_r2_fp_mul(a[0],b[0],p),pt_r2_fp_mul(a[1],b[1],p),p),pt_r2_fp_mul(a[2],b[2],p),p);
}
static void arithmetic(unsigned op, unsigned lanes, const float a[4],
    const float b[4], const float c[4], float dp, float sop, float out[4],unsigned p)
{
    for (unsigned k = 0; k < lanes; ++k) {
        float x = pt_r2_fp_in(a[k],p), y = pt_r2_fp_in(b[k],p), z = pt_r2_fp_in(c[k],p), r = 0.0f;
        switch (op) {
        case PT_R2_OP_MAD: r = pt_r2_fp_add(pt_r2_fp_mul(x,y,p),z,p); break;
        case PT_R2_OP_DP3: case PT_R2_OP_DP4: case PT_R2_OP_D2A: r = dp; break;
        /* Match the ordered comparison model in both implementations,
         * including NaNs and signed zero; do not use platform min/max. */
        case PT_R2_OP_MIN: r = x < y ? x : y; break;
        case PT_R2_OP_MAX: r = x > y ? x : y; break;
        case PT_R2_OP_CND: r = z > 0.5f ? x : y; break;
        case PT_R2_OP_CMP: r = z >= 0.0f ? x : y; break;
        case PT_R2_OP_FRC: r = pt_r2_fp_sub(x,pt_r2_fp_floor(x,p),p); break;
        case PT_R2_OP_SOP: r = sop; break;
        case PT_R2_OP_EXP2: r = pt_r2_fp_exp2(x,p); break;
        case PT_R2_OP_LOG2: r = pt_r2_fp_log2(x,p); break;
        case PT_R2_OP_RCP: r = pt_r2_fp_div(1.0f,x,p); break;
        case PT_R2_OP_RSQ: r = pt_r2_fp_div(1.0f,pt_r2_fp_sqrt(x,p),p); break;
        /* DX: an input of 0 gives MAX_FLOAT (an overflow is clamped the same way, keeping its sign). */
        case PT_R2_OP_RCP_DX: r = pt_r2_fp_div(1.0f,x,p); if (isinf(r)) r = copysignf(FLT_MAX,r); break;
        case PT_R2_OP_RSQ_DX: r = pt_r2_fp_div(1.0f,pt_r2_fp_sqrt(fabsf(x),p),p); if (isinf(r)) r = FLT_MAX; break;
        /* FF: an input of 0 gives 0. */
        case PT_R2_OP_RCP_FF: r = x == 0.0f ? 0.0f : pt_r2_fp_div(1.0f,x,p); break;
        case PT_R2_OP_RSQ_FF: r = x == 0.0f ? 0.0f : pt_r2_fp_div(1.0f,pt_r2_fp_sqrt(fabsf(x),p),p); break;
        case PT_R2_OP_EXPE: r = pt_r2_fp_exp2(pt_r2_fp_mul(x,1.44269504f,p),p); break;
        case PT_R2_OP_POW: r = x == 0.0f && z == 0.0f ? 1.0f : pt_r2_fp_exp2(pt_r2_fp_mul(z,pt_r2_fp_log2(fabsf(x),p),p),p); if (isinf(r)) r = FLT_MAX; break;
        case PT_R2_OP_POW01: { float b = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; r = b == 0.0f && z == 0.0f ? 1.0f : pt_r2_fp_exp2(pt_r2_fp_mul(z,pt_r2_fp_log2(b,p),p),p);
            r = r > 1.0f ? 1.0f : r < 0.0f || r != r ? 0.0f : r; break; }
        case PT_R2_OP_SIN: r = sinf(x); break;
        case PT_R2_OP_COS: r = cosf(x); break;
        case PT_R2_OP_MUL: r = pt_r2_fp_mul(x,y,p); break;
        case PT_R2_OP_ADD: r = pt_r2_fp_add(x,y,p); break;
        case PT_R2_OP_DST: r = k == 0 ? 1.0f : k == 1 ? pt_r2_fp_mul(a[1],b[1],p) : k == 2 ? pt_r2_fp_in(a[2],p) : pt_r2_fp_in(b[3],p); break;
        case PT_R2_OP_SGE: r = x >= y ? 1.0f : 0.0f; break;
        case PT_R2_OP_SLT: r = x < y ? 1.0f : 0.0f; break;
        case PT_R2_OP_M2ADD: r = pt_r2_fp_add(pt_r2_fp_mul(pt_r2_fp_mul(x,y,p),2.0f,p),z,p); break;
        case PT_R2_OP_ARL: r = pt_r2_fp_floor(x,p); break;
        case PT_R2_OP_ARR: r = pt_r2_fp_floor(pt_r2_fp_add(x,0.5f,p),p); break;
        default: break;
        }
        out[k] = pt_r2_fp_out(r,p);
    }
}
static float saturated(float x)
{
    /* Profile-defined ordered comparisons retain NaNs and signed zero. */
    return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}
PtR2ShaderStatus pt_r2_shader_run(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *iface, PtR2ShaderIO *io,
    PtR2ShaderSample sample, void *context, PtR2ShaderError *e)
{
    PtR2ShaderStatus status = pt_r2_shader_validate(p, iface, e);
    if (status != PT_R2_SHADER_OK) return status;
    return pt_r2_shader_execute(p, iface, io, sample, context, e);
}
PtR2ShaderStatus pt_r2_shader_execute(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *iface, PtR2ShaderIO *io,
    PtR2ShaderSample sample, void *context, PtR2ShaderError *e)
{
    if (!io || (p->textures && !sample)) {
        if (e) { memset(e, 0, sizeof(*e)); e->status = PT_R2_SHADER_ARGUMENT;
            snprintf(e->reason, sizeof(e->reason), "missing IO or texture sampler"); }
        return PT_R2_SHADER_ARGUMENT;
    }
    float t[128 + 20][4] = {{0}}, cs[3][4] = {{0}}, as[3][4] = {{0}};
    float ps[4] = {0}, pa[4] = {0};
    int32_t a0[4] = {0};
    unsigned profile=p->precision;
    memset(io->output, 0, sizeof(io->output)); io->killed = 0;
    if (p->stage == PT_R2_SHADER_US)
        for (unsigned j = 0; j < 4; ++j) io->output[j][3] = 1.0f;
    if (p->stage == PT_R2_SHADER_US)
        memcpy(t, io->input, iface->input_count * sizeof(t[0]));
    for (unsigned k = 0; k < p->step_count; ++k) {
        const PtR2ShaderStep *s = &p->step[k];
        if (s->kind == PT_R2_STEP_TEX) {
            float coordinate[4], result[4];
            memcpy(coordinate, t[s->u.tex.src], sizeof(coordinate));
            if (s->u.tex.op == 2) {
                for (unsigned j = 0; j < 4; ++j) if (pt_r2_fp_in(coordinate[j],profile) < 0.0f) io->killed = 1;
                if (io->killed) return PT_R2_SHADER_OK;
            } else {
                for (unsigned j = 0; j < 4; ++j) coordinate[j] = pt_r2_fp_in(coordinate[j],profile);
                if (!sample(context, s->u.tex.unit, s->u.tex.op, coordinate, result)) {
                    if (e) { memset(e, 0, sizeof(*e)); e->status = PT_R2_SHADER_INTERFACE;
                        e->word = s->word; e->raw_count = 1; e->raw[0] = p->words[s->word];
                        snprintf(e->reason, sizeof(e->reason), "texture callback rejected sample"); }
                    return PT_R2_SHADER_INTERFACE;
                }
                memcpy(t[s->u.tex.dst], result, sizeof(result));
            }
        } else if (s->kind == PT_R2_STEP_US) {
            const PtR2ShaderUS *u = &s->u.us;
            float r[3][4] = {{0}}, a[3][4] = {{0}}, rr[4] = {0}, ar[4] = {0};
            for (unsigned j = 0; j < 3; ++j) {
                operand(&u->rgb_source[j], io, t, cs, as, ps, pa, a0, cs[j]);
                operand(&u->alpha_source[j], io, t, cs, as, ps, pa, a0, as[j]);
            }
            for (unsigned j = 0; j < 3; ++j) ps[j] = presub(u->rgb_presub, cs[0][j], cs[1][j],profile);
            pa[3] = presub(u->alpha_presub, as[0][3], as[1][3],profile);
            for (unsigned j = 0; j < 3; ++j) {
                operand(&u->rgb_arg[j], io, t, cs, as, ps, pa, a0, r[j]);
                operand(&u->alpha_arg[j], io, t, cs, as, ps, pa, a0, a[j]);
            }
            float dp = u->rgb_op == PT_R2_OP_D2A ?
                pt_r2_fp_add(pt_r2_fp_add(pt_r2_fp_mul(r[0][0],r[1][0],profile),pt_r2_fp_mul(r[0][1],r[1][1],profile),profile),r[2][2],profile) : dot3(r[0], r[1],profile);
            if (u->rgb_op == PT_R2_OP_DP4) dp=pt_r2_fp_add(dp,pt_r2_fp_mul(a[0][0],a[1][0],profile),profile);
            arithmetic(u->alpha_op, 1, a[0], a[1], a[2], dp, 0.0f, ar,profile);
            arithmetic(u->rgb_op, 3, r[0], r[1], r[2], dp, ar[0], rr,profile);
            static const float scale[7]={0.125f,0.25f,0.5f,1.0f,2.0f,4.0f,8.0f};
            for (unsigned j = 0; j < 3; ++j) {
                rr[j] = pt_r2_fp_mul(rr[j],scale[u->rgb_scale+3],profile);
                if (u->rgb_clamp) rr[j] = saturated(rr[j]);
            }
            ar[0] = pt_r2_fp_mul(ar[0],scale[u->alpha_scale+3],profile);
            if (u->alpha_clamp) ar[0] = saturated(ar[0]);
            /* Commit only after BOTH halves read and finish computing. */
            for (unsigned j = 0; j < 3; ++j) {
                if (u->rgb_mask & (1u << j)) t[u->rgb_temp][j] = rr[j];
                if (u->rgb_output_mask & (1u << j)) io->output[u->rgb_output][j] = rr[j];
            }
            if (u->alpha_mask) t[u->alpha_temp][3] = ar[0];
            if (u->alpha_output_mask) io->output[u->alpha_output][3] = ar[0];
            if (u->depth_mask) io->depth = ar[0];
        } else {
            const PtR2ShaderPVS *v = &s->u.pvs;
            float args[3][4] = {{0}}, result[4] = {0};
            for (unsigned j = 0; j < 3; ++j) operand(&v->arg[j], io, t, cs, as, ps, pa, a0, args[j]);
            unsigned lanes = v->arg[0].type;
            float dp = pt_r2_fp_add(dot3(args[0], args[1],profile),pt_r2_fp_mul(args[0][3],args[1][3],profile),profile);
            if (lanes == 1 && (v->op == PT_R2_OP_LOG2 || v->op == PT_R2_OP_RSQ)) args[0][0] = fabsf(args[0][0]);
            if (v->op == PT_R2_OP_LIT) {
                float a = pt_r2_fp_in(args[0][0],profile), b = pt_r2_fp_in(args[1][0],profile), c = pt_r2_fp_in(args[2][0],profile);
                c = c < -128.0f ? -128.0f : c > 128.0f ? 128.0f : c;
                result[0] = result[3] = 1.0f; result[1] = b > 0.0f ? b : 0.0f;
                result[2] = b > 0.0f ? pt_r2_fp_out(powf(a > 0.0f ? a : 0.0f, c),profile) : 0.0f;
            } else {
                arithmetic(v->op, lanes, args[0], args[1], args[2], dp, 0.0f, result,profile);
                if (lanes == 1) for (unsigned j = 1; j < 4; ++j) result[j] = result[0];
            }
            if (v->clamp) for (unsigned j = 0; j < 4; ++j) result[j] = saturated(result[j]);
            float m[4] = {0};
            if (v->dual_op != PT_R2_OP_NOP) {
                float x[4] = {args[2][0]}, y[4] = {args[2][1]};
                arithmetic(v->dual_op, 1, x, y, y, 0.0f, 0.0f, m, profile);
                if (v->dual_clamp) m[0] = saturated(m[0]);
            }
            /* A0 holds integers; NaN and anything beyond +-2^20 saturate (to -2^20 for NaN), so any index stays out of 0..255. */
            if (v->dst_bank == PT_R2_ADDR) for (unsigned j = 0; j < 4; ++j) if (v->mask & (1u << j)) {
                float x = result[j]; a0[j] = !(x >= -1048576.0f) ? -1048576 : x > 1048576.0f ? 1048576 : (int32_t)x; }
            float *dst = v->dst_bank == PT_R2_TEMP ? t[v->dst] : v->dst_bank == PT_R2_ALT_TEMP ? t[128 + v->dst] : io->output[v->dst];
            if (v->dst_bank != PT_R2_ADDR) for (unsigned j = 0; j < 4; ++j) if (v->mask & (1u << j)) dst[j] = result[v->replicate_x ? 0 : j];
            if (v->dual_op != PT_R2_OP_NOP) t[128 + v->dual_dst][v->dual_lane] = m[0];
        }
    }
    return PT_R2_SHADER_OK;
}
