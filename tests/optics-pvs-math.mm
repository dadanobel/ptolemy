/* SPDX-License-Identifier: GPL-2.0-or-later */
/* PVS math-engine forms of Tiger's Radeon driver: RCP_DX/RSQ_DX, a dual-math step into the alternate temporaries, ATRM
 * sources and an ATRM destination (seen in Doom 3's interaction programs); RCP_FF/RSQ_FF, LIGHT_COEFF_DX and the
 * two-clock macro MAD (Cinebench's fixed-function lighting); EXP/LOG DX, EXP_BASEE_FF, POWER_FUNC_FF (base in the first
 * source, exponent in the third), SIN and COS; FLT2FIX into A0 (floor and rounded) and constants addressed relative to
 * A0.x and A0.y, also in a math op, an index past 255 reading 0 (Halo's skinning form). A synthetic program (no guest
 * words): the CPU interpreter against expected values, then the emitted MSL on Metal against the interpreter. Operands
 * an op does not read (i30, i31 here) must not count against the one-input interface. */
#import <Metal/Metal.h>
extern "C" {
#include "shader_ir.h"
}
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t D(unsigned op, unsigned math, unsigned type, unsigned off, unsigned dual)
{ return op | math << 6 | type << 8 | off << 13 | 15u << 20 | dual << 28; }
static uint32_t MACRO(unsigned op, unsigned type, unsigned off) { return D(op, 0, type, off, 0) | 1u << 7; }
static uint32_t S(unsigned bank, unsigned off, unsigned swizzle, unsigned negate)
{ return bank | off << 5 | swizzle << 13 | negate << 25; }
static uint32_t M(unsigned bank, unsigned off, unsigned sx, unsigned sy, unsigned atrm, unsigned op, unsigned negate_x, unsigned lane)
{ return bank | off << 5 | sx << 13 | sy << 16 | atrm << 19 | (op & 15) << 21 | (op >> 4 & 1) << 2 | negate_x << 25 | lane << 27; }
#define XYZW (0 | 1 << 3 | 2 << 6 | 3 << 9)
#define ALL(c) ((c) | (c) << 3 | (c) << 6 | (c) << 9)
static uint32_t REL(uint32_t source, unsigned lane) { return source | 1u << 4 | lane << 29; }
enum { R_T = 0, R_IN = 1, R_K = 2, R_A = 3, D_T = 0, D_A0 = 1, D_OUT = 2, D_ATRM = 4, VE_ARL = 13, VE_ARR = 14, VE_MUL = 2, VE_ADD = 3, VE_MAD = 4, VE_MAX = 7, ME_LIT = 4,
       ME_RCP_DX = 6, ME_RCP_FF = 7, ME_RSQ_DX = 8, ME_RSQ_FF = 9, ME_MUL = 10, MACRO_MAD = 0,
       ME_EXP_DX = 1, ME_LOG_DX = 2, ME_EXPE_FF = 3, ME_POW_FF = 5, ME_SIN = 16, ME_COS = 17, OUTS = 21 };
static bool near(float a, float b)
{
    uint32_t x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4);
    return x == y || (isfinite(a) && isfinite(b) && (x ^ y) < 0x80000000u && (x > y ? x - y : y - x) <= 3);
}
int main(void) { @autoreleasepool {
    const uint32_t w[2 + 4 * 28] = { 27u << 20, 0,
        /* t0 = i0 || at0.y = RSQ_DX(-i0.x) */  D(VE_ADD, 0, D_T, 0, 1), S(R_IN, 0, XYZW, 0), S(R_K, 1, XYZW, 0), M(R_IN, 0, 0, 0, 0, ME_RSQ_DX, 1, 1),
        /* t0 = 2 t0 || at1.z = RCP_DX(t0.x as before the step) */ D(VE_MUL, 0, D_T, 0, 1), S(R_T, 0, XYZW, 0), S(R_K, 2, XYZW, 0), M(R_T, 0, 0, 0, 1, ME_RCP_DX, 0, 2),
        /* o0 = at1.zzzz */                     D(VE_MAX, 0, D_OUT, 0, 0), S(R_A, 1, ALL(2), 0), S(R_A, 1, ALL(2), 0), S(R_K, 1, XYZW, 0),
        /* o1 = t0 * 1 + 0 */                   D(VE_MAD, 0, D_OUT, 1, 0), S(R_T, 0, XYZW, 0), S(R_K, 0, XYZW, 0), S(R_K, 1, XYZW, 0),
        /* o2 = RSQ_DX(i0.w); unread i31 */     D(ME_RSQ_DX, 1, D_OUT, 2, 0), S(R_IN, 0, ALL(3), 0), S(R_IN, 0, ALL(3), 0), S(R_IN, 31, ALL(3), 0),
        /* o3 = at0.yyyy || at2.w = i0.y i0.z */ D(VE_ADD, 0, D_OUT, 3, 1), S(R_A, 0, ALL(1), 0), S(R_K, 1, XYZW, 0), M(R_IN, 0, 1, 2, 2, ME_MUL, 0, 3),
        /* at3 = i0 + 1 */                      D(VE_ADD, 0, D_ATRM, 3, 0), S(R_IN, 0, XYZW, 0), S(R_K, 0, XYZW, 0), S(R_K, 0, XYZW, 0),
        /* o4 = at3; unread i30 */              D(VE_ADD, 0, D_OUT, 4, 0), S(R_A, 3, XYZW, 0), S(R_K, 1, XYZW, 0), S(R_IN, 30, XYZW, 0),
        /* o5 = at2.wwww */                     D(VE_ADD, 0, D_OUT, 5, 0), S(R_A, 2, ALL(3), 0), S(R_K, 1, XYZW, 0), S(R_K, 1, XYZW, 0),
        /* t2 = i0 + 1 */                       D(VE_ADD, 0, D_T, 2, 0), S(R_IN, 0, XYZW, 0), S(R_K, 0, XYZW, 0), S(R_K, 0, XYZW, 0),
        /* t3 = i0 + 2 */                       D(VE_ADD, 0, D_T, 3, 0), S(R_IN, 0, XYZW, 0), S(R_K, 2, XYZW, 0), S(R_K, 0, XYZW, 0),
        /* o6 = t0 t2 + t3, macro (3 temps) */  MACRO(MACRO_MAD, D_OUT, 6), S(R_T, 0, XYZW, 0), S(R_T, 2, XYZW, 0), S(R_T, 3, XYZW, 0),
        /* o7 = RCP_FF(i0.x) */                 D(ME_RCP_FF, 1, D_OUT, 7, 0), S(R_IN, 0, ALL(0), 0), S(R_IN, 29, ALL(0), 0), S(R_IN, 29, ALL(0), 0),
        /* o8 = 0 || at0.z = RSQ_FF(i0.w) */    D(VE_ADD, 0, D_OUT, 8, 1), S(R_K, 1, XYZW, 0), S(R_K, 1, XYZW, 0), M(R_IN, 0, 3, 3, 0, ME_RSQ_FF, 0, 2),
        /* o9 = at0.zzzz */                     D(VE_ADD, 0, D_OUT, 9, 0), S(R_A, 0, ALL(2), 0), S(R_K, 1, XYZW, 0), S(R_K, 1, XYZW, 0),
        /* o10 = LIT(A i0.y, B i0.x, C 2) */    D(ME_LIT, 1, D_OUT, 10, 0), S(R_IN, 0, ALL(1), 0), S(R_IN, 0, ALL(0), 0), S(R_K, 2, ALL(3), 0),
        /* o11 = EXP_DX(i0.y) */                D(ME_EXP_DX, 1, D_OUT, 11, 0), S(R_IN, 0, ALL(1), 0), S(R_IN, 29, ALL(1), 0), S(R_IN, 29, ALL(1), 0),
        /* o12 = LOG_DX(i0.w) */                D(ME_LOG_DX, 1, D_OUT, 12, 0), S(R_IN, 0, ALL(3), 0), S(R_IN, 29, ALL(3), 0), S(R_IN, 29, ALL(3), 0),
        /* o13 = EXPE_FF(i0.z) */               D(ME_EXPE_FF, 1, D_OUT, 13, 0), S(R_IN, 0, ALL(2), 0), S(R_IN, 29, ALL(2), 0), S(R_IN, 29, ALL(2), 0),
        /* o14 = POW_FF(i0.y, 2) */             D(ME_POW_FF, 1, D_OUT, 14, 0), S(R_IN, 0, ALL(1), 0), S(R_K, 0, ALL(0), 0), S(R_K, 2, ALL(3), 0),
        /* o15 = SIN(i0.z) */                   D(ME_SIN, 1, D_OUT, 15, 0), S(R_IN, 0, ALL(2), 0), S(R_IN, 29, ALL(2), 0), S(R_IN, 29, ALL(2), 0),
        /* o16 = COS(i0.z) */                   D(ME_COS, 1, D_OUT, 16, 0), S(R_IN, 0, ALL(2), 0), S(R_IN, 29, ALL(2), 0), S(R_IN, 29, ALL(2), 0),
        /* A0.x = floor(i0.y) */                D(VE_ARL, 0, D_A0, 0, 0) & ~(14u << 20), S(R_IN, 0, ALL(1), 0), S(R_IN, 0, ALL(1), 0), S(R_IN, 0, ALL(1), 0),
        /* A0.y = floor(i0.z + 0.5) */          D(VE_ARR, 0, D_A0, 0, 0) & ~(13u << 20), S(R_IN, 0, ALL(2), 0), S(R_IN, 0, ALL(2), 0), S(R_IN, 0, ALL(2), 0),
        /* o17 = c[1 + A0.x] */                 D(VE_ADD, 0, D_OUT, 17, 0), REL(S(R_K, 1, XYZW, 0), 0), S(R_K, 1, XYZW, 0), S(R_K, 1, XYZW, 0),
        /* o18 = c[3 + A0.y] */                 D(VE_ADD, 0, D_OUT, 18, 0), REL(S(R_K, 3, XYZW, 0), 1), S(R_K, 1, XYZW, 0), S(R_K, 1, XYZW, 0),
        /* o19 = c[250 + A0.x], 0 past 255 */   D(VE_ADD, 0, D_OUT, 19, 0), REL(S(R_K, 250, XYZW, 0), 0), S(R_K, 1, XYZW, 0), S(R_K, 1, XYZW, 0),
        /* o20 = RCP_DX(c[1 + A0.x].w) */       D(ME_RCP_DX, 1, D_OUT, 20, 0), REL(S(R_K, 1, ALL(3), 0), 0), S(R_IN, 29, ALL(3), 0), S(R_IN, 29, ALL(3), 0) };
    {   /* refused: relative addressing of a temporary, the loop-index mode, FLT2FIX into a temporary */
        static PtR2ShaderIR bad; PtR2ShaderError be; const uint32_t one = REL(S(R_T, 0, XYZW, 0), 0), loop = S(R_K, 0, XYZW, 0) | 1u << 31;
        const uint32_t t1[6] = { 0, 0, D(VE_ADD, 0, D_OUT, 0, 0), one, S(R_K, 1, XYZW, 0), 0 },
                       t2[6] = { 0, 0, D(VE_ADD, 0, D_OUT, 0, 0), loop, S(R_K, 1, XYZW, 0), 0 },
                       t3[6] = { 0, 0, D(VE_ARL, 0, D_T, 0, 0), S(R_IN, 0, XYZW, 0), 0, 0 };
        assert(pt_r2_shader_decode(PT_R2_SHADER_PVS, PT_R2_SHADER_FP32_SEPARATE, t1, 6, 0, &bad, &be) == PT_R2_SHADER_UNSUPPORTED);
        assert(pt_r2_shader_decode(PT_R2_SHADER_PVS, PT_R2_SHADER_FP32_SEPARATE, t2, 6, 0, &bad, &be) == PT_R2_SHADER_UNSUPPORTED);
        assert(pt_r2_shader_decode(PT_R2_SHADER_PVS, PT_R2_SHADER_FP32_SEPARATE, t3, 6, 0, &bad, &be) == PT_R2_SHADER_UNSUPPORTED);
    }
    static PtR2ShaderIR ir; PtR2ShaderError e;
    if (pt_r2_shader_decode(PT_R2_SHADER_PVS, PT_R2_SHADER_FP32_SEPARATE, w, sizeof w / 4, 0, &ir, &e)) { printf("decode: %s at %u\n", e.reason, e.word); return 1; }
    PtR2ShaderInterface iface = {}; iface.abi = PT_R2_SHADER_ABI; iface.input_count = 1; iface.output_count = OUTS; iface.constant_count = 256;
    const float x[] = { 0.0f, -0.0f, 4.0f, -0.25f, 1e-30f, 7.0f, 1e30f, 3.0f }, ws[] = { 0.0f, -0.0f, 16.0f, -4.0f, 2.5f, 1e-30f, 0.0625f, 1e30f };
    enum { N = sizeof x / sizeof x[0] };
    PtR2ShaderIO *io = (PtR2ShaderIO *)calloc(N, sizeof *io), *cpu = (PtR2ShaderIO *)calloc(N, sizeof *cpu); assert(io && cpu);
    for (unsigned v = 0; v < N; ++v) {
        const float in[4] = { x[v], 1.5f + v, -0.5f * v, ws[v] };
        memcpy(io[v].input[0], in, 16);
        for (unsigned c = 0; c < 4; ++c) { io[v].constant[0][c] = 1.0f; io[v].constant[2][c] = 2.0f;
            for (unsigned k = 3; k < 256; ++k) io[v].constant[k][c] = k * 4 + c + 0.25f; }
        cpu[v] = io[v];
        if (pt_r2_shader_run(&ir, &iface, &cpu[v], NULL, NULL, &e)) { printf("run: %s\n", e.reason); return 1; }
        const float (*o)[4] = cpu[v].output;
        for (unsigned c = 0; c < 4; ++c) {
            assert(x[v] ? o[0][c] == 1.0f / x[v] : fabsf(o[0][c]) == FLT_MAX);   /* RCP_DX of the value before the step */
            assert(o[1][c] == 2.0f * in[c] && o[4][c] == in[c] + 1.0f && o[5][c] == in[1] * in[2]);
            assert(ws[v] ? o[2][c] == 1.0f / sqrtf(fabsf(ws[v])) : o[2][c] == FLT_MAX);
            assert(x[v] ? o[3][c] == 1.0f / sqrtf(fabsf(x[v])) : o[3][c] == FLT_MAX);
            assert(o[6][c] == (2.0f * in[c]) * (in[c] + 1.0f) + (in[c] + 2.0f));   /* exact for these inputs */
            assert(o[7][c] == (x[v] ? 1.0f / x[v] : 0.0f) && o[9][c] == (ws[v] ? 1.0f / sqrtf(fabsf(ws[v])) : 0.0f));
        }
        {   const float *l = o[10]; float y = in[1];
            assert(l[0] == 1.0f && l[3] == 1.0f && l[1] == (x[v] > 0 ? x[v] : 0.0f) && l[2] == (x[v] > 0 ? y * y : 0.0f));
        }
        for (unsigned c = 0; c < 4; ++c) {
            assert(o[11][c] == exp2f(in[1]) && o[12][c] == log2f(fabsf(ws[v])) && o[13][c] == exp2f(in[2] * 1.44269504f));
            assert(o[14][c] == exp2f(2.0f * log2f(in[1])) && o[15][c] == sinf(in[2]) && o[16][c] == cosf(in[2]));
        }
        {   const int ax = (int)floorf(in[1]), ay = (int)floorf(in[2] + 0.5f);
            for (unsigned c = 0; c < 4; ++c) {
                const float *k = io[v].constant[1 + ax], *m = io[v].constant[3 + ay];
                assert(o[17][c] == k[c] && o[18][c] == m[c] && o[20][c] == 1.0f / k[3]);
                assert(o[19][c] == (250 + ax <= 255 ? io[v].constant[250 + ax][c] : 0.0f));
            }
        }
    }
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); assert(dev);
    size_t need = 0; pt_r2_shader_msl(&ir, &iface, NULL, 0, &need, &e);
    char *text = (char *)malloc(need); assert(text && pt_r2_shader_msl(&ir, &iface, text, need, NULL, &e) == PT_R2_SHADER_OK);
    MTLCompileOptions *options = [MTLCompileOptions new]; options.mathMode = MTLMathModeSafe; NSError *error = nil;
    id<MTLLibrary> library = [dev newLibraryWithSource:@(text) options:options error:&error];
    if (!library) { NSLog(@"MSL: %@", error); return 1; }
    id<MTLComputePipelineState> pipeline = [dev newComputePipelineStateWithFunction:[library newFunctionWithName:@"pt_shader_test"] error:&error];
    assert(pipeline);
    id<MTLBuffer> buffer = [dev newBufferWithBytes:io length:N * sizeof *io options:MTLResourceStorageModeShared];
    uint32_t count = N;
    id<MTLCommandBuffer> cb = [[dev newCommandQueue] commandBuffer]; id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:pipeline]; [ce setBuffer:buffer offset:0 atIndex:0]; [ce setBytes:&count length:4 atIndex:1];
    [ce dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(N, 1, 1)]; [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
    const PtR2ShaderIO *gpu = (const PtR2ShaderIO *)buffer.contents;
    for (unsigned v = 0; v < N; ++v) for (unsigned o = 0; o < OUTS; ++o) for (unsigned c = 0; c < 4; ++c)
        if (o >= 13 ? !(fabsf(gpu[v].output[o][c] - cpu[v].output[o][c]) <= 2e-6f * fmaxf(1.0f, fabsf(cpu[v].output[o][c])))
                    : !near(gpu[v].output[o][c], cpu[v].output[o][c])) {   /* e^x, pow, sin, cos: library error (observed: 5.5^2 Metal -8, CPU +2 ULP) */
            printf("optics-pvs-math: vertex %u o%u.%c Metal %a, interpreter %a\n", v, o, "xyzw"[c], gpu[v].output[o][c], cpu[v].output[o][c]);
            return 1;
        }
    free(text); free(io); free(cpu);
    puts("optics-pvs-math: ok");
    return 0;
} }
