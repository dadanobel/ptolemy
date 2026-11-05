/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Colour formats other than ARGB8888 in the product Metal executor (COLORFORMAT 3 ARGB1555, 4 RGB565, 9 I8, 13 UV88,
 * 15 ARGB4444), and ARGB8888 with the output selects R, G, B, A (US_OUT_FMT_0 0x39xx, an observed value). A 16x16 target
 * filled with a byte pattern is imported, its left half drawn with one colour: after the write-back the left half holds
 * that colour packed in the format (I8 from C2, UV88 from C0 and C1 of the default output selects B, G, R, A) and the
 * right half every original byte (decode and encode round-trip). */
#include "optics-metal.h"
#include "pixel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
enum {
    C0 = 0x100000,
    TEX = 0x200000,
    N = 16
};
static unsigned q(unsigned x, unsigned max) {
    return (x * max + 127) / 255;
}
int main(void) {
    uint32_t fs[] = {8,
                     0,
                     0,
                     0,
                     0,
                     0,
                     0x400000,
                     (1u << 15) | (1u << 6),
                     (7u << 26) | 1u,
                     (1u << 24) | 1u,
                     (21u << 7) | (20u << 14),
                     9u | (17u << 7) | (16u << 14)}; /* as optics-mip-levels */
    triangle(N, fs, 0x313);
    dr.fi.texture_mask = 1;
    dr.scissor[2] = N / 2;
    dr.color_mask = 15;
    dr.texture_mask = 1;
    OpticsTexture *t = &dr.texture[0];
    t->offset = TEX;
    t->width = t->height = t->pitch = 1;
    t->format = 0xc;
    t->layout = OPTICS_PIXEL_RGBA8;
    t->bpp = 4;
    t->levels = 1;
    t->sampler = 0x12;
    for (int c = 0; c < 4; c++)
        t->select[c] = (uint8_t)c;
    static uint8_t ram[1 << 20];
    PtRadeonConfig c = {.ram = {ram, sizeof ram, NULL}};
    PtRadeon *d;
    assert(!pt_radeon_create(&d, &c));
    void *x = m->create(d);
    assert(x);
    uint8_t rgba[4] = {0x80, 0x40, 0xc0, 0xff};
    assert(!pt_radeon_vram(d, TEX, rgba, 4, 1));
    static const uint8_t formats[6] = {3, 4, 9, 13, 15, 6};
    int bad = 0;
    for (unsigned k = 0; k < 6; k++) {
        unsigned f = formats[k], bpp = f == 9 ? 1 : f == 6 ? 4 : 2, v = 0;
        uint8_t want[4];
        if (f == 3)
            v = 1u << 15 | q(0x80, 31) << 10 | q(0x40, 31) << 5 | q(0xc0, 31);
        if (f == 4)
            v = q(0x80, 31) << 11 | q(0x40, 63) << 5 | q(0xc0, 31);
        if (f == 15)
            v = q(0xff, 15) << 12 | q(0x80, 15) << 8 | q(0x40, 15) << 4 | q(0xc0, 15);
        want[0] = (uint8_t)v;
        want[1] = (uint8_t)(v >> 8);
        if (f == 9)
            want[0] = 0x80; /* C2 = R */
        if (f == 13) {
            want[0] = 0xc0;
            want[1] = 0x40;
        } /* C0 = B, C1 = G */
        if (f == 6) {
            want[0] = 0x80;
            want[1] = 0x40;
            want[2] = 0xc0;
            want[3] = 0xff;
        } /* selects 0x39: R, G, B, A */
        static uint8_t before[N * N * 4], after[N * N * 4];
        for (unsigned i = 0; i < sizeof before; i++)
            before[i] = (uint8_t)(i * 37 + k * 11 + 5);
        dr.cb_offset = C0 + 0x10000 * k;
        dr.cb_format = (uint8_t)f;
        dr.cb_bpp = (uint8_t)bpp;
        dr.cb_select = f == 6 ? 0x39 : 0;
        assert(!pt_radeon_vram(d, dr.cb_offset, before, N * N * bpp, 1));
        const char *why = NULL;
        int rc = m->draw(x, d, &dr, &why);
        if (rc != OPTICS_EXEC_DONE) {
            fprintf(stderr, "format %u: draw %d %s\n", f, rc, why ? why : "");
            assert(0);
        }
        finish(x, d, dr.cb_offset, after, N * N * bpp);
        for (unsigned y = 0; y < N; y++)
            for (unsigned i = 0; i < N; i++) {
                const uint8_t *g = after + (y * N + i) * bpp, *w = i < N / 2 ? want : before + (y * N + i) * bpp;
                if (memcmp(g, w, bpp)) {
                    if (bad++ < 8)
                        fprintf(stderr, "format %u pixel %u,%u: %02x%02x.., want %02x%02x.. (%s)\n", f, i, y, g[0],
                                bpp > 1 ? g[1] : 0, w[0], bpp > 1 ? w[1] : 0, i < N / 2 ? "drawn" : "kept");
                    break;
                }
            }
    }
    assert(!bad);
    m->destroy(x, d);
    pt_radeon_close(d);
    puts("PASS: colour formats ARGB1555, RGB565, I8, UV88, ARGB4444, ARGB8888 with selects R,G,B,A: drawn half packed, other half's bytes kept");
    return 0;
}
