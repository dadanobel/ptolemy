/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Encodings: AMD R3xx 3D Register Reference, US section; AMD R5xx
 * Acceleration 1.5, section 7.5 (R300 straight-line PVS subset). */
#include "shader_ir.h"
#include <stdio.h>
#include <string.h>

#define IR_TAG 0x52325331u
static PtR2ShaderStatus fail(PtR2ShaderError *e, PtR2ShaderStatus s,
    const uint32_t *w, size_t n, size_t at, const char *why)
{
    if (e) {
        memset(e, 0, sizeof(*e)); e->status = s; e->word = (uint32_t)at;
        for (size_t k = 0; k < 4 && at < n && k < n - at; ++k)
            e->raw[e->raw_count++] = w[at + k];
        snprintf(e->reason, sizeof(e->reason), "%s", why);
    }
    return s;
}
static PtR2ShaderOperand ref(unsigned type, unsigned bank, unsigned index)
{
    PtR2ShaderOperand r = {0};
    r.type = (uint8_t)type; r.bank = (uint8_t)bank; r.index = (uint8_t)index;
    for (unsigned k = 0; k < 4; ++k) r.swizzle[k] = (uint8_t)k;
    return r;
}
static PtR2ShaderOperand us_select(unsigned sel, unsigned mod, bool rgb)
{
    PtR2ShaderOperand r = ref(rgb ? PT_R2_F32X3 : PT_R2_F32, PT_R2_ZERO, 0);
    unsigned comp = 0;
    if (rgb) {
        if (sel < 12) { r.bank = PT_R2_CS; r.index = sel / 4; comp = sel % 4; }
        else if (sel < 15) { r.bank = PT_R2_AS; r.index = sel - 12; comp = 4; }
        else if (sel < 19) { r.bank = PT_R2_PRESUB_RGB; comp = sel - 15; }
        else if (sel == 19) { r.bank = PT_R2_PRESUB_ALPHA; comp = 4; }
        else if (sel <= 22) r.bank = (uint8_t)(PT_R2_ZERO + sel - 20);
        else if (sel < 29) {
            r.bank = PT_R2_CS; r.index = (sel - 23) % 3;
            for (unsigned k = 0; k < 3; ++k) r.swizzle[k] = (uint8_t)((k + (sel < 26 ? 1 : 2)) % 3);
            comp = 5;
        } else { r.bank = PT_R2_CS; r.index = sel - 29; comp = 6; }
        if (comp > 0 && comp < 4)
            for (unsigned k = 0; k < 3; ++k) r.swizzle[k] = (uint8_t)(comp - 1);
        if (comp == 4) for (unsigned k = 0; k < 3; ++k) r.swizzle[k] = 3;
        /* WZY's W comes from the alpha source bank with the same index. */
        if (comp == 6) { r.swizzle[0] = 6; r.swizzle[1] = 2; r.swizzle[2] = 1; }
    } else {
        if (sel < 9) { r.bank = PT_R2_CS; r.index = sel / 3; r.swizzle[0] = sel % 3; }
        else if (sel < 12) { r.bank = PT_R2_AS; r.index = sel - 9; r.swizzle[0] = 3; }
        else if (sel < 15) { r.bank = PT_R2_PRESUB_RGB; r.swizzle[0] = sel - 12; }
        else if (sel == 15) { r.bank = PT_R2_PRESUB_ALPHA; r.swizzle[0] = 3; }
        else r.bank = (uint8_t)(PT_R2_ZERO + sel - 16);
    }
    r.absolute = (uint8_t)((mod >> 1) & 1);
    r.negate = (mod & 1) ? (uint8_t)((1u << r.type) - 1) : 0;
    return r;
}
static PtR2ShaderStatus decode_us(PtR2ShaderIR *p, PtR2ShaderError *e)
{
    const uint32_t *w = p->words; size_t n = p->word_count;
    if (n < 7) return fail(e, PT_R2_SHADER_LENGTH, w, n, 0, "US header is seven words");
    unsigned co = w[2], a0 = co & 63, na = ((co >> 6) & 63) + 1;
    unsigned t0 = (co >> 13) & 31, nt = ((co >> 18) & 31) + 1;
    if ((w[0] & ~11u) || (co & ~0x007fefffu))
        return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, 0, "non-R300 US configuration/offset bits");
    if (a0 + na > 64 || t0 + nt > 32 || n != 7 + nt + 4 * na)
        return fail(e, PT_R2_SHADER_LENGTH, w, n, 2, "US active bank range/length mismatch");
    if (w[1] > 31) return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, 1, "US pixel stack above 32 registers");
    unsigned nodes = (w[0] & 3) + 1;
    static const uint8_t rgb_op[11] = {PT_R2_OP_MAD,PT_R2_OP_DP3,PT_R2_OP_DP4,
        PT_R2_OP_D2A,PT_R2_OP_MIN,PT_R2_OP_MAX,255,PT_R2_OP_CND,
        PT_R2_OP_CMP,PT_R2_OP_FRC,PT_R2_OP_SOP};
    static const uint8_t alpha_op[12] = {PT_R2_OP_MAD,PT_R2_OP_DP3,
        PT_R2_OP_MIN,PT_R2_OP_MAX,255,PT_R2_OP_CND,PT_R2_OP_CMP,
        PT_R2_OP_FRC,PT_R2_OP_EXP2,PT_R2_OP_LOG2,PT_R2_OP_RCP,PT_R2_OP_RSQ};
    for (unsigned node = 4 - nodes; node < 4; ++node) {
        uint32_t v = w[3 + node];
        unsigned as = v & 63, ac = ((v >> 6) & 63) + 1;
        unsigned ts = (v >> 12) & 31, tc = ((v >> 17) & 31) + 1;
        if (node == 4 - nodes && !(w[0] & 8)) tc = 0;
        if ((v & 0xff000000u) || as + ac > na || (tc && ts + tc > nt))
            return fail(e, PT_R2_SHADER_ENCODING, w, n, 3 + node, "US node exceeds captured banks");
        for (unsigned k = ts; tc && k < ts + tc; ++k) {
            size_t at = 7 + k; uint32_t t = w[at]; unsigned op = (t >> 15) & 7;
            if ((t & ~0x0003ffdfu) || op > 4)
                return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, at, "US texture encoding/opcode");
            if (!op) continue;
            PtR2ShaderStep *s = &p->step[p->step_count++];
            s->kind = PT_R2_STEP_TEX; s->word = (uint32_t)at;
            s->u.tex.op = (uint8_t)op; s->u.tex.src = t & 31;
            s->u.tex.dst = (t >> 6) & 31; s->u.tex.unit = (t >> 11) & 15;
            if (op != 2) p->textures |= 1u << s->u.tex.unit;
        }
        for (unsigned k = as; k < as + ac; ++k) {
            size_t at = 7 + nt + k;
            uint32_t ra = w[at], aa = w[at + na], ri = w[at + 2 * na], ai = w[at + 3 * na];
            unsigned rop = (ri >> 23) & 15, aop = (ai >> 23) & 15;
            if (rop >= 11 || aop >= 12 || rgb_op[rop] == 255 || alpha_op[aop] == 255 ||
                ((ri >> 27) & 7) == 7 || ((ai >> 27) & 7) == 7 ||
                (ra & 0x80000000u) || (aa & 0xf0000000u))
                return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, at, "US ALU opcode/modifier/address encoding");
            PtR2ShaderStep *s = &p->step[p->step_count++];
            s->kind = PT_R2_STEP_US; s->word = (uint32_t)at;
            PtR2ShaderUS *u = &s->u.us;
            u->rgb_op = rgb_op[rop]; u->alpha_op = alpha_op[aop];
            u->rgb_presub = (ri >> 21) & 3; u->alpha_presub = (ai >> 21) & 3;
            unsigned rm = (ri >> 27) & 7, am = (ai >> 27) & 7;
            u->rgb_scale = (int8_t)(rm < 4 ? (int)rm : 3 - (int)rm);
            u->alpha_scale = (int8_t)(am < 4 ? (int)am : 3 - (int)am);
            u->rgb_clamp = (ri >> 30) & 1; u->alpha_clamp = (ai >> 30) & 1;
            for (unsigned j = 0; j < 3; ++j) {
                unsigned r = (ra >> (6 * j)) & 63, a = (aa >> (6 * j)) & 63;
                u->rgb_source[j] = ref(4, r & 32 ? PT_R2_CONSTANT : PT_R2_TEMP, r & 31);
                u->alpha_source[j] = ref(4, a & 32 ? PT_R2_CONSTANT : PT_R2_TEMP, a & 31);
                unsigned sel = (ai >> (7 * j)) & 31;
                if (sel > 18) return fail(e, PT_R2_SHADER_ENCODING, w, n, at + 3 * na, "reserved US alpha selector");
                u->rgb_arg[j] = us_select((ri >> (7 * j)) & 31, (ri >> (7 * j + 5)) & 3, true);
                u->alpha_arg[j] = us_select(sel, (ai >> (7 * j + 5)) & 3, false);
            }
            u->rgb_temp = (ra >> 18) & 31; u->rgb_mask = (ra >> 23) & 7;
            u->rgb_output = (ra >> 29) & 3; u->rgb_output_mask = (ra >> 26) & 7;
            u->alpha_temp = (aa >> 18) & 31; u->alpha_mask = (aa >> 23) & 1;
            u->alpha_output = (aa >> 25) & 3; u->alpha_output_mask = (aa >> 24) & 1;
            u->depth_mask = (aa >> 27) & 1;
            if (u->rgb_output_mask) p->outputs |= 1u << u->rgb_output;
            if (u->alpha_output_mask) p->outputs |= 1u << u->alpha_output;
            p->writes_depth |= u->depth_mask;
        }
    }
    return PT_R2_SHADER_OK;
}
/* Math engine opcodes. Only the IEEE variants share US operation semantics; DX/FF have their own zero rules. */
static uint8_t math_op(unsigned op)
{
    switch (op) {
    case 0: return PT_R2_OP_NOP;
    /* The DX partial-precision EXP/LOG (1, 2) and LOG_FULL (12) as their full-precision forms, EXP_BASEE_FF (3; its
     * operand in the third source, as Tiger's fog programs place it), POWER_FUNC_FF (5; base in the first source, exponent
     * in the third), its clamped forms CLAMP_B and CLAMP_B1 (13, 14; Approximation: unclamped) and CLAMP_01 (15), SIN and
     * COS (16, 17). Halo's PVS programs use them. */
    case 1: return PT_R2_OP_EXP2;
    case 2: case 12: return PT_R2_OP_LOG2;
    case 3: return PT_R2_OP_EXPE;
    case 5: case 13: case 14: return PT_R2_OP_POW;
    case 15: return PT_R2_OP_POW01;
    case 16: return PT_R2_OP_SIN;
    case 17: return PT_R2_OP_COS;
    case 4: return PT_R2_OP_LIT;
    case 6: return PT_R2_OP_RCP_DX;
    case 7: return PT_R2_OP_RCP_FF;
    case 8: return PT_R2_OP_RSQ_DX;
    case 9: return PT_R2_OP_RSQ_FF;
    case 10: return PT_R2_OP_MUL;
    case 11: return PT_R2_OP_EXP2;
    case 18: return PT_R2_OP_LOG2;
    case 19: return PT_R2_OP_RCP;
    case 20: return PT_R2_OP_RSQ;
    default: return 255;
    }
}
/* REG_TYPE 1:0 (temporary, input, constant, alternate temporary), ABS 3, OFFSET 12:5. ADDR_MODE is bit 31 (high) and
 * bit 4 (low): 0 absolute, 1 relative to the A0 component ADDR_SEL 30:29 (R5xx Acceleration 7.5); relative is accepted
 * for constants only, the loop-index mode not at all. */
static bool pvs_source(uint32_t x, unsigned type, PtR2ShaderOperand *r)
{
    static const uint8_t bank[4] = {PT_R2_TEMP, PT_R2_INPUT, PT_R2_CONSTANT, PT_R2_ALT_TEMP};
    static const unsigned limit[4] = {128, 32, 256, 20};
    unsigned b = x & 3, idx = (x >> 5) & 255, relative = (x >> 4) & 1;
    if ((x & 0x80000000u) || (relative ? b != 2 : (x & 0x60000000u) != 0) || idx >= limit[b]) return false;
    *r = ref(type, bank[b], idx); r->absolute = (x >> 3) & 1;
    if (relative) r->relative = (uint8_t)(1 + ((x >> 29) & 3));
    return true;
}
static PtR2ShaderStatus decode_pvs(PtR2ShaderIR *p, PtR2ShaderError *e)
{
    const uint32_t *w = p->words; size_t n = p->word_count;
    if (n < 2) return fail(e, PT_R2_SHADER_LENGTH, w, n, 0, "PVS header is two words");
    unsigned first = w[0] & 1023, last = (w[0] >> 20) & 1023;
    if (last < first || last >= 256 || n != 2 + 4 * (last - first + 1))
        return fail(e, PT_R2_SHADER_LENGTH, w, n, 0, "PVS instruction range/length mismatch");
    if ((w[0] >> 30) || (w[1] & ~1023u))
        return fail(e, PT_R2_SHADER_ENCODING, w, n, 0, "reserved PVS code-control bits");
    static const uint8_t vector_op[15] = {PT_R2_OP_NOP,PT_R2_OP_DP4,PT_R2_OP_MUL,
        PT_R2_OP_ADD,PT_R2_OP_MAD,PT_R2_OP_DST,PT_R2_OP_FRC,PT_R2_OP_MAX,
        PT_R2_OP_MIN,PT_R2_OP_SGE,PT_R2_OP_SLT,PT_R2_OP_M2ADD,255,PT_R2_OP_ARL,PT_R2_OP_ARR};
    for (size_t at = 2; at < n; at += 4) {
        uint32_t d = w[at]; unsigned op = d & 63, dst = (d >> 8) & 15;
        bool math = (d & 64) != 0, dual = (d >> 28) & 1, macro = (d & 128) != 0;
        /* Destination types: temporary 0, A0 1 (FLT2FIX only), output 2, output with X replicated 3, alternate temporary
         * 4. A dual-math step pairs a vector op with the math op in its third source word; its vector half cannot write
         * ATRM. A macro step is MAD (0) or M2ADD (1) with three distinct temporaries, which the hardware splits into two
         * clocks. */
        if ((d & 0xec001000u) || dst > 4 || (dual && (math || macro || dst == 4 || dst == 1)) || (macro && (math || op > 1)))
            return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, at, "PVS macro/predication/relative destination/type");
        uint8_t typed_op = macro ? (op ? PT_R2_OP_M2ADD : PT_R2_OP_MAD) : math ? math_op(op) : op < 15 ? vector_op[op] : 255;
        if (typed_op == 255) return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, at, "PVS opcode not implemented in P1a");
        if ((dst == 1) != (typed_op == PT_R2_OP_ARL || typed_op == PT_R2_OP_ARR) || (dst == 1 && ((d >> 13) & 127)))
            return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, at, "PVS FLT2FIX other than into A0, or A0 from another op");
        if (dual && (typed_op == PT_R2_OP_MAD || typed_op == PT_R2_OP_M2ADD))
            return fail(e, PT_R2_SHADER_ENCODING, w, n, at, "PVS dual-math vector op with three sources");
        PtR2ShaderStep *s = &p->step[p->step_count++];
        s->kind = PT_R2_STEP_PVS; s->word = (uint32_t)at;
        PtR2ShaderPVS *v = &s->u.pvs; v->op = typed_op;
        v->dst_bank = dst == 0 ? PT_R2_TEMP : dst == 4 ? PT_R2_ALT_TEMP : dst == 1 ? PT_R2_ADDR : PT_R2_OUTPUT;
        v->dst = (d >> 13) & 127; v->mask = (d >> 20) & 15;
        v->replicate_x = dst == 3; v->clamp = (d >> (math ? 25 : 24)) & 1;
        if (v->dst >= (dst == 4 ? 20 : dst != 0 ? 32 : 128))
            return fail(e, PT_R2_SHADER_ENCODING, w, n, at, "PVS output index exceeds OVM/ATRM");
        if ((dst == 2 || dst == 3) && v->mask) p->outputs |= 1u << v->dst;
        /* Operands the op does not read (the driver fills them with copies of others, observed: DP4/ADD naming input 18,
         * input 68) are zero: neither decoded nor counted by the interface check or the input/constant counts. */
        unsigned used = typed_op == PT_R2_OP_NOP ? 0 : typed_op == PT_R2_OP_MAD || typed_op == PT_R2_OP_M2ADD || typed_op == PT_R2_OP_LIT || typed_op == PT_R2_OP_POW || typed_op == PT_R2_OP_POW01 ? 3 :
            typed_op == PT_R2_OP_FRC || typed_op == PT_R2_OP_ARL || typed_op == PT_R2_OP_ARR || (math && typed_op != PT_R2_OP_MUL) ? 1 : 2;
        for (unsigned j = 0; j < (dual ? 2u : 3u); ++j) {
            uint32_t x = w[at + 1 + (j == 0 && !dual && typed_op == PT_R2_OP_EXPE ? 2 : j)];
            if (j >= used) { v->arg[j] = ref(math ? PT_R2_F32 : PT_R2_F32X4, PT_R2_ZERO, 0); continue; }
            PtR2ShaderOperand *r = &v->arg[j];
            if (!pvs_source(x, math ? PT_R2_F32 : PT_R2_F32X4, r))
                return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, at + 1 + j, "PVS relative source or out-of-range register");
            for (unsigned k = 0; k < 4; ++k) {
                unsigned sw = (x >> (13 + 3 * k)) & 7;
                if (sw > 5) return fail(e, PT_R2_SHADER_ENCODING, w, n, at + 1 + j, "reserved PVS swizzle");
                r->swizzle[k] = (uint8_t)sw;
            }
            if (math) { r->swizzle[0] = r->swizzle[3]; r->negate = (x >> 28) & 1; }
            else r->negate = (x >> 25) & 15;
        }
        if (dual) {
            /* The dual math word: source 12:0 as above, SWIZZLE_X 15:13, SWIZZLE_Y 18:16, ATRM destination 20:19,
             * opcode 24:21 with its MSB in bit 2, NEGATE_X 25, NEGATE_Y 26, write lane 28:27. */
            uint32_t x = w[at + 3]; unsigned sx = (x >> 13) & 7, sy = (x >> 16) & 7;
            v->dual_op = math_op(((x >> 21) & 15) | ((x >> 2) & 1) << 4);
            if (v->dual_op == 255 || v->dual_op == PT_R2_OP_LIT || v->dual_op == PT_R2_OP_POW || v->dual_op == PT_R2_OP_POW01 || !pvs_source(x, PT_R2_F32X4, &v->arg[2]) ||
                v->arg[2].relative || sx > 5 || sy > 5)
                return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, at + 3, "PVS dual math opcode/source not implemented");
            v->arg[2].swizzle[0] = (uint8_t)sx;
            for (unsigned k = 1; k < 4; ++k) v->arg[2].swizzle[k] = (uint8_t)sy;
            v->arg[2].negate = (x >> 25) & 3;
            v->dual_dst = (x >> 19) & 3; v->dual_lane = (x >> 27) & 3; v->dual_clamp = (d >> 25) & 1;
        }
    }
    return PT_R2_SHADER_OK;
}
PtR2ShaderStatus pt_r2_shader_decode(PtR2ShaderStage stage,
    PtR2ShaderPrecision precision, const uint32_t *w, size_t n,
    uint32_t flow, PtR2ShaderIR *p, PtR2ShaderError *e)
{
    if (e) memset(e, 0, sizeof(*e));
    if (!p || !w || !n || n > PT_R2_SHADER_WORDS)
        return fail(e, PT_R2_SHADER_ARGUMENT, NULL, 0, 0, "null input or word-count bound");
    memset(p, 0, sizeof(*p));
    if ((precision != PT_R2_SHADER_FP32_SEPARATE && precision != PT_R2_SHADER_FP32_FTZ0) ||
        (stage != PT_R2_SHADER_US && stage != PT_R2_SHADER_PVS))
        return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, 0, "unknown stage or precision profile");
    if (flow) return fail(e, PT_R2_SHADER_UNSUPPORTED, w, n, 0, "PVS flow control is not straight-line");
    p->stage = stage; p->precision = precision; p->word_count = (uint32_t)n;
    memcpy(p->words, w, n * sizeof(*w));
    PtR2ShaderStatus s = stage == PT_R2_SHADER_US ? decode_us(p, e) : decode_pvs(p, e);
    if (s == PT_R2_SHADER_OK) p->tag = IR_TAG;
    return s;
}
PtR2ShaderStatus pt_r2_shader_validate(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *i, PtR2ShaderError *e)
{
    if (!p || !i || p->tag != IR_TAG || p->word_count > PT_R2_SHADER_WORDS || p->step_count > PT_R2_SHADER_STEPS)
        return fail(e, PT_R2_SHADER_IR, NULL, 0, 0, "invalid IR header");
    /* Re-decoding is bounded and also validates every typed operand, mask,
     * tag and inactive byte. This keeps hand-crafted/corrupted IR from
     * bypassing the raw-word trust boundary in interpreter or emitter. */
    PtR2ShaderIR expected;
    PtR2ShaderStatus s = pt_r2_shader_decode((PtR2ShaderStage)p->stage,
        (PtR2ShaderPrecision)p->precision, p->words, p->word_count, 0, &expected, e);
    if (s != PT_R2_SHADER_OK) return s;
    if (memcmp(p, &expected, sizeof(*p)))
        return fail(e, PT_R2_SHADER_IR, p->words, p->word_count, 0, "IR differs from canonical decoded program");
    return pt_r2_shader_check_interface(p, i, e);
}
PtR2ShaderStatus pt_r2_shader_check_interface(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *i, PtR2ShaderError *e)
{
    if (!p || !i || p->tag != IR_TAG || p->step_count > PT_R2_SHADER_STEPS)
        return fail(e, PT_R2_SHADER_IR, NULL, 0, 0, "invalid IR header");
    if (i->abi != PT_R2_SHADER_ABI || i->input_count > 32 || !i->output_count || i->output_count > 32 ||
        i->constant_count > 256 || i->semantic_count > PT_R2_SHADER_INTERFACE_WORDS ||
        (i->texture_mask & ~65535u) || (p->textures & ~i->texture_mask) ||
        (i->output_count < 32 && (p->outputs >> i->output_count)))
        return fail(e, PT_R2_SHADER_INTERFACE, p->words, p->word_count, 0, "interface ABI/count/binding mismatch");
    for (unsigned u = 0; u < 16; ++u)
        if ((i->texture_mask & (1u << u)) && i->texture_dimension[u] > 2)
            return fail(e, PT_R2_SHADER_INTERFACE, p->words, p->word_count, 0, "unsupported texture dimension");
    for (unsigned k = 0; k < p->step_count; ++k) {
        const PtR2ShaderStep *st = &p->step[k];
        const PtR2ShaderOperand *args = st->kind == PT_R2_STEP_PVS ? st->u.pvs.arg : NULL;
        unsigned count = args ? 3 : 0;
        if (st->kind == PT_R2_STEP_US) { args = st->u.us.rgb_source; count = 6; }
        for (unsigned a = 0; a < count; ++a) {
            /* rgb_source/alpha_source are distinct arrays: do not walk
             * off an array object even though the structs are adjacent. */
            const PtR2ShaderOperand *r = st->kind == PT_R2_STEP_US ?
                (a < 3 ? &st->u.us.rgb_source[a] : &st->u.us.alpha_source[a - 3]) : &args[a];
            if ((r->bank == PT_R2_CONSTANT && r->index >= i->constant_count) ||
                (r->bank == PT_R2_INPUT && r->index >= i->input_count))
                return fail(e, PT_R2_SHADER_INTERFACE, p->words, p->word_count, st->word, "source register absent from interface");
        }
    }
    if (e) memset(e, 0, sizeof(*e));
    return PT_R2_SHADER_OK;
}
PtR2ShaderStatus pt_r2_shader_key(const PtR2ShaderIR *p,
    const PtR2ShaderInterface *i, void *data, size_t cap, size_t *needed,
    PtR2ShaderError *e)
{
    PtR2ShaderStatus s = pt_r2_shader_validate(p, i, e);
    if (s != PT_R2_SHADER_OK) return s;
    size_t n = 4 * (10 + 16 + i->semantic_count + p->word_count);
    if (needed) *needed = n;
    if (!data || cap < n) return fail(e, PT_R2_SHADER_BUFFER, NULL, 0, 0, "cache-key buffer too small");
    uint8_t *b = data; size_t at = 0;
    uint32_t head[10] = {IR_TAG, p->stage, p->precision, p->word_count, i->abi,
        i->input_count, i->output_count, i->constant_count, i->texture_mask, i->semantic_count};
    for (size_t k = 0; k < 10 + 16 + i->semantic_count + p->word_count; ++k) {
        uint32_t v = k < 10 ? head[k] : k < 26 ?
            ((i->texture_mask & (1u << (k - 10))) ? i->texture_dimension[k - 10] : 0) :
            k < 26 + i->semantic_count ? i->semantic[k - 26] : p->words[k - 26 - i->semantic_count];
        for (unsigned j = 0; j < 4; ++j) b[at++] = (uint8_t)(v >> (8 * j));
    }
    return PT_R2_SHADER_OK;
}
