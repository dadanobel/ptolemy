/* SPDX-License-Identifier: GPL-2.0-or-later
 * Metal executor for the R300 model: render targets, depth storage, textures, programs and batches. */
#import "renderer.h"
#include "raw_vertex.h"
#import "foundation/surface_metal.h"
#include "foundation/internal_scale.h"
#import <os/signpost.h>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <map>
#include <set>
#include <string>
#include <mach-o/dyld.h>
#include <mutex>
#include <unordered_map>
#include <pthread.h>
#include <time.h>
#include <vector>
#include <cmath>

namespace {
static NSString *const unpackSource =
        @"#include <metal_stdlib>\n"
         "using namespace metal;\n"
         "float4 optics_texel(device const uchar *src, constant uint *p, uint2 g) {\n"
         "    ulong a = ulong(g.y) * p[2] + ulong(g.x) * p[3];\n"
         "    uint f = p[4];\n"
         "    float4 c = float4(0, 0, 0, 1);\n"
         /* DXT1/3/5 (13-15): p[2] a row of 4x4 blocks, p[3] the block; pixel.c's reference decodes the same way. */
         "    if (f >= 13u) {\n"
         "        ulong b = ulong(g.y >> 2) * p[2] + ulong(g.x >> 2) * p[3], q = f == 13u ? b : b + 8u;\n"
         "        uint i = (g.y & 3u) * 4u + (g.x & 3u);\n"
         "        uint c0 = uint(src[q]) | (uint(src[q + 1]) << 8), c1 = uint(src[q + 2])\n"
         "                | (uint(src[q + 3]) << 8), s = (uint(src[q + 4u + i / 4u]) >> (2u * (i & 3u))) & 3u;\n"
         "        float3 e0 = float3(c0 >> 11, (c0 >> 5) & 63u, c0 & 31u) / float3(31, 63, 31),\n"
         "                e1 = float3(c1 >> 11, (c1 >> 5) & 63u, c1 & 31u) / float3(31, 63, 31);\n"
         "        c.rgb = s == 0u ? e0 : s == 1u ? e1 : (f != 13u || c0 > c1)\n"
         "                ? (s == 2u ? (2.0f * e0 + e1) / 3.0f : (e0 + 2.0f * e1) / 3.0f) : s == 2u ? (e0 + e1)\n"
         "                * 0.5f : float3(0);\n"
         "        if (f == 13u && c0 <= c1 && s == 3u) c.a = 0;\n"
         "        if (f == 14u) c.a = float((uint(src[b + i / 2u]) >> (4u * (i & 1u))) & 15u) / 15.0f;\n"
         "        if (f == 15u) {\n"
         "            float a0 = src[b], a1 = src[b + 1];\n"
         "            uint t = 3u * i,\n"
         "                    k = ((uint(src[b + 2u + t / 8u]) | (uint(src[b + 3u + t / 8u]) << 8))\n"
         "                    >> (t & 7u)) & 7u;\n"
         "            c.a = (k == 0u ? a0 : k == 1u ? a1 : a0 > a1 ? (float(8u - k) * a0 + float(k - 1u) * a1)\n"
         "                    / 7.0f : k == 6u ? 0.0f : k == 7u ? 255.0f\n"
         "                    : (float(6u - k) * a0 + float(k - 1u) * a1) / 5.0f) / 255.0f;\n"
         "        }\n"
         "    } else if (f == 0) c = float4(src[a], src[a + 1], src[a + 2], src[a + 3]) / 255.0f;\n"
         "    else if (f == 7) c.r = float(src[a]) / 255.0f;\n"
         "    else if (f == 8) c = float4(float(src[a]) / 255.0f, float(src[a + 1]) / 255.0f, 0, 1);\n"
         "    else if (f >= 9) {\n"
         "        ulong b = ulong(g.y) * p[2] + ulong(g.x & ~1u) * 2;\n"
         "        uint uyvy = (f - 9u) & 1u, sw = f >= 11u ? 128u : 0u;\n"
         "        float y = float(src[b + (uyvy ? 1u : 0u) + (g.x & 1u) * 2u]) - 16.0f;\n"
         "        float u = float(as_type<char>(uchar(uint(src[b + (uyvy ? 0u : 1u)]) ^ sw))),\n"
         "                v = float(as_type<char>(uchar(uint(src[b + (uyvy ? 2u : 3u)]) ^ sw)));\n"
         "        c = float4(clamp(float3(1.1643835616f * y + 1.5960267857f * v, 1.1643835616f * y\n"
         "                - 0.3917622901f * u - 0.8129676472f * v, 1.1643835616f * y + 2.0172321429f * u), 0.0f,\n"
         "                255.0f) / 255.0f, 1);\n"
         "    } else {\n"
         "        uint v = uint(src[a]) | (uint(src[a + 1]) << 8);\n"
         "        if ((f & 1u) == 0u) v = (v >> 8) | ((v & 255u) << 8);\n"
         "        if (f <= 2) c = float4(float(v >> 11) / 31.0f, float((v >> 5) & 63u) / 63.0f,\n"
         "                float(v & 31u) / 31.0f, 1);\n"
         "        else if (f <= 4) c = float4(float((v >> 10) & 31u) / 31.0f, float((v >> 5) & 31u) / 31.0f,\n"
         "                float(v & 31u) / 31.0f, float(v >> 15));\n"
         "        else c = float4(float((v >> 8) & 15u), float((v >> 4) & 15u), float(v & 15u),\n"
         "                float(v >> 12)) / 15.0f;\n"
         "    }\n"
         "    float4 out;\n"
         "    for (uint k = 0; k < 4; k++) {\n"
         "        uint s = (p[5] >> (k * 3)) & 7u;\n"
         "        out[k] = s < 4 ? c[s] : s == 4 ? 0.0f : 1.0f;\n"
         "    }\n"
         "    return out;\n"
         "}\n"
         "kernel void optics_unpack(device const uchar *src [[buffer(0)]], constant uint *p [[buffer(1)]],\n"
         "        texture2d<float, access::write> dst [[texture(0)]], uint2 g [[thread_position_in_grid]]) {\n"
         "    if (g.x < p[0] && g.y < p[1]) dst.write(optics_texel(src, p, g), g);\n"
         "}\n"
         "kernel void optics_unpack3d(device const uchar *src [[buffer(0)]], constant uint *p [[buffer(1)]],\n"
         "        texture3d<float, access::write> dst [[texture(0)]], uint3 g [[thread_position_in_grid]]) {\n"
         "    if (g.x < p[0] && g.y < p[1]) dst.write(optics_texel(src + ulong(g.z) * p[6], p, g.xy), g);\n"
         "}\n";
/* Internal resolution (foundation/internal_scale.h), as full-target passes. optics_scale_up fills a scaled target's
 * texels from guest-size colour: each takes the guest pixel under its centre (pt_r2_scale_guest). optics_scale_down
 * is pt_r2_scale_down8 in the same integers: the guest-size colour every reader of a scaled target takes.
 * p: the factor in halves; for up, the source origin minus the destination's and the source's last pixel; for down,
 * the guest's size in half texels. */
static NSString *const scaleSource =
        @"#include <metal_stdlib>\n"
         "using namespace metal;\n"
         "struct OpticsScale {\n"
         "    float4 position [[position]];\n"
         "};\n"
         "vertex OpticsScale optics_scale_vertex(uint i [[vertex_id]]) {\n"
         "    OpticsScale o;\n"
         "    o.position = float4(float(i & 1u) * 4.0f - 1.0f, float(i >> 1u) * 4.0f - 1.0f, 0.0f, 1.0f);\n"
         "    return o;\n"
         "}\n"
         "fragment float4 optics_scale_up(OpticsScale v [[stage_in]], texture2d<float> src [[texture(0)]],\n"
         "        constant int *p [[buffer(0)]]) {\n"
         "    int2 g = (2 * int2(v.position.xy) + 1) / p[0] + int2(p[1], p[2]);\n"
         "    return src.read(uint2(clamp(g, int2(0), int2(p[3], p[4]))));\n"
         "}\n"
         "fragment float4 optics_scale_down(OpticsScale v [[stage_in]], texture2d<float> src [[texture(0)]],\n"
         "        constant int *p [[buffer(0)]]) {\n"
         "    uint h = uint(p[0]);\n"
         "    uint2 lo = uint2(v.position.xy) * h, hi = lo + h, end = uint2(uint(p[1]), uint(p[2]));\n"
         "    uint4 sum = uint4(0u);\n"
         "    uint area = 0u;\n"
         "    for (uint ty = lo.y / 2u; 2u * ty < hi.y && 2u * ty + 2u <= end.y; ty++) {\n"
         "        uint wy = min(2u * ty + 2u, hi.y) - max(2u * ty, lo.y);\n"
         "        for (uint tx = lo.x / 2u; 2u * tx < hi.x && 2u * tx + 2u <= end.x; tx++) {\n"
         "            uint wx = min(2u * tx + 2u, hi.x) - max(2u * tx, lo.x);\n"
         "            area += wx * wy;\n"
         "            sum += wx * wy * uint4(rint(saturate(src.read(uint2(tx, ty))) * 255.0f));\n"
         "        }\n"
         "    }\n"
         "    return float4((sum + area / 2u) / area) / 255.0f;\n"
         "}\n";
struct Runtime {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<PtR2Completion> completions;
    std::map<uint64_t, PtR2Completion> retired;
    uint64_t nextCompletion = 1;
    uint64_t epoch = 1, resident = 0, peak = 0;
    unsigned inflight = 0, wakes = 0;
    bool closed = false, failed = false;
    uint64_t completed = 0, callbackNS = 0, maxCallbackNS = 0, startNS = 0,
             endNS = 0; /* startNS: submit -> GPU start, endNS: GPU end -> callback (host clock) */
    double gpuSeconds = 0;
    dispatch_block_t __strong wake = nil;
    /* Upload buffers of retired batches, reused instead of a new MTLBuffer per batch (a kernel call and
     * fresh zero-filled pages each time: about 1400 per second in Xbench OpenGL). */
    std::vector<id<MTLBuffer>> spare;
};
/* The caller reserves under the same lock as retirement. close waits for both GPU and callback users. */
static dispatch_block_t reserveWake(Runtime &s) {
    if (s.closed || !s.wake)
        return nil;
    s.wakes++;
    return s.wake;
}
static void runWake(const std::shared_ptr<Runtime> &s, dispatch_block_t wake) {
    if (!wake)
        return;
    wake();
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->wakes--;
    }
    s->changed.notify_all();
}
static bool blendValid(const PtR2Route &r) {
    uint32_t b = r.blendState;
    return !r.blend && b >> 31 && (b & 15) <= 14 && (b >> 4 & 15) <= 14 && (b >> 8 & 7) <= 4 && (b >> 12 & 15) <= 14 &&
           (b >> 16 & 15) <= 14 && (b >> 20 & 7) <= 4;
}
static bool epilogueValid(const PtR2Route &r) {
    if (r.vte > 2 || r.selectVaryings > 1 || r.pad[0] || r.pad[1])
        return false;
    for (unsigned j = 0; j < 8; j++)
        for (unsigned c = 0; c < 4; c++)
            if (r.select[j][c] > 5 || (!r.selectVaryings && !(r.generated >> j & 1) && r.select[j][c]))
                return false;
    return true;
}
static uint64_t nowNS() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000000 + t.tv_nsec;
}
static BOOL fail(NSError **e, NSString *s, NSInteger code = 1) {
    if (e)
        *e = [NSError errorWithDomain:@"Ptolemy.Radeon2.Prototype" code:code userInfo:@{NSLocalizedDescriptionKey : s}];
    return NO;
}
static void word(NSMutableData *data, uint32_t x) {
    uint8_t b[4] = {(uint8_t)x, (uint8_t)(x >> 8), (uint8_t)(x >> 16), (uint8_t)(x >> 24)};
    [data appendBytes:b length:4];
}
static NSString *body(const PtR2ShaderIR *ir, const PtR2ShaderInterface *iface, NSError **e) {
    PtR2ShaderError error = {};
    size_t needed = 0;
    if (pt_r2_shader_msl_body(ir, iface, NULL, 0, &needed, &error) != PT_R2_SHADER_BUFFER || !needed) {
        fail(e, [NSString stringWithUTF8String:error.reason]);
        return nil;
    }
    std::vector<char> bytes(needed);
    if (pt_r2_shader_msl_body(ir, iface, bytes.data(), bytes.size(), NULL, &error) != PT_R2_SHADER_OK) {
        fail(e, [NSString stringWithUTF8String:error.reason]);
        return nil;
    }
    return [NSString stringWithUTF8String:bytes.data()];
}
/* Size of the constant buffer a shader reads (highest constant used). */
static uint32_t constantBytes(const PtR2ShaderIR *ir) {
    uint32_t bytes = 16;
    auto use = [&](PtR2ShaderOperand op) {
        if (op.bank == PT_R2_CONSTANT)
            bytes = std::max(bytes, op.relative ? 256u * 16 : (uint32_t(op.index) + 1) * 16);
    };
    for (unsigned i = 0; i < ir->step_count; i++) {
        const PtR2ShaderStep &s = ir->step[i];
        if (s.kind == PT_R2_STEP_PVS)
            for (unsigned j = 0; j < 3; j++)
                use(s.u.pvs.arg[j]);
        if (s.kind == PT_R2_STEP_US)
            for (unsigned j = 0; j < 3; j++) {
                use(s.u.us.rgb_source[j]);
                use(s.u.us.alpha_source[j]);
                use(s.u.us.rgb_arg[j]);
                use(s.u.us.alpha_arg[j]);
            }
    }
    return bytes;
}
static NSString *varyings(PtR2Route r, bool vertex) {
    NSMutableString *s = [NSMutableString stringWithString:@"struct PtVary {\n"
                                                            "    float4 position [[position]];\n"];
    for (unsigned i = 0; i < 8; i++)
        [s appendFormat:@"float4 v%u [[user(v%u), %@]];\n", i, i, r.flat >> i & 1 ? @"flat" : @"center_perspective"];
    if (r.fog)
        [s appendString:@"float pt_fog [[user(fog)]];\n"];
    if (vertex && r.clipPlanes)
        [s appendString:@"float pt_clip [[clip_distance]] [6];\n"];
    [s appendString:@"};\n"];
    return s;
}
/* The fragment stage's fixed-function values (PtR2Raster), bound at fragment buffer 2. */
static NSString *fixedSource() {
    return @"struct PtFixed {\n"
            "    float4 fog;\n"
            "    float4 fogTerm;\n"
            "    float4 border[16];\n"
            "    float lod[16];\n"
            "    uint mode[16];\n"
            "    float4 key[16];\n"
            "};\n";
}
/* Emit the existing VAP integer contract as native vertex arithmetic. All
 * selectors/formats are pipeline constants; guest bytes remain immutable. */
static void rawInput(NSMutableString *s, unsigned j, PtR2Attribute a, PtR2Route r) {
    if (a.format == OPTICS_RAW_DEFAULT) {
        [s appendFormat:@"io.input[%u] = float4(0.0f, 0.0f, 0.0f, 1.0f);\n", j];
        return;
    }
    unsigned bits = a.format - OPTICS_RAW_INT, kind = bits & 3, mode = bits >> 5;
    bool noZero = a.format >= OPTICS_RAW_NORM2;
    bool floating = a.format >= OPTICS_RAW_FLOAT && a.format < OPTICS_RAW_DEFAULT;
    if (noZero) {
        kind = (a.format - OPTICS_RAW_NORM2) & 3;
        mode = (a.format - OPTICS_RAW_NORM2) >> 2;
        bits = kind | 4 | 8;
    }
    if (floating) {
        kind = (a.format - OPTICS_RAW_FLOAT) & 3;
        mode = (a.format - OPTICS_RAW_FLOAT) >> 2;
    }
    [s appendFormat:@"float4 pt_raw%u = float4(0.0f, 0.0f, 0.0f, 1.0f);\n", j];
    for (unsigned w = 0; w < optics_raw_width(a.format, a.components) / 4; w++)
        [s appendFormat:@"uint pt_w%u_%u = pt_load_word(v, ulong(id) * %uu + %uu, %uu);\n", j, w, r.stride,
                        a.offset + 4 * w, mode];
    for (unsigned c = 0; c < a.components; c++) {
        if (floating) {
            [s appendFormat:@"pt_raw%u.%c = as_type<float>(pt_w%u_%u);\n", j, "xyzw"[c], j, c];
            continue;
        }
        unsigned k = kind == 1 ? (c < 3 ? 2 - c : 3) : c, n = kind >= 2 ? 16 : 8, word = kind >= 2 ? k / 2 : 0,
                 shift = n * (kind >= 2 ? k & 1 : k);
        [s appendFormat:@"uint pt_u%u_%u = (pt_w%u_%u >> %uu) & %uu;\n"
                         "float pt_f%u_%u =",
                        j, c, j, word, shift, (1u << n) - 1, j, c];
        if (bits & 4)
            [s appendFormat:@"pt_u%u_%u >= %uu ? float(pt_u%u_%u) - %u.0f : float(pt_u%u_%u);\n", j, c, 1u << (n - 1),
                            j, c, 1u << n, j, c];
        else
            [s appendFormat:@"float(pt_u%u_%u);\n", j, c];
        [s appendFormat:@"pt_raw%u.%c =", j, "xyzw"[c]];
        if (noZero)
            [s appendFormat:@"(2.0f * pt_f%u_%u + 1.0f) / %u.0f", j, c, (1u << n) - 1];
        else if (bits & 8) {
            if (bits & 16)
                [s appendString:@"max("];
            [s appendFormat:@"pt_f%u_%u / %u.0f", j, c, (1u << (n - ((bits >> 2) & 1))) - 1];
            if (bits & 16)
                [s appendString:@", -1.0f)"];
        } else
            [s appendFormat:@"pt_f%u_%u", j, c];
        [s appendString:@";\n"];
    }
    [s appendFormat:@"io.input[%u] = float4(", j];
    uint16_t select = r.vapSelect[j];
    for (unsigned c = 0; c < 4; c++) {
        if (c)
            [s appendString:@","];
        if (!(select >> (12 + c) & 1))
            [s appendString:c == 3 ? @"1.0f" : @"0.0f"];
        else {
            unsigned k = select >> (3 * c) & 7;
            if (k < 4)
                [s appendFormat:@"pt_raw%u.%c", j, "xyzw"[k]];
            else
                [s appendString:k == 5 ? @"1.0f" : @"0.0f"];
        }
    }
    [s appendString:@");\n"];
}
static NSString *rawLoadSource() {
    return @"uint pt_load_word(device const uchar *v, ulong a, uint mode) {\n"
            "    uint x = uint(v[a]) | uint(v[a + 1]) << 8 | uint(v[a + 2]) << 16 | uint(v[a + 3]) << 24;\n"
            "    if (mode == 1u) return ((x >> 8) & 0x00ff00ffu) | ((x << 8) & 0xff00ff00u);\n"
            "    if (mode == 2u) return x >> 24 | ((x >> 8) & 0xff00u) | ((x << 8) & 0xff0000u) | x << 24;\n"
            "    if (mode == 3u) return x >> 16 | x << 16;\n"
            "    return x;\n"
            "}\n";
}
/* Metal source of a vertex function: fetches the R300 vertex inputs, runs the translated program, user clip planes
 * and fog. */
static NSString *vertexSource(NSString *source, const PtR2ShaderInterface *vi, PtR2Route r) {
    NSMutableString *s = [NSMutableString stringWithString:@"#define PT_BODY_ARGS uint pt_unused\n"];
    [s appendString:source];
    [s appendString:varyings(r, true)];
    bool raw = false;
    for (unsigned j = 0; j < vi->input_count; j++)
        raw |= r.attribute[j].format >= OPTICS_RAW_INT;
    if (raw)
        [s appendString:rawLoadSource()];
    [s appendString:@"float pt_load_f32(device const uchar *v, ulong a, uint be) {\n"
                     "    uint b = be ? (uint(v[a]) << 24 | uint(v[a + 1]) << 16 | uint(v[a + 2]) << 8 | uint(v[a + 3]))\n"
                     "            : (uint(v[a]) | uint(v[a + 1]) << 8 | uint(v[a + 2]) << 16 | uint(v[a + 3]) << 24);\n"
                     "    return as_type<float>(b);\n"
                     "}\n"
                     "vertex PtVary pt_vertex(uint id [[vertex_id]], device const uchar *v [[buffer(0)]],\n"
                     "        constant float4 *k [[buffer(1)]], constant float4 *optics_vte [[buffer(2)]]"];
    if (r.clipPlanes)
        [s appendString:@", constant float4 *pt_ucp [[buffer(3)]]"];
    [s appendString:@") {\n"
                     "    PtIO io;\n"];
    for (unsigned j = 0; j < vi->input_count; j++) {
        PtR2Attribute a = r.attribute[j];
        if (a.format >= OPTICS_RAW_INT) {
            rawInput(s, j, a, r);
            continue;
        }
        [s appendFormat:@"io.input[%u] = float4(", j];
        for (unsigned c = 0; c < 4; c++) {
            if (c)
                [s appendString:@","];
            if (c >= a.components)
                [s appendString:c == 3 ? @"1.0f" : @"0.0f"];
            else if (a.format == 2)
                [s appendFormat:@"float(v[ulong(id) * %uu + %uu]) / 255.0f", r.stride, a.offset + c];
            else
                [s appendFormat:@"pt_load_f32(v, ulong(id) * %uu + %uu, %uu)", r.stride, a.offset + c * 4, a.format];
        }
        [s appendString:@");\n"];
    }
    if (r.vte == 2) {
        [s appendFormat:@"pt_shader_body(io, k, 0u);\n"
                         "PtVary o;\n"
                         "float4 pt_pos = io.output[%u];\n"
                         "uint pt_fmt = uint(optics_vte[2].x);\n"
                         "float3 pt_win = pt_pos.xyz;\n",
                        r.positionOutput];
        [s appendString:@"if ((pt_fmt & 256u) == 0u) {\n"
                         "    pt_win.x = pt_div(pt_win.x, pt_pos.w);\n"
                         "    pt_win.y = pt_div(pt_win.y, pt_pos.w);\n"
                         "}\n"
                         "if ((pt_fmt & 512u) == 0u) pt_win.z = pt_div(pt_win.z, pt_pos.w);\n"];
        [s appendString:@"if (pt_fmt & 1u) pt_win.x *= optics_vte[0].x;\n"
                         "if (pt_fmt & 2u) pt_win.x += optics_vte[0].y;\n"
                         "if (pt_fmt & 4u) pt_win.y *= optics_vte[0].z;\n"
                         "if (pt_fmt & 8u) pt_win.y += optics_vte[0].w;\n"
                         "if (pt_fmt & 16u) pt_win.z *= optics_vte[1].x;\n"
                         "if (pt_fmt & 32u) pt_win.z += optics_vte[1].y;\n"];
        [s appendString:@"float pt_cw = (pt_fmt & 256u) ? 1.0f : pt_pos.w;\n"
                         "float pt_z = (pt_fmt & 1024u) ? pt_win.z : 0.5f;\n"
                         "o.position = float4((pt_div(2.0f * pt_win.x, optics_vte[1].z) - 1.0f) * pt_cw,\n"
                         "        (1.0f - pt_div(2.0f * pt_win.y, optics_vte[1].w)) * pt_cw, pt_z * pt_cw, pt_cw);\n"];
    } else if (r.vte)
        [s appendFormat:@"pt_shader_body(io, k, 0u);\n"
                         "PtVary o;\n"
                         "float4 pt_pos = io.output[%u];\n"
                         "o.position = float4(pt_pos.x * optics_vte[0].x + pt_pos.w * optics_vte[0].y, pt_pos.y * optics_vte[0].z\n"
                         "        + pt_pos.w * optics_vte[0].w, pt_pos.z * optics_vte[1].x + pt_pos.w\n"
                         "        * optics_vte[1].y, pt_pos.w);\n",
                        r.positionOutput];
    else
        [s appendFormat:@"pt_shader_body(io, k, 0u);\n"
                         "PtVary o;\n"
                         "o.position = io.output[%u];\n"
                         "o.position.z = (o.position.z + o.position.w) * 0.5f;\n",
                        r.positionOutput];
    for (unsigned j = 0; j < 8; j++) {
        if (j < r.varyingCount && r.selectVaryings) {
            [s appendFormat:@"o.v%u = float4(", j];
            for (unsigned c = 0; c < 4; c++) {
                uint8_t x = r.select[j][c];
                if (x < 4)
                    [s appendFormat:@"%@io.output[%u].%c", c ? @"," : @"", r.pvsOutput[j], "xyzw"[x]];
                else
                    [s appendFormat:@"%@%@", c ? @"," : @"", x == 5 ? @"1.0f" : @"0.0f"];
            }
            [s appendString:@");\n"];
        } else if (j < r.varyingCount)
            [s appendFormat:@"o.v%u = io.output[%u];\n", j, r.pvsOutput[j]];
        else
            [s appendFormat:@"o.v%u = float4(0.0f);\n", j];
    }
    /* User clip planes against the R300 clip-space position; the FG fog input (draw.h). */
    for (unsigned j = 0; j < 6; j++)
        if (r.clipPlanes)
            [s appendFormat:@"o.pt_clip[%u] = %@;\n", j,
                            r.clipPlanes >> j & 1
                                    ? [NSString stringWithFormat:@"dot(io.output[%u], pt_ucp[%u])", r.positionOutput, j]
                                    : @"1.0f"];
    if (r.fog)
        [s appendFormat:@"o.pt_fog = %@;\n",
                        r.fogSelect < 4    ? [NSString stringWithFormat:@"io.output[%u].w", r.fogOutput]
                        : r.fogSelect == 4 ? [NSString stringWithFormat:@"io.output[%u].w", r.positionOutput]
                                           : [NSString stringWithFormat:@"pt_div(io.output[%u].z, io.output[%u].w)",
                                                                        r.positionOutput, r.positionOutput]];
    [s appendString:@"return o;\n"
                     "}\n"];
    return s;
}
/* Metal source of a fragment function: textures and samplers, the translated pixel program, alpha test, fog, depth
 * output and the R300 raster operation. */
static NSString *fragmentSource(NSString *source, const PtR2ShaderIR *us, const PtR2ShaderInterface *fi, PtR2Route r) {
    bool fixed = r.fog || r.textureFixed;
    NSMutableString *s = [NSMutableString
            stringWithString:fixed ? @"#define PT_BODY_ARGS constant PtFixed &pt_fx\n"
                                      "#define PT_TEX(unit,tx,sp,c,op) pt_sample(tx,sp,c,op,unit,pt_fx,io.killed)\n"
                                   : @"#define PT_BODY_ARGS uint pt_unused\n"
                                      "#define PT_TEX(unit,tx,sp,c,op) pt_sample(tx,sp,c,op)\n"];
    [s appendString:@"#include <metal_stdlib>\n"
                     "using namespace metal;\n"];
    [s appendString:fixedSource()];
    /* Only the samplers of the texture types this shader uses: each type's sample() costs Metal's compiler about 100 ms
     * even when nothing calls it (u080: 381 -> 202 ms a fragment without the unused cube and 3D ones). */
    bool dim[3] = {false, false, false}; /* 2D, 3D, cube (texture_dimension) */
    for (unsigned j = 0; j < 16; j++)
        if (us->textures & (1u << j))
            dim[fi->texture_dimension[j] == 2 ? 2 : fi->texture_dimension[j] == 1 ? 1 : 0] = true;
    NSString *const types[3] = {@"texture2d", @"texture3d", @"texturecube"};
    for (unsigned d = 0; d < 3; d++)
        if (dim[d])
            [s appendFormat:fixed ? @"float4 pt_sample(%@<float>, sampler, float4, uint, uint, constant PtFixed &, thread uint &);\n"
                                  : @"float4 pt_sample(%@<float>, sampler, float4, uint);\n",
                            types[d]];
    [s appendString:fixed ? @"float4 pt_chroma(float4 x, uint u, constant PtFixed &fx, thread uint &k) {\n"
                             "    uint m = (fx.mode[u] >> 5) & 3u;\n"
                             "    if (m && all(abs(x - fx.key[u]) < 0.5f / 255.0f)) {\n"
                             "        if (m == 1u) k = 1u;\n"
                             "        else x = float4(0.0f);\n"
                             "    }\n"
                             "    return x;\n"
                             "}\n"
                          : @""];
    [s appendString:source];
    [s appendString:varyings(r, false)];
    if (fixed)
        /* LOD bias (TX_FILTER1) on every sample, and border modes over the sampler's zero border: the colour gets the
         * border's share of the filter footprint (exact for nearest and bilinear; with mip levels, as at level 0). */
        [s appendString:!dim[0] ? @"" : @"float4 pt_sample(texture2d<float> t, sampler s, float4 c, uint op, uint u, constant PtFixed &fx,\n"
                         "        thread uint &k) {\n"
                         "    if (op == 3u) c.xyz = pt_div(c.xyz, float3(c.w));\n"
                         "    uint m = fx.mode[u];\n"
                         "    float2 p = c.xy;\n"
                         "    if ((m & 3u) == 2u) p.x = saturate(p.x);\n"
                         "    if (((m >> 2) & 3u) == 2u) p.y = saturate(p.y);\n"
                         "    float4 x = t.sample(s, p, bias(fx.lod[u] + (op == 4u ? c.w : 0.0f)));\n"
                         "    if ((m & 15u) != 0u) {\n"
                         "        float2 n = float2(t.get_width(), t.get_height()), cov;\n"
                         "        if ((m & 16u) != 0u) {\n"
                         "            float2 q = p * n - 0.5f, i = floor(q), f = q - i;\n"
                         "            cov = (1.0f - f) * step(0.0f, i) * step(i, n - 1.0f) + f * step(0.0f, i + 1.0f)\n"
                         "                    * step(i + 1.0f, n - 1.0f);\n"
                         "        } else cov = step(0.0f, p) * (1.0f - step(1.0f, p));\n"
                         "        if ((m & 3u) == 0u) cov.x = 1.0f;\n"
                         "        if (((m >> 2) & 3u) == 0u) cov.y = 1.0f;\n"
                         "        x += fx.border[u] * (1.0f - cov.x * cov.y);\n"
                         "    }\n"
                         "    return pt_chroma(x, u, fx, k);\n"
                         "}\n"];
    if (fixed) {
        for (unsigned d = 1; d < 3; d++)
            if (dim[d])
                [s appendFormat:@"float4 pt_sample(%@<float> t, sampler s, float4 c, uint op, uint u, constant PtFixed &fx,\n"
                                 "        thread uint &k) {\n"
                                 "    if (op == 3u) c.xyz = pt_div(c.xyz, float3(c.w));\n"
                                 "    return pt_chroma(t.sample(s, c.xyz, bias(fx.lod[u] + (op == 4u ? c.w : 0.0f))), u, fx, k);\n"
                                 "}\n",
                                types[d]];
    } else
        for (unsigned d = 0; d < 3; d++)
            if (dim[d])
                [s appendFormat:@"float4 pt_sample(%@<float> t, sampler s, float4 c, uint op) {\n"
                                 "    if (op == 3u) c.xyz = pt_div(c.xyz, float3(c.w));\n"
                                 "    if (op == 4u) return t.sample(s, c.%@, bias(c.w));\n"
                                 "    return t.sample(s, c.%@);\n"
                                 "}\n",
                                types[d], d ? @"xyz" : @"xy", d ? @"xyz" : @"xy"];
    [s appendString:@"struct PtOut {\n"
                     "    float4 color [[color(0)]];"];
    if (us->writes_depth)
        [s appendString:@"float depth [[depth(any)]];"];
    [s appendString:@"};\n"
                     "struct PtAlpha {\n"
                     "uint enabled;\n"
                     "uint compare;\n"
                     "float reference;\n"
                     "};\n"
                     "bool pt_compare(uint f, float a, float b) {\n"
                     "switch (f) {\n"
                     "case 0: return false;\n"
                     "case 1: return a < b;\n"
                     "case 2: return a == b;\n"
                     "case 3: return a <= b;\n"
                     "case 4: return a > b;\n"
                     "case 5: return a != b;\n"
                     "case 6: return a >= b;\n"
                     "default: return true;\n"
                     "}\n"
                     "}\n"
                     "fragment PtOut pt_fragment(PtVary v [[stage_in]], constant float4 *k [[buffer(0)]],\n"
                     "        constant PtAlpha &alpha [[buffer(1)]]"];
    if (fixed)
        [s appendString:@", constant PtFixed &pt_fx [[buffer(2)]]"];
    if (r.rop)
        [s appendString:@", float4 pt_dst [[color(0)]]"];
    for (unsigned j = 0; j < 16; j++)
        if (us->textures & (1u << j))
            [s appendFormat:@", texture%@<float> tx%u [[texture(%u)]], sampler sp%u [[sampler(%u)]]",
                            fi->texture_dimension[j] == 2   ? @"cube"
                            : fi->texture_dimension[j] == 1 ? @"3d"
                                                            : @"2d",
                            j, j, j, j];
    [s appendString:@") {\n"
                     "    PtIO io;\n"];
    for (unsigned j = 0; j < 32; j++)
        [s appendFormat:@"io.input[%u] = float4(0.0f);\n", j];
    for (unsigned j = 0; j < r.varyingCount; j++)
        [s appendFormat:@"io.input[%u] = v.v%u;\n", r.usInput[j], j];
    for (unsigned j = 0; j < r.varyingCount; j++)
        if (r.generated >> j & 1) { /* the negated window position (pixel centre) >> shift, in guest pixels */
            NSString *c[4];
            for (unsigned q = 0; q < 4; q++) {
                unsigned sel = r.select[j][q];
                c[q] = sel < 2    ? [NSString stringWithFormat:@"v.position.%c * float(%.9g)", sel ? 'y' : 'x',
                                                            -ldexp(1.0, -(int)(sel ? r.generatedShift >> 4
                                                                                      : r.generatedShift & 15)) *
                                                                    (r.generatedScale ? 2.0 / r.generatedScale : 1.0)]
                       : sel == 5 ? @"1.0f"
                                  : @"0.0f";
            }
            [s appendFormat:@"io.input[%u] = float4(%@, %@, %@, %@);\n", r.usInput[j], c[0], c[1], c[2], c[3]];
        }
    [s appendString:fixed ? @"pt_shader_body(io, k, pt_fx" : @"pt_shader_body(io, k, 0u"];
    for (unsigned j = 0; j < 16; j++)
        if (us->textures & (1u << j))
            [s appendFormat:@", tx%u, sp%u", j, j];
    [s appendFormat:@");\n"
                     "if (io.killed) discard_fragment();\n"
                     "PtOut o;\n"
                     "o.color = io.output[%u];",
                    r.colorOutput];
    if (r.fog)
        [s appendString:@"o.color.rgb = mix(o.color.rgb, pt_fx.fog.rgb,\n"
                         "        clamp(v.pt_fog * pt_fx.fogTerm.x + pt_fx.fogTerm.y, 0.0f, 1.0f));"];
    [s appendString:@"if (alpha.enabled\n"
                     "        && !pt_compare(alpha.compare, saturate(o.color.a), alpha.reference)) discard_fragment();"];
    if (r.discard) {
        NSArray *test = @[
            @"", @"q.a == 0.0f", @"all(q.rgb == 0.0f)", @"all(q == 0.0f)", @"q.a == 255.0f", @"all(q.rgb == 255.0f)",
            @"all(q == 255.0f)"
        ];
        [s appendFormat:@"{\n"
                         "    float4 q = rint(saturate(o.color) * 255.0f);\n"
                         "    if (%@) discard_fragment();\n"
                         "}",
                        test[r.discard]];
    }
    if (r.rop) { /* RB3D_ROPCNTL: code bit (src << 1 | dst) is the result for that bit pair, on 8-bit channels */
        NSMutableArray *terms = [NSMutableArray array];
        unsigned code = r.rop - 1u;
        if (code & 1)
            [terms addObject:@"(~s & ~d)"];
        if (code & 2)
            [terms addObject:@"(~s & d)"];
        if (code & 4)
            [terms addObject:@"(s & ~d)"];
        if (code & 8)
            [terms addObject:@"(s & d)"];
        [s appendFormat:@"{\n"
                         "    uint4 s = uint4(rint(saturate(o.color) * 255.0f)), d = uint4(rint(saturate(pt_dst) * 255.0f));\n"
                         "    o.color = float4((%@) & 255u) / 255.0f;\n"
                         "}",
                        terms.count ? [terms componentsJoinedByString:@"|"] : @"uint4(0u)"];
    }
    if (us->writes_depth)
        [s appendString:@"o.depth = io.depth;"];
    [s appendString:@"return o;\n"
                     "}\n"];
    return s;
}
}

PtR2Limits pt_r2_default_limits(id<MTLDevice> device) {
    if (!device || !device.hasUnifiedMemory)
        return {};
    uint64_t working = device.recommendedMaxWorkingSetSize;
    if (!working)
        working = NSProcessInfo.processInfo.physicalMemory / 2;
    uint64_t ceiling =
            NSProcessInfo.processInfo.physicalMemory >= (UINT64_C(16) << 30) ? UINT64_C(1) << 30 : UINT64_C(512) << 20;
    uint64_t resident = std::min(ceiling, std::max(UINT64_C(64) << 20, working / 8));
    return {2, 4096, resident, std::min(UINT64_C(64) << 20, resident / 8)};
}

@interface R2Lease : NSObject {
  @public
    std::shared_ptr<Runtime> runtime;
    uint64_t epoch, charged;
    R2Lease *parent; /* Texture views retain the charged storage lease. */
    id<MTLBuffer> buffer;
    id<MTLTexture> texture;
    BOOL recycle; /* a standard upload buffer: back to Runtime.spare when the last holder (its batch) lets go */
}
@end
@implementation R2Lease
- (void)dealloc {
    if (runtime && charged) {
        dispatch_block_t wake = nil;
        {
            std::lock_guard<std::mutex> lock(runtime->mutex);
            runtime->resident -= charged;
            wake = reserveWake(*runtime);
            if (recycle && buffer && !runtime->closed && runtime->spare.size() < 8)
                runtime->spare.push_back(buffer);
        }
        runWake(runtime, wake); /* A UI frame lease can free capacity after the last GPU batch. */
    }
}
@end
struct R2Slice {
    R2Lease *__strong lease;
    size_t offset;
};
@interface PtR2Texture () {
  @public
    R2Lease *lease;
    std::atomic<int> status;
    PtR2Target *viewOf; /* view: of this target's colour (its reduced colour once the target is scaled) */
}
@end
@implementation PtR2Texture
@end
@interface PtR2Frame () {
  @public
    PtR2Texture *source;
}
@end
@implementation PtR2Frame
- (id<MTLTexture>)texture {
    return source->lease->texture;
}
@end
@interface PtR2Depth () {
  @public
    R2Lease *lease;
    BOOL initialized;
    uint32_t gw, gh; /* the guest's size; the texture is at the internal resolution `scale` */
    unsigned scale;
}
@end
@implementation PtR2Depth
- (uint32_t)width {
    return gw;
}
- (uint32_t)height {
    return gh;
}
@end
@interface PtR2Target () {
  @public
    R2Lease *lease;
    PtR2MetalSurface *surface;
    id<MTLTexture> depth;
    BOOL initialized;
    PtR2Depth *bound; /* independent depth/stencil for the following passes, nil = depth */
    /* Internal resolution: surface and depth are `scale` halves of the guest's gw x gh (PT_R2_SCALE_ONE: the same).
     * reduced: a scaled target's colour at the guest's size, for everything that reads it outside its own passes;
     * stale: drawn or copied into since it was reduced. */
    uint32_t gw, gh;
    unsigned scale;
    id<MTLTexture> reduced;
    R2Lease *reducedLease;
    BOOL stale;
}
@end
@implementation PtR2Target
- (uint32_t)width {
    return gw;
}
- (uint32_t)height {
    return gh;
}
- (unsigned)scale {
    return scale;
}
- (uint32_t)samples {
    return surface.samples.count;
}
@end
@interface PtR2Program () {
  @public
    std::shared_ptr<Runtime> runtime;
    uint64_t epoch;
    PtR2Route route;
    uint32_t textureMask;
    uint32_t vertexConstantBytes, fragmentConstantBytes;
    id<MTLRenderPipelineState> pipeline;
}
@end
@implementation PtR2Program
@end
@interface PtR2Readback () {
  @public
    R2Lease *lease;
    std::atomic<int> status;
    size_t rowBytes;
    uint32_t width, height;
    size_t stencilRow;
    BOOL depthPlanes;
    /* depth planes: `rows` storage rows from the first one of guest row originY, at the internal resolution scale */
    unsigned scale;
    uint32_t originY, rows;
}
@end
@implementation PtR2Readback
- (uint32_t)width {
    return width;
}
- (uint32_t)height {
    return height;
}
@end

@implementation PtR2Renderer {
    std::shared_ptr<Runtime> _runtime;
    id<MTLDevice> _device;
    id<MTLCommandQueue> _queue;
    PtR2MetalResolver *_resolver;
    id<MTLComputePipelineState> _unpack, _unpack3d;
    /* OPTICS_INTERNAL: the internal resolution of targets with depth, in halves (x1.5 without it). */
    unsigned _scale;
    id<MTLRenderPipelineState> _scaleUp, _scaleDown;
    id<MTLSamplerState> _sampler[2];
    id<MTLDepthStencilState> _depth[8][2];
    NSMutableDictionary<NSNumber *, id<MTLDepthStencilState>> *_stencilStates;
    std::unordered_map<uint32_t, id<MTLSamplerState>>
            _samplerStates; /* by PtR2Raster sampler code, created on first use */
    id<MTLCommandBuffer> _batch;
    id<MTLRenderCommandEncoder> _encoder;
    PtR2Target *_activeTarget;
    /* Encoder-local state only. A new pass must bind everything again. */
    BOOL _cacheBindings, _bindingsValid;
    BOOL _completionPrefix;
    PtR2Program *_boundProgram;
    id<MTLDepthStencilState> _boundDepth;
    id<MTLTexture> _boundTexture[16];
    id<MTLSamplerState> _boundSampler[16];
    MTLViewport _boundViewport;
    MTLScissorRect _boundScissor;
    uint8_t _boundCull, _boundWinding, _boundStencil, _boundFill;
    float _boundEpilogue[12];
    uint8_t _boundAlpha[12];
    float _boundBlendColor[4], _boundBias[2];
    NSMutableArray *_leases;
    NSMutableArray<PtR2Readback *> *_readbacks;
    NSMutableArray<PtR2Texture *> *_exports;
    NSMutableDictionary<NSData *, PtR2Program *> *_programs;
    NSMutableDictionary<NSString *, id<MTLLibrary>> *_libraries;
    uint64_t _libraryCompiles, _libraryNS, _libraryMaxNS,
            _librarySlow; /* MSL compiled once per source; slow: 50 ms or more */
    /* Libraries compiled in the background for a draw that waits (startLibrariesForPVS): _libLock guards _libraries and
     * these, as completions arrive on Metal's threads. Failed sources go to preparePVS, which reports the error. */
    std::mutex _libLock;
    NSMutableSet<NSString *> *_pendingSources, *_failedSources;
    dispatch_group_t _compiling;
    std::atomic<unsigned> _inFlight;
    uint64_t _asyncCompiles, _asyncNS, _asyncMaxNS;
    /* Occlusion queries: every render pass writes Metal visibility results into _visibility (8-byte slots, a ring); a
     * query gets a fresh slot in each pass it draws in, so no pass resets or adds to another's count. */
    id<MTLBuffer> _visibility;
    uint32_t _visibilityNext, _passQuery, _passSlot;
    int64_t _boundVisibility;
    std::unordered_map<uint32_t, std::vector<uint32_t>> _querySlots;
    std::unordered_map<uint32_t, unsigned> _queryScale; /* queries drawn on a scaled target, and its scale */
    PtR2Limits _limits;
    pthread_t _owner;
    PtR2Ticket _ticket;
    uint64_t _sequence, _batchBytes;
    uint64_t _jobs, _draws, _commits, _busy, _compiled, _passes, _compileNS;
    uint64_t _passBreak[12];
    BOOL _passReasons;
    R2Lease *_upload;
    size_t _uploadUsed;
    uint64_t _bufferAllocations, _snapshotBytes;
    os_log_t _log;
    dispatch_queue_t _ownerQueue;
}
- (instancetype)initWithDevice:(id<MTLDevice>)device limits:(PtR2Limits)limits error:(NSError **)error {
    return [self initWithDevice:device limits:limits ownerQueue:nil error:error];
}
- (instancetype)initWithDevice:(id<MTLDevice>)device
                        limits:(PtR2Limits)limits
                    ownerQueue:(dispatch_queue_t)queue
                         error:(NSError **)error {
    if (!(self = [super init]))
        return nil;
    if (!device || !device.hasUnifiedMemory || !limits.maxBatches || limits.maxBatches > 16 || !limits.maxJobs ||
        limits.maxJobs > 65536 || !limits.batchBytes || limits.residentBytes < limits.batchBytes) {
        fail(error, @"Invalid Apple Silicon device or explicit resource limits");
        return nil;
    }
    const char *reasons = getenv("OPTICS_PASS_REASONS");
    _passReasons = reasons && atoi(reasons) != 0;
    _owner = pthread_self();
    _limits = limits;
    _runtime = std::make_shared<Runtime>();
    _device = device;
    const char *cache = getenv("OPTICS_BIND_CACHE");
    _cacheBindings = !cache || atoi(cache) != 0;
    const char *split = getenv("OPTICS_RENDER_OWNER");
    _completionPrefix = split && atoi(split) != 0;
    _queue = [device newCommandQueueWithMaxCommandBufferCount:limits.maxBatches];
    _resolver = [[PtR2MetalResolver alloc] initWithDevice:device error:error];
    if (!_queue || !_resolver)
        return nil;
    MTLCompileOptions *unpackOptions = [MTLCompileOptions new];
    unpackOptions.mathMode = MTLMathModeSafe;
    id<MTLLibrary> unpack = [device newLibraryWithSource:unpackSource options:unpackOptions error:error];
    id<MTLFunction> function = [unpack newFunctionWithName:@"optics_unpack"],
                    function3d = [unpack newFunctionWithName:@"optics_unpack3d"];
    if (!function || !(_unpack = [device newComputePipelineStateWithFunction:function error:error]))
        return nil;
    if (!function3d || !(_unpack3d = [device newComputePipelineStateWithFunction:function3d error:error]))
        return nil;
    _scale = pt_r2_scale_parse(getenv("OPTICS_INTERNAL"));
    if (_scale != PT_R2_SCALE_ONE) {
        id<MTLLibrary> library = [device newLibraryWithSource:scaleSource options:unpackOptions error:error];
        if (!library)
            return nil;
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [library newFunctionWithName:@"optics_scale_vertex"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        pd.fragmentFunction = [library newFunctionWithName:@"optics_scale_up"];
        if (!(_scaleUp = [device newRenderPipelineStateWithDescriptor:pd error:error]))
            return nil;
        pd.fragmentFunction = [library newFunctionWithName:@"optics_scale_down"];
        if (!(_scaleDown = [device newRenderPipelineStateWithDescriptor:pd error:error]))
            return nil;
    }
    _visibility = [device newBufferWithLength:8 * 8192 options:MTLResourceStorageModeShared];
    _boundVisibility = -1;
    if (!_visibility) {
        fail(error, @"Visibility buffer allocation failed");
        return nil;
    }
    _programs = [NSMutableDictionary new];
    _libraries = [NSMutableDictionary new];
    _stencilStates = [NSMutableDictionary new];
    _pendingSources = [NSMutableSet new];
    _failedSources = [NSMutableSet new];
    _compiling = dispatch_group_create();
    _log = os_log_create("org.ptolemy.optics", "execution");
    for (unsigned i = 0; i < 2; i++) {
        MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
        d.minFilter = d.magFilter = i ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        d.sAddressMode = d.tAddressMode = MTLSamplerAddressModeClampToEdge;
        _sampler[i] = [device newSamplerStateWithDescriptor:d];
        if (!_sampler[i]) {
            fail(error, @"Sampler allocation failed");
            return nil;
        }
    }
    for (unsigned c = 0; c < 8; c++)
        for (unsigned w = 0; w < 2; w++) {
            MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
            d.depthCompareFunction = (MTLCompareFunction)c;
            d.depthWriteEnabled = w;
            _depth[c][w] = [device newDepthStencilStateWithDescriptor:d];
            if (!_depth[c][w]) {
                fail(error, @"Depth state allocation failed");
                return nil;
            }
        }
    if (queue) {
        void *tag = (__bridge void *)self;
        dispatch_queue_set_specific(queue, tag, tag, NULL);
        if (dispatch_get_specific(tag) != tag) {
            dispatch_queue_set_specific(queue, tag, NULL, NULL);
            fail(error, @"Create renderer on its supplied serial owner queue");
            return nil;
        }
        _ownerQueue = queue;
    }
    store_start(device);
    return self;
}
- (void)adoptOwner {
    if (!_ownerQueue)
        _owner = pthread_self();
}
- (BOOL)isOwner {
    return _ownerQueue ? dispatch_get_specific((__bridge void *)self) == (__bridge void *)self
                       : pthread_equal(_owner, pthread_self());
}
- (BOOL)owner:(NSError **)error {
    if (![self isOwner])
        return fail(error, @"Renderer requires its encoding owner");
    std::lock_guard<std::mutex> lock(_runtime->mutex);
    if (_runtime->closed)
        return fail(error, @"Renderer closed");
    if (_runtime->failed)
        return fail(error, @"GPU command failed; inspect completion, then reset");
    return YES;
}
- (R2Lease *)charge:(uint64_t)bytes error:(NSError **)error {
    auto s = _runtime;
    R2Lease *lease = [R2Lease new];
    lease->runtime = s;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        if (bytes > _limits.residentBytes) {
            fail(error, @"One resource exceeds the resident budget");
            return nil;
        }
        if (bytes > _limits.residentBytes - s->resident) {
            fail(error, @"Resident resource budget reached", 2);
            return nil;
        }
        lease->epoch = s->epoch;
        lease->charged = bytes;
        s->resident += bytes;
        if (s->resident > s->peak)
            s->peak = s->resident;
    }
    return lease;
}
- (BOOL)valid:(R2Lease *)lease error:(NSError **)error {
    if (!lease || lease->runtime != _runtime || lease->epoch != _runtime->epoch)
        return fail(error, @"Foreign or stale resource epoch");
    return YES;
}
- (void)endRender:(unsigned)reason {
    if (_encoder) {
        if (_passReasons && reason < 12)
            _passBreak[reason]++;
        [_encoder endEncoding];
        _encoder = nil;
        _activeTarget = nil;
    }
    _passQuery = 0;
    _boundVisibility = -1; /* a new encoder starts with visibility results disabled */
    _bindingsValid = NO;
    _boundProgram = nil;
    _boundDepth = nil;
    for (unsigned j = 0; j < 16; j++) {
        _boundTexture[j] = nil;
        _boundSampler[j] = nil;
    }
}
- (BOOL)admit:(uint64_t)bytes error:(NSError **)error {
    if (![self owner:error])
        return NO;
    if (bytes > _limits.batchBytes)
        return fail(error, @"One job exceeds the batch byte budget");
    if (_jobs >= _limits.maxJobs || bytes > _limits.batchBytes - _batchBytes) {
        _busy++;
        return fail(error, @"Batch budget reached: commit, then retry", 2);
    }
    if (!_batch) {
        {
            std::lock_guard<std::mutex> lock(_runtime->mutex);
            size_t retired = _completionPrefix ? _runtime->retired.size() : _runtime->completions.size();
            if (_runtime->inflight + retired >= _limits.maxBatches) {
                _busy++;
                return fail(error, @"Completion capacity reached: poll or retry later", 2);
            }
        }
        _batch = [_queue commandBuffer];
        if (!_batch)
            return fail(error, @"Command buffer allocation failed");
        _ticket = {_runtime->epoch, ++_sequence};
        _batch.label = [NSString stringWithFormat:@"R2 epoch %llu batch %llu", _ticket.epoch, _ticket.sequence];
        _leases = [NSMutableArray new];
        _readbacks = [NSMutableArray new];
        _exports = [NSMutableArray new];
    }
    return YES;
}
- (R2Lease *)buffer:(const void *)data bytes:(size_t)length error:(NSError **)error {
    R2Lease *lease = [self charge:length error:error];
    if (!lease)
        return nil;
    lease->buffer =
            data ? [_device newBufferWithBytes:data
                                        length:length
                                       options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeTracked]
                 : [_device newBufferWithLength:length
                                        options:MTLResourceStorageModeShared | MTLResourceHazardTrackingModeTracked];
    if (!lease->buffer) {
        fail(error, @"Buffer allocation failed");
        return nil;
    }
    _bufferAllocations++;
    return lease;
}
- (R2Slice)upload:(const void *)data bytes:(size_t)n error:(NSError **)error {
    size_t aligned = (_uploadUsed + 255) & ~size_t(255);
    if (!_upload || aligned > _upload->buffer.length || n > _upload->buffer.length - aligned) {
        size_t standard = (size_t)std::min(_limits.batchBytes, UINT64_C(1) << 20), capacity = std::max(n, standard);
        id<MTLBuffer> reuse = nil;
        if (capacity == standard) {
            std::lock_guard<std::mutex> lock(_runtime->mutex);
            if (!_runtime->spare.empty()) {
                reuse = _runtime->spare.back();
                _runtime->spare.pop_back();
            }
        }
        if (reuse) {
            _upload = [self charge:capacity error:error];
            if (_upload)
                _upload->buffer = reuse;
        } else
            _upload = [self buffer:NULL bytes:capacity error:error];
        _uploadUsed = 0;
        aligned = 0;
        if (!_upload)
            return {};
        _upload->recycle = capacity == standard;
        [_leases addObject:_upload];
    }
    memcpy((uint8_t *)_upload->buffer.contents + aligned, data, n);
    _uploadUsed = aligned + n;
    _snapshotBytes += n;
    return {_upload, aligned};
}
/* What preparePVS refuses (nil: accepted). */
static NSString *routeError(id<MTLDevice> device, const PtR2ShaderIR *pvs, const PtR2ShaderInterface *vi,
                            const PtR2ShaderIR *us, const PtR2ShaderInterface *fi, const PtR2Route &r) {
    if (!pvs || !us || !vi || !fi || pvs->stage != PT_R2_SHADER_PVS || us->stage != PT_R2_SHADER_US || pvs->textures ||
        vi->input_count > 16 || r.vte > 2 || r.varyingCount > 8 || r.positionOutput >= vi->output_count ||
        r.colorOutput >= fi->output_count || r.colorOutput > 3 || r.colorMask > 15 || r.blend > 3 ||
        (r.blendState && !blendValid(r)) || !epilogueValid(r) || (us->outputs & ~(1u << r.colorOutput)) ||
        !(pvs->outputs & (1u << r.positionOutput)) ||
        (r.sampleCount != 1 && r.sampleCount != 2 && r.sampleCount != 4) ||
        ![device supportsTextureSampleCount:r.sampleCount])
        return @"Unsupported normalized stage, route, blend or sample count";
    uint32_t seen = 0;
    for (unsigned j = 0; j < r.varyingCount; j++) {
        if (r.pvsOutput[j] >= vi->output_count || r.usInput[j] >= fi->input_count || (seen & (1u << r.usInput[j])))
            return @"Invalid or duplicate varying route";
        seen |= 1u << r.usInput[j];
    }
    for (unsigned j = 0; j < vi->input_count; j++) {
        PtR2Attribute a = r.attribute[j];
        unsigned size = optics_raw_width(a.format, a.components);
        if (size == UINT_MAX || a.offset > r.stride || size > r.stride - a.offset ||
            (a.format < OPTICS_RAW_INT && r.vapSelect[j]) ||
            (a.format == OPTICS_RAW_DEFAULT && (a.offset || r.vapSelect[j])))
            return @"Unsupported or out-of-stride vertex attribute";
    }
    if (r.fogSelect > 5 || (r.fog && r.fogSelect < 4 && r.fogOutput >= vi->output_count) || r.fog > 1 ||
        r.clipPlanes > 63 || r.rop > 16 || r.textureFixed > 1 || r.pad2 || r.discard > 6 ||
        (r.generatedScale && (r.generatedScale <= PT_R2_SCALE_ONE || r.generatedScale > PT_R2_SCALE_MAX)) ||
        (r.generated >> r.varyingCount))
        return @"Unsupported fixed-function route";
    return nil;
}
static MTLCompileOptions *compileOptions() {
    MTLCompileOptions *o = [MTLCompileOptions new];
    o.mathMode = MTLMathModeSafe;
    NSMutableDictionary *macros = [NSMutableDictionary new];
    if (getenv("OPTICS_SHADERS_COLD")) /* tests of a first use: a macro per process, so Metal's shader cache misses */
        macros[@"OPTICS_COLD"] = @(getpid());
    if (const char *salt = getenv("OPTICS_SHADER_SALT")) /* tests: a cache of its own, as after an update of the MSL */
        macros[@"OPTICS_SALT"] = @(atoi(salt));
    if (macros.count)
        o.preprocessorMacros = macros;
    return o;
}
/* The program store: every program Optics prepares is kept as its inputs - both stages' words (decoded again with
 * pt_r2_shader_decode), their interfaces and the route - in ~/Library/Application Support/Ptolemy/optics-programs.bin;
 * at the next start each one's MSL is compiled again in the background (4 at a time, utility QoS) and the libraries are
 * dropped: what stays is Metal's shader cache, so the guest's draw finds its library compiled. After an update of
 * Ptolemy that changes the MSL text, the games played before do not wait for their first compiles again. A seed file
 * beside the engine (optics-programs.bin) adds programs shipped with Ptolemy. The draw path is unchanged. Off with
 * OPTICS_PROGRAM_CACHE=0 (=PATH: another file) and with OPTICS_SHADERS_COLD (first-use tests). */
namespace {
struct StoreHead {
    uint32_t magic, version, route, iface;
};
const StoreHead STORE_HEAD = {0x504f5450 /* PTOP */, 1, sizeof(PtR2Route), sizeof(PtR2ShaderInterface)};
std::mutex store_lock;
std::set<std::string> store_seen; /* records in the files or written in this session */
FILE *store_file;
int store_state; /* 0: the file not opened for writing yet, 1: open, -1: off */
bool store_fresh; /* the file is missing or of another layout: written anew */
/* Where the program store lives: OPTICS_PROGRAM_CACHE (a path; 0 turns the store off), none with
 * OPTICS_SHADERS_COLD, else ~/Library/Application Support/Ptolemy/optics-programs.bin. */
std::string store_path() {
    const char *v = getenv("OPTICS_PROGRAM_CACHE");
    if (v && *v)
        return strcmp(v, "0") ? std::string(v) : std::string();
    if (getenv("OPTICS_SHADERS_COLD") || !getenv("HOME"))
        return std::string();
    return std::string(getenv("HOME")) + "/Library/Application Support/Ptolemy/optics-programs.bin";
}
/* The records of one file ([head][uint32 length][record]...); a file of another layout gives none. */
std::vector<std::string> store_read(const std::string &path) {
    std::vector<std::string> out;
    FILE *f = path.empty() ? NULL : fopen(path.c_str(), "rb");
    if (!f)
        return out;
    StoreHead h;
    if (fread(&h, sizeof h, 1, f) == 1 && !memcmp(&h, &STORE_HEAD, sizeof h)) {
        uint32_t n;
        while (fread(&n, 4, 1, f) == 1 && n && n < (1u << 20)) {
            std::string r(n, 0);
            if (fread(&r[0], 1, n, f) != n)
                break;
            out.push_back(std::move(r));
        }
    }
    fclose(f);
    return out;
}
std::string store_record(const PtR2ShaderIR *pvs, const PtR2ShaderInterface *vi, const PtR2ShaderIR *us,
                         const PtR2ShaderInterface *fi, const PtR2Route &r) {
    std::string b((const char *)&r, sizeof r);
    b.append((const char *)vi, sizeof *vi);
    b.append((const char *)fi, sizeof *fi);
    for (const PtR2ShaderIR *ir : {pvs, us}) {
        uint32_t h[3] = {ir->stage, ir->precision, ir->word_count};
        b.append((const char *)h, sizeof h);
        b.append((const char *)ir->words, 4 * (size_t)ir->word_count);
    }
    return b;
}
/* A record back into the renderer's inputs; false if it does not parse or decode. */
bool store_parse(const std::string &b, PtR2Route *r, PtR2ShaderInterface *vi, PtR2ShaderInterface *fi, PtR2ShaderIR *pvs,
                 PtR2ShaderIR *us) {
    size_t at = sizeof *r + 2 * sizeof *vi;
    if (b.size() < at)
        return false;
    memcpy(r, b.data(), sizeof *r);
    memcpy(vi, b.data() + sizeof *r, sizeof *vi);
    memcpy(fi, b.data() + sizeof *r + sizeof *vi, sizeof *fi);
    for (PtR2ShaderIR *ir : {pvs, us}) {
        uint32_t h[3];
        if (b.size() < at + sizeof h)
            return false;
        memcpy(h, b.data() + at, sizeof h);
        at += sizeof h;
        if (!h[2] || h[2] > PT_R2_SHADER_WORDS || b.size() < at + 4 * (size_t)h[2])
            return false;
        PtR2ShaderError e;
        if (pt_r2_shader_decode((PtR2ShaderStage)h[0], (PtR2ShaderPrecision)h[1], (const uint32_t *)(b.data() + at),
                                h[2], 0, ir, &e) != PT_R2_SHADER_OK)
            return false;
        at += 4 * (size_t)h[2];
    }
    return at == b.size();
}
/* Appends a program's record to the store, once per process; the first record opens the file (written anew when it
 * was of another layout). */
void store_add(const std::string &rec) {
    std::lock_guard<std::mutex> g(store_lock);
    if (store_state < 0 || !store_seen.insert(rec).second)
        return;
    if (!store_state) {
        std::string path = store_path();
        if (!path.empty()) {
            NSString *dir = [@(path.c_str()) stringByDeletingLastPathComponent];
            [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
            store_file = fopen(path.c_str(), store_fresh ? "wb" : "ab");
            if (store_file && store_fresh && fwrite(&STORE_HEAD, sizeof STORE_HEAD, 1, store_file) != 1) {
                fclose(store_file);
                store_file = NULL;
            }
        }
        store_state = store_file ? 1 : -1;
        if (!store_file)
            return;
    }
    uint32_t n = (uint32_t)rec.size();
    std::string out((const char *)&n, 4);
    out += rec;
    if (fwrite(out.data(), 1, out.size(), store_file) == out.size()) /* one write: concurrent engines append whole records */
        fflush(store_file);
}
/* Once per process: the files' records are known (so they are not written again), and their libraries compile in the
 * background into Metal's cache. */
void store_start(id<MTLDevice> device) {
    static std::once_flag once;
    std::call_once(once, [device] {
        std::string path = store_path();
        if (path.empty()) {
            store_state = -1;
            return;
        }
        std::vector<std::string> mine = store_read(path), recs = mine;
        {
            FILE *f = fopen(path.c_str(), "rb");
            StoreHead h;
            store_fresh = !f || fread(&h, sizeof h, 1, f) != 1 || memcmp(&h, &STORE_HEAD, sizeof h);
            if (f)
                fclose(f);
        }
        char exe[4096];
        uint32_t size = sizeof exe;
        if (!_NSGetExecutablePath(exe, &size)) {
            std::string seed = std::string([[@(exe) stringByDeletingLastPathComponent] UTF8String]) + "/optics-programs.bin";
            for (std::string &r : store_read(seed))
                recs.push_back(std::move(r));
        }
        {
            std::lock_guard<std::mutex> g(store_lock);
            for (const std::string &r : recs)
                store_seen.insert(r);
        }
        if (recs.empty())
            return;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            @autoreleasepool {
                uint64_t t0 = nowNS();
                MTLCompileOptions *o = compileOptions();
                dispatch_semaphore_t slots = dispatch_semaphore_create(4);
                dispatch_group_t g = dispatch_group_create();
                NSMutableSet<NSString *> *done = [NSMutableSet new];
                std::unique_ptr<PtR2ShaderIR> pvs(new PtR2ShaderIR), us(new PtR2ShaderIR);
                unsigned compiled = 0, skipped = 0;
                for (const std::string &rec : recs) {
                    PtR2Route r;
                    PtR2ShaderInterface vi, fi;
                    NSString *vb = nil, *fb = nil;
                    if (!store_parse(rec, &r, &vi, &fi, pvs.get(), us.get()) || routeError(device, pvs.get(), &vi, us.get(), &fi, r) ||
                        !(vb = body(pvs.get(), &vi, nil)) || !(fb = body(us.get(), &fi, nil))) {
                        skipped++;
                        continue;
                    }
                    for (NSString *source in @[ vertexSource(vb, &vi, r), fragmentSource(fb, us.get(), &fi, r) ]) {
                        if ([done containsObject:source])
                            continue;
                        [done addObject:source];
                        dispatch_semaphore_wait(slots, DISPATCH_TIME_FOREVER);
                        dispatch_group_enter(g);
                        compiled++;
                        [device newLibraryWithSource:source
                                             options:o
                                   completionHandler:^(id<MTLLibrary> l, NSError *e) {
                                       (void)l;
                                       (void)e;
                                       dispatch_semaphore_signal(slots);
                                       dispatch_group_leave(g);
                                   }];
                    }
                }
                dispatch_group_wait(g, DISPATCH_TIME_FOREVER);
                fprintf(stderr, "optics: program store: %zu programs (%zu of this machine), %u libraries compiled in the background in %.1f s, %u skipped\n",
                        recs.size(), mine.size(), compiled, (nowNS() - t0) / 1e9, skipped);
            }
        });
    });
}
} // namespace
/* A draw's MSL libraries not compiled yet start compiling on Metal's threads (once per source); YES while any of them is
 * still compiling, and wake() runs as each one completes. The caller's draw waits (the guest CPU runs meanwhile) and comes
 * back to preparePVS, which finds them. An invalid route or a failed source: NO, preparePVS reports it. */
- (BOOL)startLibrariesForPVS:(const PtR2ShaderIR *)pvs
             vertexInterface:(const PtR2ShaderInterface *)vi
                    fragment:(const PtR2ShaderIR *)us
                   interface:(const PtR2ShaderInterface *)fi
                       route:(PtR2Route)r
                        wake:(void (^)(void))wake {
    if (![self owner:nil] || routeError(_device, pvs, vi, us, fi, r))
        return NO;
    NSString *vbody = body(pvs, vi, nil), *fbody = body(us, fi, nil);
    if (!vbody || !fbody)
        return NO;
    BOOL waiting = NO;
    for (NSString *source in @[ vertexSource(vbody, vi, r), fragmentSource(fbody, us, fi, r) ]) {
        {
            std::lock_guard<std::mutex> g(_libLock);
            if (_libraries[source] || [_failedSources containsObject:source])
                continue;
            waiting = YES;
            if ([_pendingSources containsObject:source])
                continue;
            [_pendingSources addObject:source];
        }
        _inFlight++;
        dispatch_group_enter(_compiling);
        uint64_t t = nowNS();
        [_device newLibraryWithSource:source
                              options:compileOptions()
                    completionHandler:^(id<MTLLibrary> l, NSError *err) {
                        (void)err;
                        uint64_t dt = nowNS() - t;
                        {
                            std::lock_guard<std::mutex> g(self->_libLock);
                            if (l) {
                                if (self->_libraries.count >= 1024)
                                    [self->_libraries removeAllObjects];
                                self->_libraries[source] = l;
                            } else
                                [self->_failedSources addObject:source];
                            [self->_pendingSources removeObject:source];
                            self->_asyncCompiles++;
                            self->_asyncNS += dt;
                            self->_asyncMaxNS = std::max(self->_asyncMaxNS, dt);
                        }
                        self->_inFlight--;
                        if (wake)
                            wake();
                        dispatch_group_leave(self->_compiling);
                    }];
    }
    return waiting;
}
- (unsigned)librariesCompiling {
    return _inFlight.load();
}
- (uint64_t)asyncStat:(int)k {
    std::lock_guard<std::mutex> g(_libLock);
    return k == 0 ? _asyncCompiles : k == 1 ? _asyncNS : _asyncMaxNS;
}
- (void)waitForLibraries {
    dispatch_group_wait(_compiling, DISPATCH_TIME_FOREVER);
}
- (BOOL)waitForLibrariesNS:(uint64_t)ns {
    return !dispatch_group_wait(_compiling, dispatch_time(DISPATCH_TIME_NOW, (int64_t)ns));
}
- (PtR2Program *)preparePVS:(const PtR2ShaderIR *)pvs
            vertexInterface:(const PtR2ShaderInterface *)vi
                   fragment:(const PtR2ShaderIR *)us
                  interface:(const PtR2ShaderInterface *)fi
                      route:(PtR2Route)r
                      error:(NSError **)error {
    if (![self owner:error])
        return nil;
    if (NSString *why = routeError(_device, pvs, vi, us, fi, r)) {
        fail(error, why);
        return nil;
    }
    NSMutableData *key = [NSMutableData new];
    word(key, 1);
    for (unsigned stage = 0; stage < 2; stage++) {
        uint8_t bytes[16384];
        size_t n = 0;
        PtR2ShaderError e = {};
        if (pt_r2_shader_key(stage ? us : pvs, stage ? fi : vi, bytes, sizeof bytes, &n, &e) != PT_R2_SHADER_OK) {
            fail(error, [NSString stringWithUTF8String:e.reason]);
            return nil;
        }
        word(key, (uint32_t)n);
        [key appendBytes:bytes length:n];
    }
    for (uint32_t v : {r.positionOutput, r.colorOutput, r.varyingCount, r.sampleCount, r.colorMask, r.blend})
        word(key, v);
    word(key, r.blendState);
    word(key, r.vte);
    word(key, r.selectVaryings);
    for (unsigned j = 0; j < 8; j++)
        for (unsigned c = 0; c < 4; c++)
            word(key, r.select[j][c]);
    for (unsigned j = 0; j < r.varyingCount; j++) {
        word(key, r.pvsOutput[j]);
        word(key, r.usInput[j]);
    }
    word(key, r.stride);
    for (unsigned j = 0; j < vi->input_count; j++) {
        word(key, r.attribute[j].offset);
        word(key, r.attribute[j].components);
        word(key, r.attribute[j].format);
        word(key, r.vapSelect[j]);
    }
    for (uint32_t v : {r.fog, r.fogSelect, r.fogOutput, r.clipPlanes, r.flat, r.rop, r.textureFixed, r.generated,
                       r.generatedShift, r.discard, r.generatedScale})
        word(key, v);
    PtR2Program *cached = _programs[key];
    if (cached)
        return cached;
    /* Explicit bounded cache: it only deduplicates, a program in use stays alive in its draws' leases and the frontend's
     * table (a cache that refused new programs once full lost more and more geometry in long sessions). Dropped whole;
     * an LRU if recompiles show up in long sessions. */
    if (_programs.count >= 512)
        [_programs removeAllObjects];
    uint64_t begin = nowNS();
    os_signpost_id_t sid = os_signpost_id_generate(_log);
    os_signpost_interval_begin(_log, sid, "R2Compile");
    NSString *vbody = body(pvs, vi, error), *fbody = body(us, fi, error);
    if (!vbody || !fbody) {
        os_signpost_interval_end(_log, sid, "R2Compile");
        return nil;
    }
    PtR2Program *p = [PtR2Program new];
    p->runtime = _runtime;
    p->epoch = _runtime->epoch;
    p->route = r;
    p->textureMask = us->textures;
    p->vertexConstantBytes = constantBytes(pvs);
    p->fragmentConstantBytes = constantBytes(us);
    NSString *vertexMSL = vertexSource(vbody, vi, r), *fragmentMSL = fragmentSource(fbody, us, fi, r);
    MTLCompileOptions *options = compileOptions();
    /* Programs that differ only in blend, write mask or other pipeline state have the same vertex or fragment MSL
     * (Halo compiled 888 programs in 38 s before this); each source is compiled once (a bounded table,
     * dropped whole like the program cache). Here the caller waits for the compile (startLibrariesForPVS does not). */
    auto library = [&](NSString *source) -> id<MTLLibrary> {
        {
            std::lock_guard<std::mutex> g(_libLock);
            id<MTLLibrary> l = _libraries[source];
            if (l)
                return l;
        }
        uint64_t t = nowNS();
        id<MTLLibrary> l = [_device newLibraryWithSource:source options:options error:error];
        t = nowNS() - t;
        _libraryNS += t;
        _libraryCompiles++;
        _libraryMaxNS = std::max(_libraryMaxNS, t);
        _librarySlow += t >= 50000000u;
        if (l) {
            std::lock_guard<std::mutex> g(_libLock);
            if (_libraries.count >= 1024)
                [_libraries removeAllObjects];
            _libraries[source] = l;
        }
        return l;
    };
    id<MTLLibrary> vl = library(vertexMSL);
    id<MTLLibrary> fl = vl ? library(fragmentMSL) : nil;
    if (fl) {
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.label = @"Radeon2 direct US/PVS";
        d.vertexFunction = [vl newFunctionWithName:@"pt_vertex"];
        d.fragmentFunction = [fl newFunctionWithName:@"pt_fragment"];
        d.rasterSampleCount = r.sampleCount;
        d.depthAttachmentPixelFormat = d.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        auto c = d.colorAttachments[0];
        c.pixelFormat = MTLPixelFormatRGBA8Unorm;
        c.writeMask = (MTLColorWriteMask)((r.colorMask & 1 ? MTLColorWriteMaskRed : 0) |
                                          (r.colorMask & 2 ? MTLColorWriteMaskGreen : 0) |
                                          (r.colorMask & 4 ? MTLColorWriteMaskBlue : 0) |
                                          (r.colorMask & 8 ? MTLColorWriteMaskAlpha : 0));
        c.blendingEnabled = r.blend != 0 && !r.rop;
        c.sourceRGBBlendFactor = r.blend == 1 ? MTLBlendFactorSourceAlpha : MTLBlendFactorOne;
        c.destinationRGBBlendFactor = r.blend == 3 ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
        c.sourceAlphaBlendFactor = MTLBlendFactorOne;
        c.destinationAlphaBlendFactor = c.destinationRGBBlendFactor;
        if (r.blendState) {
            uint32_t b = r.blendState;
            c.blendingEnabled = !r.rop; /* a logic op replaces blending (GL) */
            c.sourceRGBBlendFactor = (MTLBlendFactor)(b & 15);
            c.destinationRGBBlendFactor = (MTLBlendFactor)(b >> 4 & 15);
            c.rgbBlendOperation = (MTLBlendOperation)(b >> 8 & 7);
            c.sourceAlphaBlendFactor = (MTLBlendFactor)(b >> 12 & 15);
            c.destinationAlphaBlendFactor = (MTLBlendFactor)(b >> 16 & 15);
            c.alphaBlendOperation = (MTLBlendOperation)(b >> 20 & 7);
        }
        p->pipeline = [_device newRenderPipelineStateWithDescriptor:d error:error];
    }
    _compileNS += nowNS() - begin;
    os_signpost_interval_end(_log, sid, "R2Compile");
    if (!p->pipeline)
        return nil;
    _compiled++;
    _programs[[key copy]] = p;
    store_add(store_record(pvs, vi, us, fi, r));
    return p;
}
- (PtR2Target *)targetWidth:(uint32_t)w height:(uint32_t)h samples:(PtR2MsaaInput)input error:(NSError **)error {
    return [self targetWidth:w height:h samples:input scale:PT_R2_SCALE_ONE error:error];
}
/* scale: the internal resolution of the storage; width and height stay the guest's. A multisampled target is a 3D
 * target from the start and takes the renderer's scale; a single-sample one takes it when depth storage is first
 * bound to it (promote:), so a desktop or a 2D surface is never scaled. */
- (PtR2Target *)targetWidth:(uint32_t)w
                     height:(uint32_t)h
                    samples:(PtR2MsaaInput)input
                      scale:(unsigned)scale
                      error:(NSError **)error {
    if (![self owner:error])
        return nil;
    if (!w || !h || w > 16384 || h > 16384) { /* before the sizes are scaled */
        fail(error, @"Invalid offscreen dimensions/device");
        return nil;
    }
    uint64_t available;
    {
        std::lock_guard<std::mutex> lock(_runtime->mutex);
        available = _limits.residentBytes - _runtime->resident;
    }
    PtR2MetalSurface *s = [PtR2MetalSurface surfaceWithDevice:_device
                                                        width:pt_r2_scale_size(w, scale)
                                                       height:pt_r2_scale_size(h, scale)
                                                        input:input
                                                       budget:available
                                                        error:error];
    if (!s)
        return nil;
    if (s.samples.count != 1 && s.samples.count != 2 && s.samples.count != 4) {
        fail(error, @"No pipeline support for this sample count");
        return nil;
    }
    if (s.samples.count > 1 && scale != _scale) {
        /* From x2 the larger target already smooths edges: at most PT_R2_SCALE_MSAA_CAP samples of the guest's. */
        static_assert(PT_R2_SCALE_MSAA_CAP == 2, "aa_config bits 2:1 = 0 select 2 samples (pt_r2_msaa_plan)");
        if (pt_r2_scale_samples(s.samples.count, _scale) < s.samples.count)
            input.aa_config &= ~6u;
        return [self targetWidth:w height:h samples:input scale:_scale error:error];
    }
    MTLTextureDescriptor *d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                               width:s.color.width
                                                              height:s.color.height
                                                           mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageRenderTarget;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    if (s.samples.count > 1) {
        d.textureType = MTLTextureType2DMultisample;
        d.sampleCount = s.samples.count;
    }
    id<MTLTexture> depth = [_device newTextureWithDescriptor:d];
    if (!depth) {
        fail(error, @"Depth allocation failed");
        return nil;
    }
    if (UINT64_MAX - s.allocatedBytes < depth.allocatedSize) {
        fail(error, @"Target accounting overflow");
        return nil;
    }
    R2Lease *lease = [self charge:s.allocatedBytes + depth.allocatedSize error:error];
    if (!lease)
        return nil;
    PtR2Target *target = [PtR2Target new];
    target->lease = lease;
    target->surface = s;
    target->depth = depth;
    target->gw = w;
    target->gh = h;
    target->scale = scale;
    return target;
}
- (PtR2Texture *)textureWidth:(uint32_t)w
                       height:(uint32_t)h
                        bytes:(const void *)bytes
                     rowBytes:(size_t)row
                       layout:(OpticsPixelLayout)layout
                        error:(NSError **)error {
    unsigned bpp = optics_pixel_bytes(layout);
    if (!bpp || !h || !w || row > SIZE_MAX / h) {
        fail(error, @"Invalid linear pixel upload");
        return nil;
    }
    PtR2Levels l = {1, {0}, {row}, {0}};
    return [self textureWidth:w
                       height:h
                        faces:1
                       levels:&l
                        bytes:bytes
                       length:(size_t)(h - 1) * row + (size_t)w * bpp
                       layout:layout
                        error:error];
}
- (PtR2Texture *)textureWidth:(uint32_t)w
                       height:(uint32_t)h
                        faces:(unsigned)faces
                       levels:(const PtR2Levels *)lv
                        bytes:(const void *)bytes
                       length:(size_t)length
                       layout:(OpticsPixelLayout)layout
                        error:(NSError **)error {
    unsigned bpp = optics_pixel_bytes(layout), n = lv ? lv->levels : 0;
    if (!bytes || !w || !h || w > 16384 || h > 16384 || !bpp ||
        (layout.format >= OPTICS_PIXEL_YUYV601_SIGNED && layout.format < OPTICS_PIXEL_DXT1 && (w & 1)) || !n ||
        n > 13 || (w >> (n - 1)) + (h >> (n - 1)) == 0) {
        fail(error, @"Invalid linear pixel upload");
        return nil;
    }
    if ((faces != 1 && faces != 6) || (faces == 6 && w != h)) {
        fail(error, @"Invalid cube map upload");
        return nil;
    }
    for (unsigned k = 0; k < n; k++) { /* every row of every face of every level inside length (DXT: rows of blocks) */
        size_t lw = OPTICS_PIXEL_UNITS(layout.format, std::max(1u, w >> k)),
               lh = OPTICS_PIXEL_UNITS(layout.format, std::max(1u, h >> k)), r = lv->row[k];
        if (r < lw * bpp || r > UINT32_MAX || r > SIZE_MAX / lh ||
            (faces > 1 && lv->face[k] < (lh - 1) * r + lw * bpp)) {
            fail(error, @"Invalid linear pixel upload");
            return nil;
        }
        size_t one = (lh - 1) * r + lw * bpp, last = (faces - 1) * lv->face[k];
        if (faces > 1 && lv->face[k] > (SIZE_MAX - one) / (faces - 1)) {
            fail(error, @"Invalid cube map upload");
            return nil;
        }
        if (lv->offset[k] > length || last + one > length - lv->offset[k]) {
            fail(error, @"Invalid linear pixel upload");
            return nil;
        }
    }
    if (![self admit:length error:error])
        return nil;
    R2Slice input = [self upload:bytes bytes:length error:error];
    if (!input.lease)
        return nil;
    MTLTextureDescriptor *d =
            faces == 6 ? [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                               size:w
                                                                          mipmapped:n > 1]
                       : [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                            width:w
                                                                           height:h
                                                                        mipmapped:n > 1];
    d.mipmapLevelCount = n;
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    id<MTLTexture> tex = [_device newTextureWithDescriptor:d];
    if (!tex) {
        fail(error, @"Unpack texture allocation failed");
        return nil;
    }
    R2Lease *lease = [self charge:tex.allocatedSize error:error];
    if (!lease)
        return nil;
    lease->texture = tex;
    [self endRender:0];
    id<MTLComputeCommandEncoder> e = [_batch computeCommandEncoder];
    if (!e) {
        fail(error, @"Unpack encoder failed");
        return nil;
    }
    [e setComputePipelineState:_unpack];
    NSUInteger x = _unpack.threadExecutionWidth, y = std::min((NSUInteger)8, _unpack.maxTotalThreadsPerThreadgroup / x);
    for (unsigned k = 0; k < n; k++) {
        uint32_t lw = std::max(1u, w >> k), lh = std::max(1u, h >> k);
        uint32_t p[6] = {lw, lh, (uint32_t)lv->row[k], bpp, layout.format, 0};
        for (unsigned i = 0; i < 4; i++)
            p[5] |= (uint32_t)layout.component[i] << (i * 3);
        [e setBytes:p length:sizeof p atIndex:1];
        for (unsigned f = 0; f < faces; f++) { /* a level (and cube face) through a 2D view of it */
            id<MTLTexture> dst = faces == 1 && n == 1 ? tex
                                                      : [tex newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                               textureType:MTLTextureType2D
                                                                                    levels:NSMakeRange(k, 1)
                                                                                    slices:NSMakeRange(f, 1)];
            if (!dst) {
                [e endEncoding];
                fail(error, @"Texture level view failed");
                return nil;
            }
            [e setBuffer:input.lease->buffer offset:input.offset + lv->offset[k] + f * lv->face[k] atIndex:0];
            [e setTexture:dst atIndex:0];
            [e dispatchThreads:MTLSizeMake(lw, lh, 1) threadsPerThreadgroup:MTLSizeMake(x, y, 1)];
        }
    }
    [e endEncoding];
    PtR2Texture *result = [PtR2Texture new];
    result->lease = lease;
    result->status.store(0);
    [_leases addObject:result];
    [_exports addObject:result];
    _batchBytes += length;
    _jobs++;
    return result;
}
- (PtR2Texture *)textureWidth:(uint32_t)w
                       height:(uint32_t)h
                        depth:(uint32_t)z
                       levels:(const PtR2Levels *)lv
                        bytes:(const void *)bytes
                       length:(size_t)length
                       layout:(OpticsPixelLayout)layout
                        error:(NSError **)error {
    unsigned bpp = optics_pixel_bytes(layout), n = lv ? lv->levels : 0;
    if (!bytes || !w || !h || !z || w > 2048 || h > 2048 || z > 2048 || !bpp ||
        layout.format >= OPTICS_PIXEL_YUYV601_SIGNED || !n || n > 12) {
        fail(error, @"Invalid 3D texture upload");
        return nil;
    }
    for (unsigned k = 0; k < n; k++) { /* every row of every slice of every level inside length */
        size_t lw = std::max(1u, w >> k), lh = std::max(1u, h >> k), lz = std::max(1u, z >> k), r = lv->row[k], one;
        if (r < lw * bpp || r > UINT32_MAX || r > SIZE_MAX / lh ||
            ((one = (lh - 1) * r + lw * bpp) > lv->face[k] && lz > 1) || lv->face[k] > UINT32_MAX ||
            (lz > 1 && lv->face[k] > (SIZE_MAX - one) / (lz - 1)) || lv->offset[k] > length ||
            (lz - 1) * lv->face[k] + one > length - lv->offset[k]) {
            fail(error, @"Invalid 3D texture upload");
            return nil;
        }
    }
    if (![self admit:length error:error])
        return nil;
    R2Slice input = [self upload:bytes bytes:length error:error];
    if (!input.lease)
        return nil;
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = MTLTextureType3D;
    d.pixelFormat = MTLPixelFormatRGBA8Unorm;
    d.width = w;
    d.height = h;
    d.depth = z;
    d.mipmapLevelCount = n;
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    id<MTLTexture> tex = [_device newTextureWithDescriptor:d];
    if (!tex) {
        fail(error, @"Unpack texture allocation failed");
        return nil;
    }
    R2Lease *lease = [self charge:tex.allocatedSize error:error];
    if (!lease)
        return nil;
    lease->texture = tex;
    [self endRender:0];
    id<MTLComputeCommandEncoder> e = [_batch computeCommandEncoder];
    if (!e) {
        fail(error, @"Unpack encoder failed");
        return nil;
    }
    NSUInteger x = _unpack3d.threadExecutionWidth,
               y = std::min((NSUInteger)8, _unpack3d.maxTotalThreadsPerThreadgroup / x);
    [e setComputePipelineState:_unpack3d];
    for (unsigned k = 0; k < n; k++) { /* a level through a 3D view of it */
        uint32_t lw = std::max(1u, w >> k), lh = std::max(1u, h >> k), lz = std::max(1u, z >> k);
        uint32_t p[7] = {lw, lh, (uint32_t)lv->row[k], bpp, layout.format, 0, (uint32_t)lv->face[k]};
        for (unsigned i = 0; i < 4; i++)
            p[5] |= (uint32_t)layout.component[i] << (i * 3);
        id<MTLTexture> dst = n == 1 ? tex
                                    : [tex newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                             textureType:MTLTextureType3D
                                                                  levels:NSMakeRange(k, 1)
                                                                  slices:NSMakeRange(0, 1)];
        if (!dst) {
            [e endEncoding];
            fail(error, @"Texture level view failed");
            return nil;
        }
        [e setBytes:p length:sizeof p atIndex:1];
        [e setBuffer:input.lease->buffer offset:input.offset + lv->offset[k] atIndex:0];
        [e setTexture:dst atIndex:0];
        [e dispatchThreads:MTLSizeMake(lw, lh, lz) threadsPerThreadgroup:MTLSizeMake(x, y, 1)];
    }
    [e endEncoding];
    PtR2Texture *result = [PtR2Texture new];
    result->lease = lease;
    result->status.store(0);
    [_leases addObject:result];
    [_exports addObject:result];
    _batchBytes += length;
    _jobs++;
    return result;
}
- (PtR2Texture *)floatTextureWidth:(uint32_t)w
                            height:(uint32_t)h
                            levels:(unsigned)n
                             bytes:(const float *)rgba
                             error:(NSError **)error {
    if (![self owner:error])
        return nil;
    if (!rgba || !w || !h || w > 16384 || h > 16384 || !n || n > 13 || (w >> (n - 1)) + (h >> (n - 1)) == 0) {
        fail(error, @"Invalid float texture upload");
        return nil;
    }
    uint64_t size = 0;
    for (unsigned k = 0; k < n; k++)
        size += (uint64_t)std::max(1u, w >> k) * std::max(1u, h >> k) * 16;
    if (![self admit:size error:error])
        return nil;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                                 width:w
                                                                                height:h
                                                                             mipmapped:n > 1];
    d.mipmapLevelCount = n;
    d.storageMode = MTLStorageModeShared;
    d.usage = MTLTextureUsageShaderRead;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    id<MTLTexture> tex = [_device newTextureWithDescriptor:d];
    if (!tex) {
        fail(error, @"Texture allocation failed");
        return nil;
    }
    R2Lease *lease = [self charge:tex.allocatedSize error:error];
    if (!lease)
        return nil;
    for (unsigned k = 0; k < n; k++) {
        uint32_t lw = std::max(1u, w >> k), lh = std::max(1u, h >> k);
        [tex replaceRegion:MTLRegionMake2D(0, 0, lw, lh) mipmapLevel:k withBytes:rgba bytesPerRow:(NSUInteger)lw * 16];
        rgba += (size_t)lw * lh * 4;
    }
    lease->texture = tex;
    PtR2Texture *result = [PtR2Texture new];
    result->lease = lease;
    result->status.store(1);
    _jobs++;
    _batchBytes += size;
    return result;
}
- (BOOL)renderTarget:(PtR2Target *)t
           clearMask:(uint8_t)mask
               color:(MTLClearColor)color
               depth:(double)depth
             stencil:(uint8_t)stencil
               error:(NSError **)error {
    if (!t->initialized && mask != 7)
        return fail(error, @"Unselected attachment planes require initialization/import");
    if (_encoder && _activeTarget == t && !mask)
        return YES;
    [self endRender:1];
    MTLRenderPassDescriptor *pass = [t->surface renderPassWithLoad:mask & 1 ? MTLLoadActionClear : MTLLoadActionLoad
                                                             clear:color];
    PtR2Depth *z = t->bound;
    BOOL fresh = z && !z->initialized; /* uninitialized independent storage starts at 1 / 0 */
    pass.depthAttachment.texture = z ? z->lease->texture : t->depth;
    pass.depthAttachment.loadAction = mask & 2 || fresh ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = mask & 2 ? depth : 1;
    pass.stencilAttachment.texture = pass.depthAttachment.texture;
    pass.stencilAttachment.loadAction = mask & 4 || fresh ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.stencilAttachment.storeAction = MTLStoreActionStore;
    pass.stencilAttachment.clearStencil = mask & 4 ? stencil : 0;
    pass.visibilityResultBuffer = _visibility;
    _encoder = [_batch renderCommandEncoderWithDescriptor:pass];
    if (!_encoder)
        return fail(error, @"Render encoder allocation failed");
    _activeTarget = t;
    [_leases addObject:t];
    if (z) {
        [_leases addObject:z];
        z->initialized = YES;
    }
    _passes++;
    t->initialized = YES;
    t->stale = YES;
    return YES;
}
- (PtR2Depth *)depthWidth:(uint32_t)w height:(uint32_t)h error:(NSError **)error {
    if (![self owner:error])
        return nil;
    if (!w || !h || w > 16384 || h > 16384) {
        fail(error, @"Invalid depth storage size");
        return nil;
    }
    /* Depth storage is for 3D targets only: always at the internal resolution. */
    uint32_t tw = pt_r2_scale_size(w, _scale), th = pt_r2_scale_size(h, _scale);
    if (tw > 16384 || th > 16384) {
        fail(error, @"Depth storage too large at the internal resolution");
        return nil;
    }
    MTLTextureDescriptor *d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                               width:tw
                                                              height:th
                                                           mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageRenderTarget;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    id<MTLTexture> tex = [_device newTextureWithDescriptor:d];
    if (!tex) {
        fail(error, @"Depth allocation failed");
        return nil;
    }
    R2Lease *lease = [self charge:tex.allocatedSize error:error];
    if (!lease)
        return nil;
    lease->texture = tex;
    PtR2Depth *z = [PtR2Depth new];
    z->lease = lease;
    z->gw = w;
    z->gh = h;
    z->scale = _scale;
    return z;
}
- (BOOL)target:(PtR2Target *)t depth:(PtR2Depth *)z error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error] ||
        (z && ![self valid:z->lease error:error]))
        return NO;
    if (z && (z.width < t.width || z.height < t.height || t->surface.samples.count != 1))
        return fail(error, @"Depth storage must cover a single-sample target");
    if (t->bound == z)
        return YES;
    if (z && t->scale != z->scale && ![self promote:t error:error])
        return NO;
    if (_activeTarget == t)
        [self endRender:2];
    t->bound = z;
    return YES;
}
/* The first depth storage bound to a single-sample target makes it a 3D target: from here it is drawn at the internal
 * resolution. Its colour so far is enlarged into the new storage; the guest-size texture stays as the reduced colour,
 * so views the caller already holds keep showing the target. */
- (BOOL)promote:(PtR2Target *)t error:(NSError **)error {
    if (![self admit:0 error:error])
        return NO;
    PtR2Target *big = [self targetWidth:t->gw height:t->gh samples:(PtR2MsaaInput) {} scale:_scale error:error];
    if (!big)
        return NO;
    [self endRender:2];
    t->reduced = t->surface.color;
    /* ponytail: the old lease stays charged for the guest-size depth it let go; charge the colour alone if budgets
     * get tight. */
    t->reducedLease = t->lease;
    t->surface = big->surface;
    t->depth = big->depth;
    t->lease = big->lease;
    t->scale = _scale;
    if (!t->initialized)
        return YES;
    /* The new storage's own depth and stencil start as an import leaves them: 1 and 0. */
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.depthAttachment.texture = t->depth;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.clearDepth = 1;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.stencilAttachment.texture = t->depth;
    pass.stencilAttachment.loadAction = MTLLoadActionClear;
    pass.stencilAttachment.clearStencil = 0;
    pass.stencilAttachment.storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [_batch renderCommandEncoderWithDescriptor:pass];
    if (!e)
        return fail(error, @"Depth clear encoder failed");
    [e endEncoding];
    _passes++;
    if (![self enlarge:t->reduced x:0 y:0 width:t->gw height:t->gh into:t x:0 y:0 error:error])
        return NO;
    t->stale = NO; /* the reduced colour is what was enlarged */
    return YES;
}
/* Guest-size colour (the rectangle at sx, sy) into a scaled target's rectangle at dx, dy, all in guest pixels: each
 * texel takes the guest pixel under its centre, so at x2 what goes in reads back unchanged (at x1.5 a texel between
 * two pixels takes the second: see pt_r2_scale_guest). The batch exists (admit:). */
- (BOOL)enlarge:(id<MTLTexture>)from
              x:(uint32_t)sx
              y:(uint32_t)sy
          width:(uint32_t)w
         height:(uint32_t)h
           into:(PtR2Target *)t
              x:(uint32_t)dx
              y:(uint32_t)dy
          error:(NSError **)error {
    [self endRender:6];
    unsigned k = t->scale;
    id<MTLTexture> to = t->surface.color;
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = to;
    pass.colorAttachments[0].loadAction =
            !dx && !dy && w == t->gw && h == t->gh ? MTLLoadActionDontCare : MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [_batch renderCommandEncoderWithDescriptor:pass];
    if (!e)
        return fail(error, @"Scale encoder failed");
    /* Guest pixels [a, b) own the texels [first(a), first(b)); the target's last texel belongs to its last pixel. */
    NSUInteger x0 = pt_r2_scale_first(dx, k), y0 = pt_r2_scale_first(dy, k),
               x1 = dx + w == t->gw ? to.width : (NSUInteger)pt_r2_scale_first(dx + w, k),
               y1 = dy + h == t->gh ? to.height : (NSUInteger)pt_r2_scale_first(dy + h, k);
    MTLScissorRect box = {x0, y0, x1 - x0, y1 - y0};
    const int32_t p[5] = {(int32_t)k, (int32_t)sx - (int32_t)dx, (int32_t)sy - (int32_t)dy, (int32_t)(sx + w - 1),
                          (int32_t)(sy + h - 1)};
    [e setRenderPipelineState:_scaleUp];
    [e setScissorRect:box];
    [e setFragmentTexture:from atIndex:0];
    [e setFragmentBytes:p length:sizeof p atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    t->stale = YES;
    [_leases addObject:t];
    _passes++;
    return YES;
}
/* A target's single-sample colour at the guest's size: what export, the scanout snapshot, sampling as a texture and
 * copies out read. The target's own colour when it is not scaled; else its reduced colour (pt_r2_scale_down8 on the
 * GPU), reduced again only after the target changed. A multisampled target is resolved by the caller first and reduced
 * on every call (the resolve control may differ). The batch exists (admit:). */
- (id<MTLTexture>)reduced:(PtR2Target *)t error:(NSError **)error {
    if (t->scale == PT_R2_SCALE_ONE)
        return t->surface.resolved;
    if (!t->reduced) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                     width:t->gw
                                                                                    height:t->gh
                                                                                 mipmapped:NO];
        d.storageMode = MTLStorageModePrivate;
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        d.hazardTrackingMode = MTLHazardTrackingModeTracked;
        id<MTLTexture> tex = [_device newTextureWithDescriptor:d];
        if (!tex) {
            fail(error, @"Reduced colour allocation failed");
            return nil;
        }
        R2Lease *lease = [self charge:tex.allocatedSize error:error];
        if (!lease)
            return nil;
        t->reduced = tex;
        t->reducedLease = lease;
        t->stale = YES;
    }
    if (!t->stale && t->surface.samples.count == 1)
        return t->reduced;
    [self endRender:6];
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = t->reduced;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [_batch renderCommandEncoderWithDescriptor:pass];
    if (!e) {
        fail(error, @"Scale encoder failed");
        return nil;
    }
    const int32_t p[5] = {(int32_t)t->scale, (int32_t)(t->gw * t->scale), (int32_t)(t->gh * t->scale), 0, 0};
    [e setRenderPipelineState:_scaleDown];
    [e setFragmentTexture:t->surface.resolved atIndex:0];
    [e setFragmentBytes:p length:sizeof p atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    t->stale = NO;
    [_leases addObject:t];
    _passes++;
    return t->reduced;
}
/* A rectangle between single-sample colours of two targets, one of them at least scaled (a multisampled source is
 * resolved by the caller). A 3D target copied into another makes that one a 3D target too: the copy is a buffer swap,
 * and its destination is what is shown or sampled next. Then the texels are copied as they are where the two
 * rectangles' texels line up - always at x2, at x1.5 when the origins differ by an even number of pixels - and
 * otherwise through the guest's size: reduced, then enlarged. The batch exists (admit:). */
- (BOOL)copyScaled:(PtR2Target *)src
                 x:(uint32_t)sx
                 y:(uint32_t)sy
             width:(uint32_t)w
            height:(uint32_t)h
                to:(PtR2Target *)dst
                 x:(uint32_t)dx
                 y:(uint32_t)dy
             error:(NSError **)error {
    if (dst->scale == PT_R2_SCALE_ONE && ![self promote:dst error:error])
        return NO;
    unsigned k = dst->scale;
    if (src->scale == k && (k == PT_R2_SCALE_MAX || !(((sx ^ dx) | (sy ^ dy)) & 1))) {
        [self endRender:6];
        id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
        if (!e)
            return fail(error, @"Copy blit encoder failed");
        uint32_t x0 = pt_r2_scale_first(sx, k), y0 = pt_r2_scale_first(sy, k);
        [e copyFromTexture:src->surface.resolved
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(x0, y0, 0)
                       sourceSize:MTLSizeMake(pt_r2_scale_first(sx + w, k) - x0, pt_r2_scale_first(sy + h, k) - y0, 1)
                        toTexture:dst->surface.color
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(pt_r2_scale_first(dx, k), pt_r2_scale_first(dy, k), 0)];
        [e endEncoding];
        dst->stale = YES;
        [_leases addObject:dst];
    } else {
        id<MTLTexture> from = [self reduced:src error:error];
        if (!from || ![self enlarge:from x:sx y:sy width:w height:h into:dst x:dx y:dy error:error])
            return NO;
    }
    [_leases addObject:src];
    _jobs++;
    return YES;
}
/* A view (view:) of a scaled target shows its reduced colour: brought up to date here, before the texture is read. */
- (BOOL)current:(PtR2Texture *)x error:(NSError **)error {
    PtR2Target *of = x->viewOf;
    return !of || of->scale == PT_R2_SCALE_ONE || !of->stale || [self reduced:of error:error] != nil;
}
- (BOOL)clearDepth:(PtR2Depth *)z mask:(uint8_t)mask depth:(double)v stencil:(uint8_t)s error:(NSError **)error {
    if (![self owner:error] || ![self valid:z ? z->lease : nil error:error])
        return NO;
    if (!(mask & 6) || (mask & ~6u) || !std::isfinite(v) || v < 0 || v > 1)
        return fail(error, @"Invalid depth storage clear");
    if (![self admit:0 error:error])
        return NO;
    [self endRender:3];
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    BOOL fresh = !z->initialized;
    pass.depthAttachment.texture = z->lease->texture;
    pass.depthAttachment.loadAction = mask & 2 || fresh ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.depthAttachment.clearDepth = mask & 2 ? v : 1;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.stencilAttachment.texture = z->lease->texture;
    pass.stencilAttachment.loadAction = mask & 4 || fresh ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.stencilAttachment.clearStencil = mask & 4 ? s : 0;
    pass.stencilAttachment.storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [_batch renderCommandEncoderWithDescriptor:pass];
    if (!e)
        return fail(error, @"Depth clear encoder failed");
    [e endEncoding];
    z->initialized = YES;
    [_leases addObject:z];
    _jobs++;
    _passes++;
    return YES;
}
- (BOOL)copyDepth:(PtR2Depth *)src to:(PtR2Depth *)dst error:(NSError **)error {
    if (![self owner:error] || ![self valid:src ? src->lease : nil error:error] ||
        ![self valid:dst ? dst->lease : nil error:error])
        return NO;
    if (src == dst || src.width > dst.width || src.height > dst.height)
        return fail(error, @"Invalid depth growth copy");
    if (![self clearDepth:dst mask:6 depth:1 stencil:0 error:error])
        return NO;
    if (!src->initialized)
        return YES;
    [self endRender:4];
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e)
        return fail(error, @"Depth growth blit encoder failed");
    [e copyFromTexture:src->lease->texture
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(src->lease->texture.width, src->lease->texture.height, 1)
                    toTexture:dst->lease->texture
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
    [e endEncoding];
    [_leases addObject:src];
    [_leases addObject:dst];
    _jobs++;
    return YES;
}
- (BOOL)loadDepth:(PtR2Depth *)z
                y:(uint32_t)y
           height:(uint32_t)h
            bytes:(const void *)bytes
           format:(unsigned)f
            error:(NSError **)error {
    if (![self owner:error] || ![self valid:z ? z->lease : nil error:error])
        return NO;
    unsigned bpp = optics_depth_bytes(f);
    if (!bpp || !bytes || !h || y > z.height || h > z.height - y)
        return fail(error, @"Invalid linear depth import");
    /* The storage rows of guest rows [y, y + h), th of them from t0: each texel takes the guest pixel under its centre
     * (at PT_R2_SCALE_ONE the same rows and pixels). */
    unsigned k = z->scale;
    uint32_t tw = (uint32_t)z->lease->texture.width, t0 = pt_r2_scale_first(y, k),
             th = (y + h == z.height ? (uint32_t)z->lease->texture.height : pt_r2_scale_first(y + h, k)) - t0;
    size_t row = (tw * 4u + 255u) & ~size_t(255), srow = (tw + 255u) & ~size_t(255), n = (row + srow) * th;
    if (![self admit:n error:error])
        return NO;
    R2Lease *b = [self buffer:NULL bytes:n error:error];
    if (!b)
        return NO;
    uint8_t *dst = (uint8_t *)b->buffer.contents;
    for (unsigned j = 0; j < th; j++)
        for (unsigned x = 0; x < tw; x++) {
            uint32_t gy = std::min(pt_r2_scale_guest(t0 + j, k), y + h - 1) - y,
                     gx = std::min(pt_r2_scale_guest(x, k), z.width - 1);
            float v;
            uint8_t s;
            optics_depth_unpack(f, (const uint8_t *)bytes + ((size_t)gy * z.width + gx) * bpp, &v, &s);
            memcpy(dst + j * row + x * 4, &v, 4);
            dst[row * th + j * srow + x] = s;
        }
    if (!z->initialized && ![self clearDepth:z mask:6 depth:1 stencil:0 error:error])
        return NO;
    [self endRender:5];
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e)
        return fail(error, @"Depth import encoder failed");
    [e copyFromBuffer:b->buffer
                   sourceOffset:0
              sourceBytesPerRow:row
            sourceBytesPerImage:row * th
                     sourceSize:MTLSizeMake(tw, th, 1)
                      toTexture:z->lease->texture
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:MTLOriginMake(0, t0, 0)
                        options:MTLBlitOptionDepthFromDepthStencil];
    [e copyFromBuffer:b->buffer
                   sourceOffset:row * th
              sourceBytesPerRow:srow
            sourceBytesPerImage:srow * th
                     sourceSize:MTLSizeMake(tw, th, 1)
                      toTexture:z->lease->texture
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:MTLOriginMake(0, t0, 0)
                        options:MTLBlitOptionStencilFromDepthStencil];
    [e endEncoding];
    [_leases addObject:b];
    [_leases addObject:z];
    _jobs++;
    _batchBytes += n;
    return YES;
}
- (PtR2Readback *)readbackDepth:(PtR2Depth *)z y:(uint32_t)y0 height:(uint32_t)h error:(NSError **)error {
    if (![self owner:error] || ![self valid:z ? z->lease : nil error:error])
        return nil;
    if (!h || y0 > z.height || h > z.height - y0 || !z->initialized) {
        fail(error, @"Invalid depth export");
        return nil;
    }
    /* The storage rows of guest rows [y0, y0 + h), read whole; copyDepthReadback picks each guest pixel's centre texel.
     * ponytail: at x2 four times the guest's bytes in one job; rows of centre texels only if the batch byte budget
     * is ever hit (46 MB for 1920 x 1200). */
    unsigned k = z->scale;
    uint32_t tw = (uint32_t)z->lease->texture.width, t0 = pt_r2_scale_first(y0, k),
             th = pt_r2_scale_first(y0 + h, k) - t0;
    size_t row = (tw * 4u + 255u) & ~size_t(255), srow = (tw + 255u) & ~size_t(255), n = (row + srow) * th;
    if (![self admit:n error:error])
        return nil;
    R2Lease *b = [self buffer:NULL bytes:n error:error];
    if (!b)
        return nil;
    [self endRender:5];
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e) {
        fail(error, @"Depth export encoder failed");
        return nil;
    }
    [e copyFromTexture:z->lease->texture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, t0, 0)
                          sourceSize:MTLSizeMake(tw, th, 1)
                            toBuffer:b->buffer
                   destinationOffset:0
              destinationBytesPerRow:row
            destinationBytesPerImage:row * th
                             options:MTLBlitOptionDepthFromDepthStencil];
    [e copyFromTexture:z->lease->texture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, t0, 0)
                          sourceSize:MTLSizeMake(tw, th, 1)
                            toBuffer:b->buffer
                   destinationOffset:row * th
              destinationBytesPerRow:srow
            destinationBytesPerImage:srow * th
                             options:MTLBlitOptionStencilFromDepthStencil];
    [e endEncoding];
    PtR2Readback *r = [PtR2Readback new];
    r->lease = b;
    r->rowBytes = row;
    r->stencilRow = srow;
    r->depthPlanes = YES;
    r->width = z.width;
    r->height = h;
    r->scale = k;
    r->originY = y0;
    r->rows = th;
    r->status.store(0);
    [_readbacks addObject:r];
    [_leases addObject:r];
    [_leases addObject:z];
    _jobs++;
    _batchBytes += n;
    return r;
}
- (NSData *)copyDepthReadback:(PtR2Readback *)r format:(unsigned)f error:(NSError **)error {
    if (![self owner:error] || ![self valid:r ? r->lease : nil error:error])
        return nil;
    unsigned bpp = optics_depth_bytes(f);
    if (!bpp || !r->depthPlanes) {
        fail(error, @"Wrong depth readback format");
        return nil;
    }
    if (r->status.load(std::memory_order_acquire) != 1) {
        fail(error, @"Depth readback not successfully completed", 2);
        return nil;
    }
    const unsigned w = r.width, h = r.height; /* once: a message per pixel cost seconds at 4K (u239b) */
    NSMutableData *out = [NSMutableData dataWithLength:(size_t)w * h * bpp];
    uint8_t *o = (uint8_t *)out.mutableBytes;
    const uint8_t *p = (const uint8_t *)r->lease->buffer.contents;
    uint32_t t0 = pt_r2_scale_first(r->originY, r->scale);
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            /* not averaged: the texel under the guest pixel's centre (at PT_R2_SCALE_ONE the pixel itself) */
            uint32_t ty = pt_r2_scale_centre(r->originY + y, r->scale) - t0, tx = pt_r2_scale_centre(x, r->scale);
            float v;
            memcpy(&v, p + ty * r->rowBytes + tx * 4, 4);
            uint8_t s = p[r->rowBytes * r->rows + ty * r->stencilRow + tx];
            if (!optics_depth_pack(f, v, s, o + ((size_t)y * w + x) * bpp)) {
                fail(error, @"GPU depth is outside normalized integer range");
                return nil;
            }
        }
    return out;
}
- (BOOL)resolveDepth:(PtR2Target *)t into:(PtR2Depth *)z error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error] ||
        ![self valid:z ? z->lease : nil error:error])
        return NO;
    if (t->surface.samples.count < 2 || !t->initialized || z.width != t.width || z.height != t.height ||
        z->scale != t->scale)
        return fail(error, @"Depth resolve needs an initialized multisampled target of the storage's size");
    if (![self admit:0 error:error])
        return NO;
    [self endRender:5];
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.depthAttachment.texture = t->depth;
    pass.depthAttachment.loadAction = MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
    pass.depthAttachment.resolveTexture = z->lease->texture;
    pass.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
    pass.stencilAttachment.texture = t->depth;
    pass.stencilAttachment.loadAction = MTLLoadActionLoad;
    pass.stencilAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
    pass.stencilAttachment.resolveTexture = z->lease->texture;
    pass.stencilAttachment.stencilResolveFilter = MTLMultisampleStencilResolveFilterSample0;
    id<MTLRenderCommandEncoder> e = [_batch renderCommandEncoderWithDescriptor:pass];
    if (!e)
        return fail(error, @"Depth resolve encoder failed");
    [e endEncoding];
    z->initialized = YES;
    [_leases addObject:t];
    [_leases addObject:z];
    _jobs++;
    _passes++;
    return YES;
}
/* Initialize a single-sample target's colour plane from an immutable texture of the same size (import of a
 * guest surface the GPU does not own yet); its own depth/stencil are cleared. Encoded in the current batch, no wait. */
- (PtR2Texture *)view:(PtR2Target *)t component:(const uint8_t *)c error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error])
        return nil;
    if (!c || t->surface.samples.count != 1 || !t->initialized || c[0] > 5 || c[1] > 5 || c[2] > 5 || c[3] > 5) {
        fail(error, @"View needs an initialized single-sample target and components 0..5");
        return nil;
    }
    auto channel = [](uint8_t x) {
        return x < 4    ? (MTLTextureSwizzle)(MTLTextureSwizzleRed + x)
               : x == 4 ? MTLTextureSwizzleZero
                        : MTLTextureSwizzleOne;
    };
    id<MTLTexture> v = [(t->scale == PT_R2_SCALE_ONE ? t->surface.color : t->reduced)
            newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm
                              textureType:MTLTextureType2D
                                   levels:NSMakeRange(0, 1)
                                   slices:NSMakeRange(0, 1)
                                  swizzle:MTLTextureSwizzleChannelsMake(channel(c[0]), channel(c[1]), channel(c[2]),
                                                                        channel(c[3]))];
    if (!v) {
        fail(error, @"Texture view creation failed");
        return nil;
    }
    R2Lease *lease = [R2Lease new];
    lease->runtime = _runtime;
    lease->epoch = _runtime->epoch;
    lease->texture = v;
    lease->parent = t->lease; /* storage charged to the target */
    PtR2Texture *r = [PtR2Texture new];
    r->lease = lease;
    r->status.store(0);
    r->viewOf = t;
    return r; /* Borrowed mutable view: never a completed host frame. */
}
- (PtR2Texture *)copyView:(PtR2Target *)t
                    width:(uint32_t)w
                   height:(uint32_t)h
                component:(const uint8_t *)c
                    error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error])
        return nil;
    if (!c || t->surface.samples.count != 1 || !t->initialized || !w || !h || w > t.width || h > t.height || c[0] > 5 ||
        c[1] > 5 || c[2] > 5 || c[3] > 5) {
        fail(error, @"Region view needs an initialized single-sample target, an in-bounds size and components 0..5");
        return nil;
    }
    if (![self admit:0 error:error])
        return nil;
    id<MTLTexture> src = [self reduced:t error:error];
    if (!src)
        return nil;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:src.pixelFormat
                                                                                 width:w
                                                                                height:h
                                                                             mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    id<MTLTexture> tex = [_device newTextureWithDescriptor:d];
    if (!tex) {
        fail(error, @"Region texture allocation failed");
        return nil;
    }
    R2Lease *lease = [self charge:tex.allocatedSize error:error];
    if (!lease)
        return nil;
    auto channel = [](uint8_t x) {
        return x < 4    ? (MTLTextureSwizzle)(MTLTextureSwizzleRed + x)
               : x == 4 ? MTLTextureSwizzleZero
                        : MTLTextureSwizzleOne;
    };
    id<MTLTexture> v = [tex newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                              textureType:MTLTextureType2D
                                                   levels:NSMakeRange(0, 1)
                                                   slices:NSMakeRange(0, 1)
                                                  swizzle:MTLTextureSwizzleChannelsMake(channel(c[0]), channel(c[1]),
                                                                                        channel(c[2]), channel(c[3]))];
    if (!v) {
        fail(error, @"Texture view creation failed");
        return nil;
    }
    lease->texture = v; /* the view keeps its storage */
    [self endRender:6];
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e) {
        fail(error, @"Region blit encoder failed");
        return nil;
    }
    [e copyFromTexture:src
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(w, h, 1)
                    toTexture:tex
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
    [e endEncoding];
    PtR2Texture *r = [PtR2Texture new];
    r->lease = lease;
    r->status.store(0);
    [_leases addObject:t];
    [_leases addObject:r];
    [_exports addObject:r];
    _jobs++;
    return r;
}
- (BOOL)copy:(PtR2Target *)src
             x:(uint32_t)sx
             y:(uint32_t)sy
         width:(uint32_t)w
        height:(uint32_t)h
            to:(PtR2Target *)dst
             x:(uint32_t)dx
             y:(uint32_t)dy
         error:(NSError **)error {
    if (![self owner:error] || ![self valid:src ? src->lease : nil error:error] ||
        ![self valid:dst ? dst->lease : nil error:error])
        return NO;
    if (src == dst || src->surface.samples.count != 1 || dst->surface.samples.count != 1 || !src->initialized ||
        !dst->initialized || !w || !h || sx > src.width || w > src.width - sx || sy > src.height ||
        h > src.height - sy || dx > dst.width || w > dst.width - dx || dy > dst.height || h > dst.height - dy)
        return fail(error, @"Copy needs two initialized single-sample targets and an in-bounds rectangle");
    if (![self admit:0 error:error])
        return NO;
    if (src->scale != PT_R2_SCALE_ONE || dst->scale != PT_R2_SCALE_ONE)
        return [self copyScaled:src x:sx y:sy width:w height:h to:dst x:dx y:dy error:error];
    id<MTLTexture> from = src->surface.color;
    [self endRender:6];
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e)
        return fail(error, @"Copy blit encoder failed");
    [e copyFromTexture:from
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(sx, sy, 0)
                   sourceSize:MTLSizeMake(w, h, 1)
                    toTexture:dst->surface.color
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(dx, dy, 0)];
    [e endEncoding];
    [_leases addObject:src];
    [_leases addObject:dst];
    _jobs++;
    return YES;
}
- (BOOL)resolve:(PtR2Target *)src
        control:(uint32_t)c
              x:(uint32_t)sx
              y:(uint32_t)sy
          width:(uint32_t)w
         height:(uint32_t)h
             to:(PtR2Target *)dst
              x:(uint32_t)dx
              y:(uint32_t)dy
          error:(NSError **)error {
    if (![self owner:error] || ![self valid:src ? src->lease : nil error:error] ||
        ![self valid:dst ? dst->lease : nil error:error])
        return NO;
    if (src == dst || src->surface.samples.count < 2 || dst->surface.samples.count != 1 || !src->initialized ||
        !dst->initialized || !w || !h || sx > src.width || w > src.width - sx || sy > src.height ||
        h > src.height - sy || dx > dst.width || w > dst.width - dx || dy > dst.height || h > dst.height - dy)
        return fail(
                error,
                @"Resolve needs an initialized multisampled source, a single-sample target and an in-bounds rectangle");
    if (![self admit:0 error:error] || ![self resolve:src control:c error:error])
        return NO;
    if (src->scale != PT_R2_SCALE_ONE || dst->scale != PT_R2_SCALE_ONE)
        return [self copyScaled:src x:sx y:sy width:w height:h to:dst x:dx y:dy error:error];
    id<MTLTexture> from = src->surface.resolved;
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e)
        return fail(error, @"Resolve blit encoder failed");
    [e copyFromTexture:from
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(sx, sy, 0)
                   sourceSize:MTLSizeMake(w, h, 1)
                    toTexture:dst->surface.color
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(dx, dy, 0)];
    [e endEncoding];
    [_leases addObject:src];
    [_leases addObject:dst];
    _jobs++;
    return YES;
}
- (BOOL)load:(PtR2Target *)t from:(PtR2Texture *)src error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error] ||
        ![self valid:src ? src->lease : nil error:error])
        return NO;
    if (t->surface.samples.count != 1 || src->lease->texture.width != t.width || src->lease->texture.height != t.height)
        return fail(error, @"Import needs a single-sample target of the texture's size");
    if (t->scale != PT_R2_SCALE_ONE)
        return fail(error, @"Import into a target already at the internal resolution");
    if (![self admit:0 error:error] || ![self current:src error:error])
        return NO;
    [self endRender:7];
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e)
        return fail(error, @"Import blit encoder failed");
    [e copyFromTexture:src->lease->texture
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(t.width, t.height, 1)
                    toTexture:t->surface.color
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
    [e endEncoding];
    [_leases addObject:src];
    [_leases addObject:t];
    _jobs++;
    t->initialized = YES;
    return [self renderTarget:t
                    clearMask:t->bound ? 0 : 6
                        color:MTLClearColorMake(0, 0, 0, 0)
                        depth:1
                      stencil:0
                        error:error];
}
- (BOOL)clear:(PtR2Target *)t color:(MTLClearColor)c depth:(double)d error:(NSError **)error {
    return [self clear:t color:c depth:d stencil:0 error:error];
}
- (BOOL)clear:(PtR2Target *)t color:(MTLClearColor)c depth:(double)d stencil:(uint8_t)stencil error:(NSError **)error {
    return [self clear:t mask:7 color:c depth:d stencil:stencil error:error];
}
- (BOOL)clear:(PtR2Target *)t
           mask:(uint8_t)mask
          color:(MTLClearColor)c
          depth:(double)d
        stencil:(uint8_t)stencil
          error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error])
        return NO;
    if (!mask || (mask & ~7u) || !std::isfinite(d) || d < 0 || d > 1 || !std::isfinite(c.red) ||
        !std::isfinite(c.green) || !std::isfinite(c.blue) || !std::isfinite(c.alpha))
        return fail(error, @"Invalid clear values");
    if (![self admit:0 error:error] || ![self renderTarget:t
                                                 clearMask:mask
                                                     color:c
                                                     depth:d
                                                   stencil:stencil
                                                     error:error])
        return NO;
    _jobs++;
    return YES;
}
- (id<MTLDepthStencilState>)depthState:(PtR2Raster)r error:(NSError **)error {
    if (!r.stencilEnable)
        return _depth[r.depthCompare][r.depthWrite];
    uint64_t key = r.depthCompare | (uint64_t)r.depthWrite << 3 | (uint64_t)r.stencilReadMask << 4 |
                   (uint64_t)r.stencilWriteMask << 12;
    const PtR2StencilFace faces[2] = {r.stencilFront, r.stencilBack};
    unsigned bit = 20;
    for (auto f : faces)
        for (unsigned v : {f.compare, f.fail, f.depthFail, f.pass}) {
            key |= (uint64_t)v << bit;
            bit += 3;
        }
    id<MTLDepthStencilState> state = _stencilStates[@(key)];
    if (state)
        return state;
    if (_stencilStates.count >= 256) {
        fail(error, @"Stencil state cache admission limit reached");
        return nil;
    }
    MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
    d.depthCompareFunction = (MTLCompareFunction)r.depthCompare;
    d.depthWriteEnabled = r.depthWrite;
    for (unsigned face = 0; face < 2; face++) {
        MTLStencilDescriptor *s = [MTLStencilDescriptor new];
        PtR2StencilFace f = faces[face];
        s.stencilCompareFunction = (MTLCompareFunction)f.compare;
        s.stencilFailureOperation = (MTLStencilOperation)f.fail;
        s.depthFailureOperation = (MTLStencilOperation)f.depthFail;
        s.depthStencilPassOperation = (MTLStencilOperation)f.pass;
        s.readMask = r.stencilReadMask;
        s.writeMask = r.stencilWriteMask;
        if (face)
            d.backFaceStencil = s;
        else
            d.frontFaceStencil = s;
    }
    state = [_device newDepthStencilStateWithDescriptor:d];
    if (!state) {
        fail(error, @"Stencil state allocation failed");
        return nil;
    }
    _stencilStates[@(key)] = state;
    return state;
}
- (BOOL)draw:(PtR2Program *)p
                   target:(PtR2Target *)t
                 vertices:(const void *)v
                    count:(uint32_t)count
                  indices:(const uint32_t *)index
                    count:(uint32_t)n
          vertexConstants:(const float *)vc
        fragmentConstants:(const float *)fc
                 textures:(NSArray<PtR2Texture *> *)textures
                   linear:(BOOL)linear
                   raster:(PtR2Raster)r
                    error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error])
        return NO;
    if (!p || p->runtime != _runtime || p->epoch != _runtime->epoch ||
        p->route.sampleCount != t->surface.samples.count || !t->initialized || !v || !count || count > (1u << 24) ||
        !index || !n || r.lines > 1 || n % (r.lines ? 2u : 3u) || n > (1u << 24) || r.depthCompare > 7 ||
        r.depthWrite > 1 || r.cull > 2 || r.frontCCW > 1 || !std::isfinite(r.x) || !std::isfinite(r.y) ||
        !std::isfinite(r.width) || !std::isfinite(r.height) || r.width <= 0 || r.height <= 0 ||
        !std::isfinite(r.depthBias) || !std::isfinite(r.depthSlope) || !std::isfinite(r.znear) ||
        !std::isfinite(r.zfar) || r.znear < 0 || r.zfar > 1 || r.znear > r.zfar || !r.scissorWidth ||
        !r.scissorHeight || r.scissorX >= t.width || r.scissorY >= t.height || r.scissorWidth > t.width - r.scissorX ||
        r.scissorHeight > t.height - r.scissorY || r.stencilEnable > 1 || r.alphaTest > 1 || r.alphaCompare > 7 ||
        !std::isfinite(r.alphaReference) || r.alphaReference < 0 || r.alphaReference > 1 || r.samplers > 1 ||
        r.fill > 1)
        return fail(error, @"Unsupported or invalid normalized draw/target/raster");
    for (auto f : {r.stencilFront, r.stencilBack})
        if (f.compare > 7 || f.fail > 7 || f.depthFail > 7 || f.pass > 7)
            return fail(error, @"Invalid stencil operation");
    if (r.samplers)
        for (unsigned j = 0; j < 16; j++)
            if ((r.sampler[j] & 7) > 5 || (r.sampler[j] >> 3 & 7) > 5 || (r.sampler[j] >> 8 & 3) > 2 ||
                (r.sampler[j] >> 10 & 7) > 4 || (r.sampler[j] >> 17 & 7) > 5 || r.sampler[j] >> 20)
                return fail(error, @"Invalid sampler code");
    id<MTLDepthStencilState> ds = [self depthState:r error:error];
    if (!ds)
        return NO;
    for (unsigned j = 0; j < n; j++)
        if (index[j] >= count)
            return fail(error, @"Index outside immutable vertex snapshot");
    for (unsigned j = 0; j < 16; j++)
        if (p->textureMask & (1u << j)) {
            if (j >= textures.count || ![textures[j] isKindOfClass:PtR2Texture.class] ||
                ![self valid:textures[j]->lease error:error])
                return fail(error, @"Missing, stale or foreign texture binding");
        }
    uint64_t bytes =
            (uint64_t)count * p->route.stride + (uint64_t)n * 4 + p->vertexConstantBytes + p->fragmentConstantBytes;
    if (![self admit:bytes error:error])
        return NO;
    for (unsigned j = 0; j < 16; j++)
        if ((p->textureMask & (1u << j)) && ![self current:textures[j] error:error])
            return NO;
    static const float zero[256 * 4] = {0};
    R2Slice vb = [self upload:v bytes:(size_t)count * p->route.stride error:error], ib = [self upload:index
                                                                                                bytes:n * 4
                                                                                                error:error];
    R2Slice vconstants = [self upload:vc ?: zero bytes:p->vertexConstantBytes error:error],
            fconstants = [self upload:fc ?: zero bytes:p->fragmentConstantBytes error:error];
    if (!vb.lease || !ib.lease || !vconstants.lease || !fconstants.lease)
        return NO;
    if (![self renderTarget:t clearMask:0 color:MTLClearColorMake(0, 0, 0, 0) depth:1 stencil:0 error:error])
        return NO;
    BOOL fresh = !_cacheBindings || !_bindingsValid;
    if (fresh || _boundProgram != p) {
        [_encoder setRenderPipelineState:p->pipeline];
        [_leases addObject:p];
        _boundProgram = p;
    }
    if (fresh || _boundDepth != ds) {
        [_encoder setDepthStencilState:ds];
        _boundDepth = ds;
    }
    if (fresh || _boundStencil != r.stencilReference) {
        [_encoder setStencilReferenceValue:r.stencilReference];
        _boundStencil = r.stencilReference;
    }
    if (fresh || _boundCull != r.cull) {
        [_encoder setCullMode:(MTLCullMode)r.cull];
        _boundCull = r.cull;
    }
    if (fresh || _boundWinding != r.frontCCW) {
        [_encoder setFrontFacingWinding:r.frontCCW ? MTLWindingCounterClockwise : MTLWindingClockwise];
        _boundWinding = r.frontCCW;
    }
    /* The depth slope is per texel, and a scaled target has scale / 2 texels to a guest pixel. */
    float slope = t->scale == PT_R2_SCALE_ONE ? r.depthSlope : pt_r2_scale_length(r.depthSlope, t->scale);
    if (fresh || _boundBias[0] != r.depthBias || _boundBias[1] != slope) {
        [_encoder setDepthBias:r.depthBias slopeScale:slope clamp:0];
        _boundBias[0] = r.depthBias;
        _boundBias[1] = slope;
    }
    MTLViewport vp = {r.x, r.y, r.width, r.height, r.znear, r.zfar};
    MTLScissorRect sc = {r.scissorX, r.scissorY, r.scissorWidth, r.scissorHeight};
    if (t->scale != PT_R2_SCALE_ONE) { /* guest pixels to target texels; the scissor's edges move outward */
        vp.originX = pt_r2_scale_length((float)r.x, t->scale);
        vp.originY = pt_r2_scale_length((float)r.y, t->scale);
        vp.width = pt_r2_scale_length((float)r.width, t->scale);
        vp.height = pt_r2_scale_length((float)r.height, t->scale);
        PtR2ScaleBox g = {r.scissorX, r.scissorY, r.scissorX + r.scissorWidth, r.scissorY + r.scissorHeight};
        g = pt_r2_scale_scissor(g, t->scale, (uint32_t)t->surface.color.width, (uint32_t)t->surface.color.height);
        sc = {g.x0, g.y0, g.x1 - g.x0, g.y1 - g.y0};
    }
    if (fresh || memcmp(&_boundViewport, &vp, sizeof vp)) {
        [_encoder setViewport:vp];
        _boundViewport = vp;
    }
    if (fresh || memcmp(&_boundScissor, &sc, sizeof sc)) {
        [_encoder setScissorRect:sc];
        _boundScissor = sc;
    }
    if (fresh || memcmp(_boundBlendColor, r.blendColor, sizeof _boundBlendColor)) {
        [_encoder setBlendColorRed:r.blendColor[0] green:r.blendColor[1] blue:r.blendColor[2] alpha:r.blendColor[3]];
        memcpy(_boundBlendColor, r.blendColor, sizeof _boundBlendColor);
    }
    [_encoder setVertexBuffer:vb.lease->buffer offset:vb.offset atIndex:0];
    [_encoder setVertexBuffer:vconstants.lease->buffer offset:vconstants.offset atIndex:1];
    if (fresh || memcmp(_boundEpilogue, r.vertexEpilogue, sizeof _boundEpilogue)) {
        [_encoder setVertexBytes:r.vertexEpilogue length:sizeof r.vertexEpilogue atIndex:2];
        memcpy(_boundEpilogue, r.vertexEpilogue, sizeof _boundEpilogue);
    }
    [_encoder setFragmentBuffer:fconstants.lease->buffer offset:fconstants.offset atIndex:0];
    struct {
        uint32_t enabled, compare;
        float reference;
    } alpha = {r.alphaTest, r.alphaCompare, r.alphaReference};
    static_assert(sizeof alpha == 12, "Alpha binding snapshot size");
    if (fresh || memcmp(_boundAlpha, &alpha, sizeof alpha)) {
        [_encoder setFragmentBytes:&alpha length:sizeof alpha atIndex:1];
        memcpy(_boundAlpha, &alpha, sizeof alpha);
    }
    if (p->route.fog || p->route.textureFixed) { /* PtFixed in fixedSource() */
        struct {
            float fog[4], fogTerm[4], border[16][4], lod[16];
            uint32_t mode[16];
            float key[16][4];
        } fx;
        static_assert(sizeof fx == 672, "PtFixed layout");
        memcpy(fx.fog, r.fog, 3 * sizeof(float));
        fx.fog[3] = 0;
        fx.fogTerm[0] = r.fog[3];
        fx.fogTerm[1] = r.fog[4];
        fx.fogTerm[2] = fx.fogTerm[3] = 0;
        memcpy(fx.border, r.border, sizeof fx.border);
        memcpy(fx.lod, r.lodBias, sizeof fx.lod);
        memcpy(fx.mode, r.textureMode, sizeof fx.mode);
        memcpy(fx.key, r.chromaKey, sizeof fx.key);
        [_encoder setFragmentBytes:&fx length:sizeof fx atIndex:2];
    }
    if (p->route.clipPlanes)
        [_encoder setVertexBytes:r.clipPlane length:sizeof r.clipPlane atIndex:3];
    if (fresh || _boundFill != r.fill) {
        [_encoder setTriangleFillMode:r.fill ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
        _boundFill = r.fill;
    }
    for (unsigned j = 0; j < 16; j++)
        if (p->textureMask & (1u << j)) {
            id<MTLSamplerState> sampler = _sampler[!!linear];
            if (r.samplers) {
                uint32_t c = r.sampler[j];
                __strong id<MTLSamplerState> &s = _samplerStates[c];
                if (!s) {
                    MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
                    d.magFilter = c & 64 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
                    d.minFilter = c & 128 ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
                    d.sAddressMode = (MTLSamplerAddressMode)(c & 7);
                    d.tAddressMode = (MTLSamplerAddressMode)(c >> 3 & 7);
                    d.rAddressMode = (MTLSamplerAddressMode)(c >> 17 & 7);
                    d.mipFilter = (c >> 8 & 3) == 2   ? MTLSamplerMipFilterLinear
                                  : (c >> 8 & 3) == 1 ? MTLSamplerMipFilterNearest
                                                      : MTLSamplerMipFilterNotMipmapped;
                    d.maxAnisotropy = 1u << (c >> 10 & 7);
                    d.lodMinClamp = (float)(c >> 13 & 15);
                    if (!(c >> 8 & 3))
                        d.lodMaxClamp = d.lodMinClamp; /* no filter between levels: the first level only */
                    s = [_device newSamplerStateWithDescriptor:d];
                    if (!s)
                        return fail(error, @"Sampler allocation failed");
                }
                sampler = s;
            }
            id<MTLTexture> texture = textures[j]->lease->texture;
            if (fresh || _boundTexture[j] != texture) {
                [_encoder setFragmentTexture:texture atIndex:j];
                [_leases addObject:textures[j]];
                _boundTexture[j] = texture;
            }
            if (fresh || _boundSampler[j] != sampler) {
                [_encoder setFragmentSamplerState:sampler atIndex:j];
                _boundSampler[j] = sampler;
            }
        }
    int64_t visibility = -1;
    if (r.query) { /* a fresh slot for this query in this pass, zeroed before the GPU can write it */
        if (_passQuery != r.query) {
            _passSlot = _visibilityNext++ % 8192;
            ((uint64_t *)_visibility.contents)[_passSlot] = 0;
            _passQuery = r.query;
            if (_querySlots.size() >= 4096) {
                _querySlots.clear(); /* queries never asked for */
                _queryScale.clear();
            }
            if (t->scale != PT_R2_SCALE_ONE)
                _queryScale[r.query] = t->scale;
            auto &slots = _querySlots[r.query];
            if (slots.size() < 64)
                slots.push_back(_passSlot);
        }
        visibility = _passSlot;
    }
    if (visibility != _boundVisibility) {
        [_encoder setVisibilityResultMode:visibility < 0 ? MTLVisibilityResultModeDisabled
                                                         : MTLVisibilityResultModeCounting
                                   offset:visibility < 0 ? 0 : (NSUInteger)visibility * 8];
        _boundVisibility = visibility;
    }
    [_encoder drawIndexedPrimitives:r.lines ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle
                         indexCount:n
                          indexType:MTLIndexTypeUInt32
                        indexBuffer:ib.lease->buffer
                  indexBufferOffset:ib.offset];
    _bindingsValid = YES;
    _batchBytes += bytes;
    _jobs++;
    _draws++;
    return YES;
}
- (uint64_t)queryCount:(uint32_t)query {
    auto it = _querySlots.find(query);
    if (it == _querySlots.end())
        return 0;
    uint64_t n = 0;
    const uint64_t *v = (const uint64_t *)_visibility.contents;
    for (uint32_t s : it->second)
        n += v[s];
    _querySlots.erase(it);
    auto scaled = _queryScale.find(query);
    if (scaled != _queryScale.end()) { /* texels of a scaled target as guest pixels, rounded up: seen stays seen */
        unsigned kk = scaled->second * scaled->second;
        n = (n * 4 + kk - 1) / kk;
        _queryScale.erase(scaled);
    }
    return n;
}
- (BOOL)resolve:(PtR2Target *)t control:(uint32_t)c error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error])
        return NO;
    if (!t->initialized || !(c & 1) || (c & ~7u))
        return fail(error, @"Uninitialized target or unsupported resolve control");
    [self endRender:8];
    if (![_resolver encodeSurface:t->surface commandBuffer:_batch control:c error:error])
        return NO;
    [_leases addObject:t];
    return YES;
}
- (PtR2Texture *)snapshot:(PtR2Target *)t resolveControl:(uint32_t)c error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error])
        return nil;
    if (!t->initialized || !(c & 1) || (c & ~7u)) {
        fail(error, @"Uninitialized target or unsupported resolve control");
        return nil;
    }
    if (![self admit:0 error:error])
        return nil;
    /* The frame that is shown is the storage itself, at the target's internal resolution (the presenter fits it to
     * the screen): not the reduced colour. */
    id<MTLTexture> from = t->surface.resolved;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                 width:from.width
                                                                                height:from.height
                                                                             mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead;
    d.hazardTrackingMode = MTLHazardTrackingModeTracked;
    id<MTLTexture> tex = [_device newTextureWithDescriptor:d];
    if (!tex) {
        fail(error, @"Snapshot allocation failed");
        return nil;
    }
    R2Lease *lease = [self charge:tex.allocatedSize error:error];
    if (!lease)
        return nil;
    if (![self resolve:t control:c error:error])
        return nil;
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e) {
        fail(error, @"Snapshot blit encoder failed");
        return nil;
    }
    [e copyFromTexture:from
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(from.width, from.height, 1)
                    toTexture:tex
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
    [e endEncoding];
    lease->texture = tex;
    PtR2Texture *result = [PtR2Texture new];
    result->lease = lease;
    result->status.store(0);
    [_leases addObject:result];
    [_exports addObject:result];
    _jobs++;
    return result;
}
- (PtR2Frame *)frame:(PtR2Texture *)texture error:(NSError **)error {
    if (![self owner:error] || ![self valid:texture ? texture->lease : nil error:error])
        return nil;
    if (texture->status.load(std::memory_order_acquire) != 1) {
        fail(error, @"Frame not successfully completed", 2);
        return nil;
    }
    PtR2Frame *frame = [PtR2Frame new];
    frame->source = texture;
    return frame;
}
- (PtR2Readback *)readback:(PtR2Target *)t resolveControl:(uint32_t)c error:(NSError **)error {
    if (![self owner:error] || ![self valid:t ? t->lease : nil error:error])
        return nil;
    if (!t->initialized || !(c & 1) || (c & ~7u)) {
        fail(error, @"Uninitialized target or unsupported resolve control");
        return nil;
    }
    size_t row = ((size_t)t.width * 4 + 255) & ~size_t(255), bytes = row * t.height;
    if (![self admit:bytes error:error])
        return nil;
    R2Lease *lease = [self buffer:NULL bytes:bytes error:error];
    if (!lease)
        return nil;
    if (![self resolve:t control:c error:error])
        return nil;
    id<MTLTexture> from = [self reduced:t error:error];
    if (!from)
        return nil;
    id<MTLBlitCommandEncoder> e = [_batch blitCommandEncoder];
    if (!e) {
        fail(error, @"Readback blit encoder failed");
        return nil;
    }
    [e copyFromTexture:from
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(t.width, t.height, 1)
                            toBuffer:lease->buffer
                   destinationOffset:0
              destinationBytesPerRow:row
            destinationBytesPerImage:bytes];
    [e endEncoding];
    PtR2Readback *r = [PtR2Readback new];
    r->lease = lease;
    r->rowBytes = row;
    r->width = t.width;
    r->height = t.height;
    r->status.store(0);
    [_readbacks addObject:r];
    [_leases addObject:r];
    _jobs++;
    _batchBytes += bytes;
    return r;
}
- (NSData *)copyReadback:(PtR2Readback *)r error:(NSError **)error {
    if (![self owner:error] || ![self valid:r ? r->lease : nil error:error])
        return nil;
    if (r->depthPlanes) {
        fail(error, @"Depth readback requires an explicit integer format");
        return nil;
    }
    if (r->status.load(std::memory_order_acquire) != 1) {
        fail(error, @"Readback not successfully completed", 2);
        return nil;
    }
    NSMutableData *data = [NSMutableData dataWithLength:(size_t)r.width * r.height * 4];
    for (unsigned y = 0; y < r.height; y++)
        memcpy((uint8_t *)data.mutableBytes + (size_t)y * r.width * 4,
               (const uint8_t *)r->lease->buffer.contents + y * r->rowBytes, (size_t)r.width * 4);
    return data;
}
- (PtR2Ticket)commit:(NSError **)error {
    if (![self owner:error] || !_batch) {
        fail(error, @"No batch to commit");
        return {};
    }
    [self endRender:9];
    auto state = _runtime;
    PtR2Ticket ticket = _ticket;
    uint64_t submitted = nowNS(), submittedAbs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    BOOL prefix = _completionPrefix;
    __block NSArray *leases = [_leases copy];
    __block NSArray<PtR2Readback *> *readbacks = [_readbacks copy];
    __block NSArray<PtR2Texture *> *exports = [_exports copy];
    os_log_t log = _log;
    os_signpost_id_t sid = os_signpost_id_generate(log);
    os_signpost_interval_begin(log, sid, "R2Batch", "epoch=%llu seq=%llu jobs=%llu bytes=%llu", ticket.epoch,
                               ticket.sequence, _jobs, _batchBytes);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->inflight++;
    }
    [_batch addCompletedHandler:^(id<MTLCommandBuffer> done) {
        BOOL ok = done.status == MTLCommandBufferStatusCompleted;
        for (PtR2Readback *r in readbacks)
            r->status.store(ok ? 1 : -1, std::memory_order_release);
        for (PtR2Texture *t in exports)
            t->status.store(ok ? 1 : -1, std::memory_order_release);
        NSUInteger retained = leases.count;
        /* Retire wrappers BEFORE signalling capacity. Do not strand a retry
         * behind leases captured by a completed handler with no future wake. */
        leases = nil;
        readbacks = nil;
        exports = nil;
        uint64_t latency = nowNS() - submitted;
        double seconds = done.GPUEndTime >= done.GPUStartTime ? done.GPUEndTime - done.GPUStartTime : 0;
        PtR2Completion completion = {ticket, ok};
        dispatch_block_t wake = nil;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->inflight--;
            if (ticket.epoch == state->epoch && !state->closed) {
                if (prefix) {
                    /* Handler order across command buffers is not an execution-order guarantee.
                     * Only a contiguous set of retired callbacks becomes guest-visible. */
                    state->retired.emplace(ticket.sequence, completion);
                    for (;;) {
                        auto found = state->retired.find(state->nextCompletion);
                        if (found == state->retired.end())
                            break;
                        PtR2Completion c = found->second;
                        state->retired.erase(found);
                        state->nextCompletion++;
                        if (state->completions.empty())
                            state->completions.push_back(c);
                        else {
                            auto &last = state->completions.back();
                            c.success = c.success && last.success;
                            last = c;
                        }
                    }
                } else
                    state->completions.push_back(completion);
                state->completed++;
                state->gpuSeconds += seconds;
                state->callbackNS += latency;
                state->maxCallbackNS = std::max(state->maxCallbackNS, latency);
                {
                    double gs = done.GPUStartTime * 1e9, ge = done.GPUEndTime * 1e9,
                           cb = (double)clock_gettime_nsec_np(
                                   CLOCK_UPTIME_RAW); /* GPU times: host seconds, mach absolute */
                    if (gs >= (double)submittedAbs && ge >= gs && cb >= ge) {
                        state->startNS += (uint64_t)(gs - (double)submittedAbs);
                        state->endNS += (uint64_t)(cb - ge);
                    }
                }
                if (!ok)
                    state->failed = true;
            }
            wake = reserveWake(*state);
        }
        state->changed.notify_all();
        os_signpost_interval_end(log, sid, "R2Batch", "success=%d retained=%lu", ok, (unsigned long)retained);
        runWake(state, wake);
    }];
    [_batch commit];
    _batch = nil;
    _leases = nil;
    _readbacks = nil;
    _exports = nil;
    _upload = nil;
    _uploadUsed = 0;
    _jobs = _batchBytes = 0;
    _commits++;
    return ticket;
}
- (BOOL)poll:(PtR2Completion *)out {
    if (!out)
        return NO;
    std::lock_guard<std::mutex> lock(_runtime->mutex);
    if (_runtime->completions.empty())
        return NO;
    *out = _runtime->completions.front();
    _runtime->completions.pop_front();
    return YES;
}
- (BOOL)setCompletionWake:(dispatch_block_t)wake error:(NSError **)error {
    if (![self owner:error])
        return NO;
    std::lock_guard<std::mutex> lock(_runtime->mutex);
    _runtime->wake = [wake copy];
    return YES;
}
- (BOOL)reset:(NSError **)error {
    if (![self isOwner])
        return fail(error, @"Reset requires its encoding owner");
    [self endRender:10];
    for (PtR2Readback *r in _readbacks)
        r->status.store(-1);
    for (PtR2Texture *t in _exports)
        t->status.store(-1);
    _batch = nil;
    _leases = nil;
    _readbacks = nil;
    _exports = nil;
    _upload = nil;
    _uploadUsed = 0;
    _jobs = _batchBytes = 0;
    {
        std::lock_guard<std::mutex> lock(_runtime->mutex);
        if (_runtime->closed || _runtime->epoch == UINT64_MAX)
            return fail(error, @"Closed renderer or exhausted reset epoch");
        _runtime->epoch++;
        _runtime->failed = false;
        _runtime->completions.clear();
        _runtime->retired.clear();
        _runtime->nextCompletion = _sequence + 1;
        _runtime->completed = _runtime->callbackNS = _runtime->maxCallbackNS = _runtime->startNS = _runtime->endNS = 0;
        _runtime->gpuSeconds = 0;
    }
    [_programs removeAllObjects];
    [_stencilStates removeAllObjects];
    return YES;
}
- (NSDictionary *)diagnostics {
    if (![self isOwner])
        return @{@"error" : @"encoding owner required"};
    std::lock_guard<std::mutex> lock(_runtime->mutex);
    NSMutableDictionary *reasons = [NSMutableDictionary new];
    if (_passReasons) {
        NSArray *names = @[
            @"texture-unpack", @"target-or-colour-clear", @"depth-binding", @"depth-clear", @"depth-growth",
            @"depth-transfer", @"colour-copy", @"colour-import", @"resolve", @"commit", @"reset", @"close"
        ];
        for (unsigned i = 0; i < 12; i++)
            reasons[names[i]] = @(_passBreak[i]);
    }
    return @{
        @"passBreaks" : reasons,
        @"epoch" : @(_runtime->epoch),
        @"residentBytes" : @(_runtime->resident),
        @"peakBytes" : @(_runtime->peak),
        @"inflightBatches" : @(_runtime->inflight),
        @"completedUnpolled" : @(_runtime->completions.size()),
        @"draws" : @(_draws),
        @"commits" : @(_commits),
        @"renderPasses" : @(_passes),
        @"backpressure" : @(_busy),
        @"programsCompiled" : @(_compiled),
        @"compileNS" : @(_compileNS),
        @"librariesCompiled" : @(_libraryCompiles),
        @"libraryNS" : @(_libraryNS),
        @"libraryMaxNS" : @(_libraryMaxNS),
        @"librariesSlow" : @(_librarySlow),
        @"librariesAsync" : @([self asyncStat:0]),
        @"libraryAsyncNS" : @([self asyncStat:1]),
        @"libraryAsyncMaxNS" : @([self asyncStat:2]),
        @"gpuFailed" : @(_runtime->failed),
        @"bufferAllocations" : @(_bufferAllocations),
        @"snapshotBytes" : @(_snapshotBytes),
        @"epochCompletedBatches" : @(_runtime->completed),
        @"epochGPUSeconds" : @(_runtime->gpuSeconds),
        @"epochMeanSubmitToCallbackNS" : @(_runtime->completed ? _runtime->callbackNS / _runtime->completed : 0),
        @"epochMaxSubmitToCallbackNS" : @(_runtime->maxCallbackNS),
        @"epochMeanSubmitToStartNS" : @(_runtime->completed ? _runtime->startNS / _runtime->completed : 0),
        @"epochMeanEndToCallbackNS" : @(_runtime->completed ? _runtime->endNS / _runtime->completed : 0)
    };
}
- (void)close {
    if (!_runtime)
        return;
    [self endRender:11];
    for (PtR2Readback *r in _readbacks)
        r->status.store(-1);
    for (PtR2Texture *t in _exports)
        t->status.store(-1);
    _batch = nil;
    _leases = nil;
    _readbacks = nil;
    _exports = nil;
    _upload = nil;
    _uploadUsed = 0;
    {
        std::unique_lock<std::mutex> lock(_runtime->mutex);
        _runtime->closed = true;
        _runtime->wake = nil;
        _runtime->completions.clear();
        _runtime->retired.clear();
        _runtime->spare.clear();
        _runtime->changed.wait(lock, [&] { return !_runtime->inflight && !_runtime->wakes; });
    }
    [_programs removeAllObjects];
}
- (void)dealloc {
    [self close];
    if (_ownerQueue)
        dispatch_queue_set_specific(_ownerQueue, (__bridge void *)self, NULL, NULL);
}
@end
