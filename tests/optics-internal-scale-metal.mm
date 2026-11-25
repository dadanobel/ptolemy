/* SPDX-License-Identifier: GPL-2.0-or-later
 * Internal resolution in the Metal renderer: OPTICS_INTERNAL = argv[1] ("1", "1.5" or "2"). The renderer's results are
 * compared with a model kept here at the target's resolution by the rules of foundation/internal_scale.h: what the guest
 * reads back is that model reduced (pt_r2_scale_down8), depth is the texel under each pixel's centre. With "1" the model
 * is the guest's own pixels, so the same checks pin the unscaled path. The frame that is shown is the model itself. Of a multisampled target: its sample count (the
 * cap at x2) and that it reads back at the guest's size. */
#include "renderer.h"
#include "foundation/internal_scale.h"
extern "C" {
#include "draw.h"
}
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>
typedef std::vector<uint8_t> Bytes;
enum { W = 17, H = 9 }; /* odd: at x1.5 the target's size is rounded up */
static PtR2Renderer *r;
static PtR2Program *program;
static void finish() {
    NSError *e = nil;
    PtR2Ticket t = [r commit:&e];
    assert(t.sequence);
    for (unsigned n = 0; n < 5000; n++) {
        PtR2Completion c;
        while ([r poll:&c]) {
            assert(c.success);
            if (c.ticket.sequence == t.sequence)
                return;
        }
        usleep(1000);
    }
    assert(!"the GPU batch did not complete");
}
static Bytes pixels(PtR2Target *t) {
    NSError *e = nil;
    PtR2Readback *rb = [r readback:t resolveControl:1 error:&e];
    assert(rb && rb.width == t.width && rb.height == t.height);
    finish();
    NSData *data = [r copyReadback:rb error:&e];
    assert(data && data.length == (size_t)t.width * t.height * 4);
    return Bytes((const uint8_t *)data.bytes, (const uint8_t *)data.bytes + data.length);
}
static PtR2Target *target(const Bytes *rgba) { /* a new target, imported from rgba or cleared to opaque black */
    NSError *e = nil;
    PtR2Target *t = [r targetWidth:W height:H samples:(PtR2MsaaInput) {} error:&e];
    assert(t && t.width == W && t.height == H && t.scale == PT_R2_SCALE_ONE);
    if (rgba) {
        OpticsPixelLayout l = {OPTICS_PIXEL_RGBA8, {0, 1, 2, 3}};
        PtR2Texture *x = [r textureWidth:W height:H bytes:rgba->data() rowBytes:W * 4 layout:l error:&e];
        assert(x && [r load:t from:x error:&e]);
    } else
        assert([r clear:t color:MTLClearColorMake(0, 0, 0, 1) depth:1 error:&e]);
    return t;
}
/* One triangle over the whole target in one colour, cut by the scissor; depth 0.5 where depth is written. */
static void draw(PtR2Target *t, const float *rgba, PtR2ScaleBox box, bool depth) {
    PtR2Raster raster = {};
    raster.width = W;
    raster.height = H;
    raster.zfar = 1;
    raster.scissorX = box.x0;
    raster.scissorY = box.y0;
    raster.scissorWidth = box.x1 - box.x0;
    raster.scissorHeight = box.y1 - box.y0;
    raster.depthCompare = 7;
    raster.depthWrite = depth;
    const float v[3][8] = {{-1, -1, 0, 1, 1, 1, 1, 1}, {3, -1, 0, 1, 1, 1, 1, 1}, {-1, 3, 0, 1, 1, 1, 1, 1}};
    const uint32_t ix[3] = {0, 1, 2};
    static float vc[1024], fc[1024];
    memcpy(fc, rgba, 4 * sizeof(float));
    NSError *e = nil;
    BOOL ok = [r draw:program
                       target:t
                     vertices:v
                        count:3
                      indices:ix
                        count:3
              vertexConstants:vc
            fragmentConstants:fc
                     textures:@[]
                       linear:NO
                       raster:raster
                        error:&e];
    if (!ok)
        fprintf(stderr, "draw: %s\n", e.localizedDescription.UTF8String);
    assert(ok);
}
/* The target as the rules say it is, at its own resolution. */
struct Model {
    unsigned k;
    uint32_t tw, th;
    Bytes t;
    explicit Model(unsigned scale)
        : k(scale), tw(pt_r2_scale_size(W, scale)), th(pt_r2_scale_size(H, scale)), t((size_t)tw * th * 4) {
    }
    void fill(const uint8_t *c, PtR2ScaleBox guest) { /* a draw: the scissor's edges outward */
        PtR2ScaleBox b = pt_r2_scale_scissor(guest, k, tw, th);
        for (uint32_t y = b.y0; y < b.y1; y++)
            for (uint32_t x = b.x0; x < b.x1; x++)
                memcpy(&t[((size_t)y * tw + x) * 4], c, 4);
    }
    /* guest pixels (w x h at sx, sy of a W-wide image) enter at dx, dy: each texel takes the pixel under its centre */
    void put(const Bytes &g, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy) {
        uint32_t x0 = pt_r2_scale_first(dx, k), x1 = dx + w == W ? tw : pt_r2_scale_first(dx + w, k);
        uint32_t y0 = pt_r2_scale_first(dy, k), y1 = dy + h == H ? th : pt_r2_scale_first(dy + h, k);
        for (uint32_t y = y0; y < y1; y++)
            for (uint32_t x = x0; x < x1; x++) {
                uint32_t gx = std::min(pt_r2_scale_guest(x, k), dx + w - 1) - dx + sx,
                         gy = std::min(pt_r2_scale_guest(y, k), dy + h - 1) - dy + sy;
                memcpy(&t[((size_t)y * tw + x) * 4], &g[((size_t)gy * W + gx) * 4], 4);
            }
    }
    /* a rectangle of another target (guest pixels): its texels as they are where the two rectangles' texels line up,
     * else its guest-size colour enlarged */
    void copy(const Model &s, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy) {
        if (k != PT_R2_SCALE_MAX && (((sx ^ dx) | (sy ^ dy)) & 1)) {
            put(s.guest(), sx, sy, w, h, dx, dy);
            return;
        }
        uint32_t x0 = pt_r2_scale_first(sx, k), y0 = pt_r2_scale_first(sy, k), nx = pt_r2_scale_first(sx + w, k) - x0,
                 ny = pt_r2_scale_first(sy + h, k) - y0;
        for (uint32_t y = 0; y < ny; y++)
            memcpy(&t[((size_t)(pt_r2_scale_first(dy, k) + y) * tw + pt_r2_scale_first(dx, k)) * 4],
                   &s.t[((size_t)(y0 + y) * tw + x0) * 4], (size_t)nx * 4);
    }
    Bytes guest() const {
        Bytes g((size_t)W * H * 4);
        pt_r2_scale_down8(t.data(), (size_t)tw * 4, g.data(), (size_t)W * 4, W, H, k);
        return g;
    }
};
static unsigned distance(const Bytes &a, const Bytes &b) {
    assert(a.size() == b.size());
    unsigned m = 0;
    for (size_t i = 0; i < a.size(); i++)
        m = std::max(m, (unsigned)abs((int)a[i] - (int)b[i]));
    return m;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        const char *arg = argc > 1 ? argv[1] : "1";
        setenv("OPTICS_INTERNAL", arg, 1);
        unsigned k = pt_r2_scale_parse(arg), worst = 0;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        NSError *e = nil;
        r = [[PtR2Renderer alloc] initWithDevice:device limits:pt_r2_default_limits(device) error:&e];
        assert(r);
        static PtR2ShaderIR pvs, us; /* the pass-through vertex program and a fragment program: constant 0 as the colour */
        PtR2ShaderError er = {};
        const uint32_t fs[12] = {0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0x400000,
                                 0,
                                 (7u << 26) | 32u,
                                 (1u << 24) | 32u,
                                 (21u << 7) | (20u << 14),
                                 9u | (17u << 7) | (16u << 14)};
        assert(optics_draw_passthrough(2, &pvs) &&
               pt_r2_shader_decode(PT_R2_SHADER_US, PT_R2_SHADER_FP32_FTZ0, fs, 12, 0, &us, &er) == PT_R2_SHADER_OK);
        PtR2ShaderInterface vi = {}, fi = {};
        vi.abi = fi.abi = PT_R2_SHADER_ABI;
        vi.input_count = vi.output_count = 2;
        vi.constant_count = 1;
        fi.input_count = 1;
        fi.output_count = 4;
        fi.constant_count = 32;
        PtR2Route route = {};
        route.colorMask = 15;
        route.varyingCount = 1;
        route.pvsOutput[0] = 1;
        route.sampleCount = 1;
        route.stride = 32;
        route.attribute[0] = (PtR2Attribute){0, 4, 0};
        route.attribute[1] = (PtR2Attribute){16, 4, 0};
        program = [r preparePVS:&pvs vertexInterface:&vi fragment:&us interface:&fi route:route error:&e];
        assert(program);
        const PtR2ScaleBox all = {0, 0, W, H}, box = {3, 2, 8, 6};
        const float green[4] = {0, 1, 0, 1}, red[4] = {1, 0, 0, 1};
        const uint8_t green8[4] = {0, 255, 0, 255}, red8[4] = {255, 0, 0, 255}, black8[4] = {0, 0, 0, 255};
        Bytes noise((size_t)W * H * 4);
        srand(k);
        for (auto &b : noise)
            b = (uint8_t)(rand() >> 7);

        /* 1. A guest surface is imported as it is; the first depth storage bound makes it a scaled target, of the
         *    guest's size to the caller. Until something is drawn the guest reads back its own bytes; after a draw, the
         *    enlarged import reduced (the import itself at x2 and x1: tests/optics-internal-scale.c) with the draw's pixel. */
        PtR2Target *t = target(&noise);
        PtR2Depth *z = [r depthWidth:W height:H error:&e];
        assert(z && z.width == W && z.height == H && [r target:t depth:z error:&e]);
        assert(t.scale == k && t.width == W && t.height == H);
        Bytes got = pixels(t);
        assert(got == noise);
        const PtR2ScaleBox corner = {0, 0, 1, 1};
        Model m(k);
        m.put(noise, 0, 0, W, H, 0, 0);
        draw(t, green, corner, false);
        m.fill(green8, corner);
        got = pixels(t);
        worst = std::max(worst, distance(got, m.guest()));
        assert(distance(got, m.guest()) <= 1);

        /* 2. A draw under a scissor, with depth: the colour is the model's (no seam at the scissor's edge, nothing
         *    outside it), and depth changed in the same scissor: each pixel reads the texel under its centre (no
         *    half-pixel shift between the two). */
        assert([r clear:t color:MTLClearColorMake(0, 0, 0, 1) depth:1 error:&e]);
        m.fill(black8, all);
        draw(t, green, box, true);
        m.fill(green8, box);
        got = pixels(t);
        worst = std::max(worst, distance(got, m.guest()));
        assert(distance(got, m.guest()) <= 1);
        for (uint32_t y = box.y0; y < box.y1; y++)
            for (uint32_t x = box.x0; x < box.x1; x++)
                assert(!memcmp(&got[((size_t)y * W + x) * 4], green8, 4));
        {
            PtR2Readback *rb = [r readbackDepth:z y:0 height:H error:&e];
            assert(rb);
            finish();
            NSData *d24 = [r copyDepthReadback:rb format:2 error:&e];
            assert(d24 && d24.length == (size_t)W * H * 4);
            PtR2ScaleBox b = pt_r2_scale_scissor(box, k, m.tw, m.th);
            for (uint32_t y = 0; y < H; y++)
                for (uint32_t x = 0; x < W; x++) {
                    uint32_t cx = pt_r2_scale_centre(x, k), cy = pt_r2_scale_centre(y, k);
                    bool hit = cx >= b.x0 && cx < b.x1 && cy >= b.y0 && cy < b.y1;
                    float v;
                    uint8_t s;
                    assert(optics_depth_unpack(2, (const uint8_t *)d24.bytes + ((size_t)y * W + x) * 4, &v, &s));
                    assert(fabsf(v - (hit ? 0.5f : 1.0f)) < 1e-6f && s == 0);
                }
        }

        /* 3. Depth rows from the guest and back: unchanged at every factor, for a range of rows and after a partial
         *    reload (one texel per pixel, never a blend of neighbours). */
        {
            PtR2Depth *z2 = [r depthWidth:W height:H error:&e];
            assert(z2);
            Bytes a((size_t)W * H * 4), b2((size_t)W * 3 * 4);
            for (auto &x : a)
                x = (uint8_t)(rand() >> 7);
            for (auto &x : b2)
                x = (uint8_t)(rand() >> 7);
            assert([r loadDepth:z2 y:0 height:H bytes:a.data() format:2 error:&e]);
            PtR2Readback *part = [r readbackDepth:z2 y:2 height:5 error:&e];
            assert(part && part.width == W && part.height == 5);
            finish();
            NSData *rows = [r copyDepthReadback:part format:2 error:&e];
            assert(rows && rows.length == (size_t)W * 5 * 4 && !memcmp(rows.bytes, &a[(size_t)2 * W * 4], rows.length));
            assert([r loadDepth:z2 y:3 height:3 bytes:b2.data() format:2 error:&e]);
            memcpy(&a[(size_t)3 * W * 4], b2.data(), b2.size());
            PtR2Readback *whole = [r readbackDepth:z2 y:0 height:H error:&e];
            assert(whole);
            finish();
            rows = [r copyDepthReadback:whole format:2 error:&e];
            assert(rows && rows.length == a.size() && !memcmp(rows.bytes, a.data(), a.size()));
        }

        /* 4. The target read as a texture is its colour at the guest's size - what the guest would read back - and a view
         *    taken before a draw shows the target after it. */
        const uint8_t rgba[4] = {0, 1, 2, 3};
        PtR2Texture *view = [r view:t component:rgba error:&e];
        assert(view);
        {
            PtR2Target *copy = [r targetWidth:W height:H samples:(PtR2MsaaInput) {} error:&e];
            assert(copy && [r load:copy from:view error:&e]);
            Bytes seen = pixels(copy);
            assert(copy.scale == PT_R2_SCALE_ONE && seen == pixels(t));
            draw(t, red, all, false);
            m.fill(red8, all);
            PtR2Target *after = [r targetWidth:W height:H samples:(PtR2MsaaInput) {} error:&e];
            assert(after && [r load:after from:view error:&e]);
            seen = pixels(after);
            for (size_t i = 0; i < seen.size(); i += 4)
                assert(!memcmp(&seen[i], red8, 4));
        }

        /* 5. Copies. A rectangle of the scaled target copied into another target makes that one scaled too (a buffer
         *    swap): texel for texel where the two rectangles line up, else through the guest's size. A rectangle from
         *    an unscaled target into the scaled one is enlarged. */
        {
            PtR2Target *dest = target(nullptr), *source = target(&noise);
            Model d(k);
            d.fill(black8, all);
            draw(t, green, box, false);
            m.fill(green8, box);
            assert([r copy:t x:2 y:1 width:8 height:6 to:dest x:4 y:3 error:&e]); /* origins two pixels apart */
            d.copy(m, 2, 1, 8, 6, 4, 3);
            assert(dest.scale == k && dest.width == W && dest.height == H);
            assert([r copy:t x:2 y:1 width:5 height:2 to:dest x:1 y:0 error:&e]); /* one apart: not lined up at x1.5 */
            d.copy(m, 2, 1, 5, 2, 1, 0);
            Bytes seen = pixels(dest);
            worst = std::max(worst, distance(seen, d.guest()));
            assert(distance(seen, d.guest()) <= 1);
            assert([r copy:source x:1 y:0 width:7 height:5 to:t x:10 y:4 error:&e]); /* to the target's last pixels */
            m.put(noise, 1, 0, 7, 5, 10, 4);
            got = pixels(t);
            worst = std::max(worst, distance(got, m.guest()));
            assert(distance(got, m.guest()) <= 1);
        }

        /* 6. A multisampled target is scaled from the start; at x2 it keeps at most 2 of the guest's 4 samples. What the
         *    guest reads back is resolved and reduced to its own size. */
        {
            PtR2MsaaInput aa = {};
            aa.aa_config = 1 | 2 << 1; /* on, 4 samples; positions on the 1/16 grid (in twelfths: 3 and 9) */
            aa.mspos[0] = 0x33 | 0x99 << 8 | 0x93 << 16;
            aa.mspos[1] = 0x39;
            PtR2Target *ms = [r targetWidth:W height:H samples:aa error:&e];
            assert(ms && ms.width == W && ms.height == H && ms.scale == k && ms.samples == (k == 4 ? 2u : 4u));
            assert([r clear:ms color:MTLClearColorMake(0, 0, 0, 1) depth:1 error:&e]);
            Bytes seen = pixels(ms);
            for (size_t i = 0; i < seen.size(); i += 4)
                assert(!memcmp(&seen[i], black8, 4));
        }
        /* 7. The frame that is shown: a snapshot is the target at its internal resolution - texel for texel the model,
         *    so every edge is where the guest's is times the factor - not the reduced colour. Texels past the guest's
         *    last pixel (an odd size at x1.5) are no part of the picture. */
        {
            PtR2Texture *snap = [r snapshot:t resolveControl:1 error:&e];
            assert(snap);
            finish();
            PtR2Frame *frame = [r frame:snap error:&e];
            assert(frame);
            id<MTLTexture> shown = frame.texture;
            assert(shown.width == m.tw && shown.height == m.th);
            size_t row = ((size_t)m.tw * 4 + 255) & ~(size_t)255;
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLBuffer> out = [device newBufferWithLength:row * m.th options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            assert(queue && out && cb && blit);
            [blit copyFromTexture:shown
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(m.tw, m.th, 1)
                                toBuffer:out
                       destinationOffset:0
                  destinationBytesPerRow:row
                destinationBytesPerImage:row * m.th];
            [blit endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            assert(cb.status == MTLCommandBufferStatusCompleted);
            const uint8_t *px = (const uint8_t *)out.contents;
            unsigned edge = 0;
            for (uint32_t y = 0; 2 * y + 2 <= H * k; y++)
                for (uint32_t x = 0; 2 * x + 2 <= W * k; x++)
                    for (unsigned c = 0; c < 4; c++)
                        edge = std::max(edge, (unsigned)abs((int)px[y * row + 4 * x + c] -
                                                            (int)m.t[((size_t)y * m.tw + x) * 4 + c]));
            worst = std::max(worst, edge);
            assert(edge <= 1);
        }
        [r close];
        r = nil;
        printf("optics-internal-scale-metal x%g: ok (largest difference from the model %u)\n", k * 0.5, worst);
    }
    return 0;
}
