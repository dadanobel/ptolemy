/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Directed test of independent depth/stencil storage in the product Metal executor: depth belongs to
 * the ZB buffer, not to the colour target. C0/Z0 -> C1/Z0 -> C0/Z1 -> C0/Z0 -> C0/Z1 with LESS tests and no clear
 * between them; then a CPU write over Z0 drops its GPU copy (depth is not imported: fresh storage starts at 1.0). */
#include "optics-metal.h"
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
enum {
    C0 = 0x100000,
    C1 = 0x200000,
    Z0 = 0x300000,
    Z1 = 0x400000,
    N = 64,
    LESS = 1
};
static void draw(void *x, PtRadeon *d, uint32_t cb, uint32_t zb, float z, float r, float g, float b) {
    dr.cb_offset = cb;
    dr.zb_offset = zb;
    for (int v = 0; v < 3; v++)
        vert[v][0][2] = z;
    dr.us_constant[0][0] = r;
    dr.us_constant[0][1] = g;
    dr.us_constant[0][2] = b;
    dr.us_constant[0][3] = 1;
    const char *why = NULL;
    int rc = m->draw(x, d, &dr, &why);
    if (rc != OPTICS_EXEC_DONE) {
        fprintf(stderr, "draw %#x/%#x: %d %s\n", cb, zb, rc, why ? why : "");
        assert(0);
    }
}
static uint32_t pixel(void *x, PtRadeon *d, uint32_t cb) { /* centre pixel after completion and export, 0xAARRGGBB */
    for (int i = 0; m->fence(x, d) != OPTICS_FENCE_DONE; i++) {
        assert(i < 5000);
        usleep(1000);
    }
    assert(!m->materialize(x, d, cb, cb + N * N * 4, 0));
    uint8_t p[4];
    assert(!pt_radeon_vram(d, cb + (N / 2 * N + N / 2) * 4, p, 4, 0));
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static void depth_triangle(void) { /* a LESS depth test that writes; D24S8 cleared to depth 1.0, stencil 0 */
    uint32_t fs[] = {0,
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
                     9u | (17u << 7) | (16u << 14)}; /* colour = constant 0 */
    triangle(N, fs, 0x5e57);
    dr.color_mask = 15;
    dr.depth_enable = 1;
    dr.depth_write = 1;
    dr.depth_compare = LESS;
    dr.depth_clear_word = 0xffffff00u;
    dr.depth_clear_format = 2;
}
int main(void) {
    depth_triangle();
    static uint8_t ram[1 << 20];
    PtRadeonConfig c = {.ram = {ram, sizeof ram, NULL}};
    PtRadeon *d;
    assert(!pt_radeon_create(&d, &c));
    uint8_t pitch[4] = {N, 0, 0, 0};
    assert(!pt_radeon_mmio(d, 0x4f24, pitch, 4, 1)); /* ZB_DEPTHPITCH */
    void *x = m->create(d);
    assert(x);
    assert(!m->clear(x, d, Z0, 2) && !m->clear(x, d, Z1, 2));
    draw(x, d, C0, Z0, 0.5f, 1, 0, 0);
    assert(pixel(x, d, C0) == 0xffff0000u); /* Z0 = 0.5 */
    draw(x, d, C1, Z0, 0.7f, 0, 1, 0);
    assert(pixel(x, d, C1) == 0); /* Z0 shared with C0: 0.7 fails */
    draw(x, d, C0, Z1, 0.7f, 0, 0, 1);
    assert(pixel(x, d, C0) == 0xff0000ffu); /* Z1 independent (1.0): passes, Z1 = 0.7 */
    draw(x, d, C0, Z0, 0.6f, 1, 1, 1);
    assert(pixel(x, d, C0) == 0xff0000ffu); /* Z0 kept 0.5: 0.6 fails */
    draw(x, d, C0, Z1, 0.65f, 1, 1, 0);
    assert(pixel(x, d, C0) == 0xffffff00u); /* Z1 kept 0.7: 0.65 passes */
    assert(!m->materialize(x, d, Z0, Z0 + 4, 1)); /* CPU write over Z0: its GPU copy is dropped */
    draw(x, d, C1, Z0, 0.9f, 0, 1, 1);
    assert(pixel(x, d, C1) == 0xff00ffffu); /* fresh Z0 storage at 1.0 */
    const char *io = getenv("OPTICS_DEPTH_IO");
    if (!io || atoi(io)) { /* depth I/O on, read as the product reads it (unset: 1) */
        /* A tiled ZB_DEPTHPITCH layout with OPTICS_DEPTH_IO keeps the GPU-only depth path: the draw is not refused. */
        uint8_t tiled[4] = {N, 0, 1, 0};
        assert(!pt_radeon_mmio(d, 0x4f24, tiled, 4, 1));
        draw(x, d, C0, Z0, 0.1f, 1, 0, 1);
        assert(pixel(x, d, C0) == 0xffff00ffu);
        /* A 2-sample drawable with a macro and micro tiled ZB (Leopard's Chess): a CPU read of the ZB area sees sample 0
         * of the multisampled depth in the R300 2-sample layout (frontend_metal.mm msaa2_offset), in both samples. */
        enum {
            C2 = 0x500000,
            Z2 = 0x600000
        };
        uint8_t ms[4] = {N, 0, 3, 0};
        assert(!pt_radeon_mmio(d, 0x4f24, ms, 4, 1));
        dr.aa_config = 1;
        dr.aa_samples = 2;
        dr.aa_tile = 0x17;
        dr.aa_mspos[0] = 0x33393993;
        dr.aa_mspos[1] = 0x03393939;
        draw(x, d, C2, Z2, 0.25f, 1, 0, 0);
        for (int i = 0; m->fence(x, d) != OPTICS_FENCE_DONE; i++) {
            assert(i < 5000);
            usleep(1000);
        }
        assert(!m->materialize(x, d, Z2, Z2 + N * N * 8, 0));
        static const unsigned at[3][2] = {{5, 6}, {37, 9}, {63, 63}};
        uint8_t want[4];
        assert(optics_depth_pack(2, 0.25f, 0, want));
        for (unsigned k = 0; k < 3; k++) {
            unsigned px = at[k][0], py = at[k][1];
            uint8_t q[8];
            uint32_t o = ((py >> 3) * (N / 32) + (px >> 5)) * 2048 + ((px & 1) << 2 | (py & 1) << 3 |
                                                                      (px >> 1 & 1) << 5 | (py >> 1 & 1) << 6 |
                                                                      (py >> 2 & 1) << 7 | (px >> 2 & 7) << 8);
            assert(!pt_radeon_vram(d, Z2 + o, q, 4, 0) && !pt_radeon_vram(d, Z2 + o + 16, q + 4, 4, 0) &&
                   !memcmp(q, want, 4) && !memcmp(q + 4, want, 4));
        }
        dr.aa_config = 0;
        dr.aa_samples = 0;
        /* Reused VRAM (Tiger u195 T2 #326): a colour target still GPU-owned, then a tiled ZB at the same address. A read
         * there exported the stale depth into the owned pages - "busy" - and the device stopped for good. Now the
         * colour owner is exported, the depth dropped, and the next draw works. */
        enum {
            C3 = 0x700000,
            Z4 = 0x800000
        };
        assert(!pt_radeon_mmio(d, 0x4f24, tiled, 4, 1));
        draw(x, d, C3, Z4, 0.2f, 0, 1, 0);
        draw(x, d, C0, C3, 0.3f, 1, 0, 0);
        for (int i = 0; m->fence(x, d) != OPTICS_FENCE_DONE; i++) {
            assert(i < 5000);
            usleep(1000);
        }
        assert(!m->materialize(x, d, C3, C3 + N * N * 4, 0));
        uint8_t g[4];
        assert(!pt_radeon_vram(d, C3 + (N / 2 * N + N / 2) * 4, g, 4, 0));
        assert((g[0] | g[1] << 8 | g[2] << 16 | (uint32_t)g[3] << 24) == 0xff00ff00u); /* the colour owner's pixel */
        draw(x, d, C0, 0x900000, 0.5f, 0, 0, 1);
        assert(pixel(x, d, C0) == 0xff0000ffu);
        /* ARGB16161616 drawn with OUT_FMT C4_16_FP (Leopard's GL_RGBA16F FBO, u195 #8): the export holds halves (it held
         * unorm16, 0xffff - a half NaN - for 1.0), and an import reads halves (a pixel outside the scissor survives the
         * round trip). Channels C0..C3 = B, G, R, A (output selects 0: 0x1b). */
        enum {
            C5 = 0xa00000
        };
        uint16_t blue[4] = {0x3c00, 0, 0, 0x3c00}, h[4];
        for (unsigned i = 0; i < N * N; i++)
            assert(!pt_radeon_vram(d, C5 + i * 8, (uint8_t *)blue, 8, 1));
        dr.cb_format = 10;
        dr.cb_bpp = 8;
        dr.cb_fp16 = 1;
        dr.depth_enable = dr.depth_write = 0;
        dr.scissor[0] = dr.scissor[1] = N / 2;
        draw(x, d, C5, 0xb00000, 0.5f, 1, 0, 0);
        for (int i = 0; m->fence(x, d) != OPTICS_FENCE_DONE; i++) {
            assert(i < 5000);
            usleep(1000);
        }
        assert(!m->materialize(x, d, C5, C5 + N * N * 8, 0));
        assert(!pt_radeon_vram(d, C5 + ((N - 1) * N + N - 1) * 8, (uint8_t *)h, 8, 0));
        assert(h[0] == 0 && h[1] == 0 && h[2] == 0x3c00 && h[3] == 0x3c00); /* red, as halves */
        assert(!pt_radeon_vram(d, C5, (uint8_t *)h, 8, 0));
        assert(!memcmp(h, blue, 8)); /* untouched: blue came back as halves */
        /* OUT_FMT C_2_10_10_10 (Tiger's GL_RGB10_A2 FBO, selects 0x39: C0..C3 = R, G, B, A): R, G, B in 10 bits from
         * bit 0 and A in 31:30 of a little-endian word. Opaque red 0xc00003ff; opaque blue 0xfff00000 survives import
         * and export. */
        enum {
            C8 = 0xd40000
        };
        uint8_t p4[4];
        const uint8_t blue10[4] = {0x00, 0x00, 0xf0, 0xff};
        for (unsigned i = 0; i < N * N; i++)
            assert(!pt_radeon_vram(d, C8 + i * 4, (uint8_t *)blue10, 4, 1));
        dr.cb_format = 6;
        dr.cb_bpp = 4;
        dr.cb_fp16 = 0;
        dr.cb_rgb10a2 = 1;
        dr.cb_select = 0x39;
        draw(x, d, C8, 0xb00000, 0.5f, 1, 0, 0);
        for (int i = 0; m->fence(x, d) != OPTICS_FENCE_DONE; i++) {
            assert(i < 5000);
            usleep(1000);
        }
        assert(!m->materialize(x, d, C8, C8 + N * N * 4, 0));
        assert(!pt_radeon_vram(d, C8 + ((N - 1) * N + N - 1) * 4, p4, 4, 0) && p4[0] == 0xff && p4[1] == 0x03 &&
               p4[2] == 0 && p4[3] == 0xc0);
        assert(!pt_radeon_vram(d, C8, p4, 4, 0) && !memcmp(p4, blue10, 4));
        /* A VRAM target keeps native bytes whatever COLOR_ENDIAN (the R300 swaps at the bus, for guest RAM): an
         * ARGB4444 target drawn with 1 (Leopard's GL_RGBA4 FBO, u207 #3) holds opaque red as 0xff00 little-endian. */
        enum {
            C9 = 0xd80000
        };
        uint8_t q2[2];
        dr.scissor[0] = dr.scissor[1] = 0;
        dr.cb_format = 15;
        dr.cb_bpp = 2;
        dr.cb_rgb10a2 = 0;
        dr.cb_select = 0;
        dr.cb_endian = 1;
        draw(x, d, C9, 0xb00000, 0.5f, 1, 0, 0);
        for (int i = 0; m->fence(x, d) != OPTICS_FENCE_DONE; i++) {
            assert(i < 5000);
            usleep(1000);
        }
        assert(!m->materialize(x, d, C9, C9 + N * N * 2, 0));
        assert(!pt_radeon_vram(d, C9, q2, 2, 0) && q2[0] == 0x00 && q2[1] == 0xff);
        dr.scissor[0] = dr.scissor[1] = 0;
        dr.cb_format = dr.cb_bpp = dr.cb_fp16 = dr.cb_rgb10a2 = dr.cb_select = dr.cb_endian = 0;
    }
    m->destroy(x, d);
    pt_radeon_close(d);
    puts("optics-depth-metal: ok");
    return 0;
}
