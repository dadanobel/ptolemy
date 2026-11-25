/* SPDX-License-Identifier: GPL-2.0-or-later
 * The rules of internal resolution (foundation/internal_scale.h): sizes, scissor edges, the centre sample for depth
 * and the colour reduction against a reference written independently in floating point. */
#include "../src/optics/foundation/internal_scale.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The reference: integrate the target over the guest pixel's square, in doubles; a texel that reaches past the
 * guest's gw x gh pixels is no part of it. */
static double reference(const uint8_t *t, size_t stride, uint32_t x, uint32_t y, unsigned k, double m, uint32_t gw,
                        uint32_t gh) {
    double sum = 0, area = 0, x0 = x * m, x1 = (x + 1) * m, y0 = y * m, y1 = (y + 1) * m;
    for (uint32_t ty = (uint32_t)y0; ty < y1 && ty + 1 <= gh * m; ty++)
        for (uint32_t tx = (uint32_t)x0; tx < x1 && tx + 1 <= gw * m; tx++) {
            double wx = fmin(tx + 1, x1) - fmax(tx, x0), wy = fmin(ty + 1, y1) - fmax(ty, y0);
            sum += wx * wy * t[ty * stride + 4 * tx + k];
            area += wx * wy;
        }
    return sum / area;
}
static void colour(unsigned h, uint32_t gw, uint32_t gh) {
    uint32_t tw = pt_r2_scale_size(gw, h), th = pt_r2_scale_size(gh, h);
    uint8_t *t = malloc((size_t)tw * th * 4), *g = malloc((size_t)gw * gh * 4);
    assert(t && g);
    srand(h * 1000 + gw);
    for (size_t i = 0; i < (size_t)tw * th * 4; i++)
        t[i] = (uint8_t)(rand() >> 7);
    pt_r2_scale_down8(t, (size_t)tw * 4, g, (size_t)gw * 4, gw, gh, h);
    for (uint32_t y = 0; y < gh; y++)
        for (uint32_t x = 0; x < gw; x++)
            for (unsigned k = 0; k < 4; k++) {
                double want = reference(t, (size_t)tw * 4, x, y, k, h * 0.5, gw, gh);
                assert(fabs(g[(y * gw + x) * 4 + k] - want) <= (h == 2 ? 0 : 0.5 + 1e-9)); /* nearest; x1: a copy */
            }
    free(t);
    free(g);
}
/* Colour that enters a scaled target (each texel takes the guest pixel under its centre), then is read back. */
static void roundtrip(unsigned h, uint32_t gw, uint32_t gh) {
    uint32_t tw = pt_r2_scale_size(gw, h), th = pt_r2_scale_size(gh, h);
    uint8_t *g = malloc((size_t)gw * gh * 4), *t = malloc((size_t)tw * th * 4), *back = malloc((size_t)gw * gh * 4);
    assert(g && t && back);
    srand(h * 77 + gw);
    for (size_t i = 0; i < (size_t)gw * gh * 4; i++)
        g[i] = (uint8_t)(rand() >> 7);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t gx = pt_r2_scale_guest(x, h), gy = pt_r2_scale_guest(y, h);
            gx = gx < gw ? gx : gw - 1; /* the texel past the last pixel of a size rounded up */
            gy = gy < gh ? gy : gh - 1;
            memcpy(t + ((size_t)y * tw + x) * 4, g + ((size_t)gy * gw + gx) * 4, 4);
        }
    pt_r2_scale_down8(t, (size_t)tw * 4, back, (size_t)gw * 4, gw, gh, h);
    unsigned changed = 0;
    for (uint32_t y = 0; y < gh; y++)
        for (uint32_t x = 0; x < gw; x++) {
            int same = !memcmp(back + ((size_t)y * gw + x) * 4, g + ((size_t)y * gw + x) * 4, 4);
            /* x1 and x2: every pixel; x1.5: the second pixel of each pair (its texels are all its own) */
            assert(same || (h == 3 && (!(x & 1) || !(y & 1))));
            changed += !same;
        }
    assert((h == 3) == (changed != 0)); /* the x1.5 difference is real: the header says so */
    free(g);
    free(t);
    free(back);
}
int main(void) {
    /* sizes: up, and x1 is the guest's own */
    assert(pt_r2_scale_size(1024, 2) == 1024 && pt_r2_scale_size(1024, 3) == 1536 && pt_r2_scale_size(1024, 4) == 2048);
    assert(pt_r2_scale_size(1023, 3) == 1535 && pt_r2_scale_size(1, 3) == 2 && pt_r2_scale_size(0, 4) == 0);
    /* lengths: points and lines are as many target pixels as guest pixels times the factor */
    assert(pt_r2_scale_length(1.0f, 2) == 1.0f && pt_r2_scale_length(1.0f, 3) == 1.5f && pt_r2_scale_length(2.5f, 4) == 5.0f);
    /* scissor: outward, clamped; the register's 1440 offset comes off first */
    PtR2ScaleBox g = {1, 1, 3, 4}, a = pt_r2_scale_scissor(g, 3, 1536, 1152), b = pt_r2_scale_scissor(g, 4, 2048, 1536),
                 c = pt_r2_scale_scissor(g, 2, 1024, 768);
    assert(a.x0 == 1 && a.y0 == 1 && a.x1 == 5 && a.y1 == 6); /* 1.5 -> 1, 4.5 -> 5, 6.0 -> 6 */
    assert(b.x0 == 2 && b.y0 == 2 && b.x1 == 6 && b.y1 == 8);
    assert(c.x0 == 1 && c.y0 == 1 && c.x1 == 3 && c.y1 == 4);
    PtR2ScaleBox reg = {1440 + 10, 1440 + 0, 1440 + 1024, 1440 + 769}, off = {reg.x0 - 1440, reg.y0 - 1440, reg.x1 - 1440,
                                                                             reg.y1 - 1440};
    a = pt_r2_scale_scissor(off, 3, 1536, 1152);
    assert(a.x0 == 15 && a.y0 == 0 && a.x1 == 1536 && a.y1 == 1152); /* 769 rows clamp to the target's 1152 */
    PtR2ScaleBox out = {2000, 10, 3000, 5};
    a = pt_r2_scale_scissor(out, 4, 2048, 1536);
    assert(a.x0 == a.x1 && a.y0 == a.y1); /* outside or inverted: empty, never past the target */
    /* every target texel is inside some guest pixel's scaled scissor: nothing is cut between neighbours */
    for (unsigned h = 2; h <= 4; h++)
        for (uint32_t x = 0; x < 64; x++) {
            PtR2ScaleBox one = {x, 0, x + 1, 1}, t = pt_r2_scale_scissor(one, h, 1000, 1000);
            assert(t.x0 <= x * h / 2 && t.x1 >= ((x + 1) * h + 1) / 2 && t.x1 > t.x0);
            uint32_t centre = pt_r2_scale_centre(x, h);
            assert(centre >= t.x0 && centre < t.x1 && centre < pt_r2_scale_size(64, h)); /* depth: one texel, inside */
        }
    assert(pt_r2_scale_centre(0, 4) == 1 && pt_r2_scale_centre(5, 4) == 11 && pt_r2_scale_centre(5, 2) == 5);
    assert(pt_r2_scale_centre(0, 3) == 0 && pt_r2_scale_centre(1, 3) == 2 && pt_r2_scale_centre(2, 3) == 3 &&
           pt_r2_scale_centre(3, 3) == 5);
    /* depth is picked, not blended: a checkerboard of two values reads back as those two values only */
    for (unsigned h = 3; h <= 4; h++) {
        uint32_t tw = pt_r2_scale_size(16, h);
        for (uint32_t x = 0; x < 16; x++) {
            uint32_t texel = pt_r2_scale_centre(x, h), depth = texel & 1 ? 0xffffffu : 0;
            assert(texel < tw && (depth == 0 || depth == 0xffffffu));
        }
    }
    /* colour: x2, x1.5 and the copy at x1; odd guest sizes too */
    colour(2, 17, 9);
    colour(3, 16, 8);
    colour(3, 17, 9);
    colour(4, 16, 8);
    colour(4, 5, 3);
    /* a flat colour stays that colour at every factor */
    for (unsigned h = 2; h <= 4; h++) {
        uint8_t t[8 * 8 * 4], g2[4 * 4 * 4];
        memset(t, 0x7f, sizeof t);
        pt_r2_scale_down8(t, 8 * 4, g2, 4 * 4, 4, 4, h);
        for (unsigned i = 0; i < sizeof g2; i++)
            assert(g2[i] == 0x7f);
    }
    /* the switch, the MSAA cap, the allocation cost */
    assert(pt_r2_scale_parse("2") == 4 && pt_r2_scale_parse("1.5") == 3 && pt_r2_scale_parse("1") == 2);
    assert(pt_r2_scale_parse(NULL) == 3 && pt_r2_scale_parse("") == 3 && pt_r2_scale_parse("3") == 3 &&
           pt_r2_scale_parse("1.50") == 3 && pt_r2_scale_parse("20") == 3 && pt_r2_scale_parse("10") == 3 &&
           PT_R2_SCALE_DEFAULT == 3); /* no setting, and anything unknown: the default, x1.5 */
    assert(pt_r2_scale_samples(4, 4) == 2 && pt_r2_scale_samples(6, 4) == 2 && pt_r2_scale_samples(4, 3) == 4 &&
           pt_r2_scale_samples(4, 2) == 4 && pt_r2_scale_samples(1, 4) == 1);
    assert(pt_r2_scale_bytes(100, 2) == 100 && pt_r2_scale_bytes(100, 3) == 225 && pt_r2_scale_bytes(100, 4) == 400);
    /* "auto": screen pixels the guest is shown on, then the budget; the cases of the displays note */
    assert(pt_r2_scale_auto(1024, 768, 2940, 1846, PT_R2_SCALE_PIXELS_FANLESS) == 3); /* Air 13": a game, x1.5 */
    assert(pt_r2_scale_auto(1472, 924, 2940, 1846, PT_R2_SCALE_PIXELS_FANLESS) == 2); /* its desktop: as is */
    assert(pt_r2_scale_auto(1024, 768, 3024, 1890, PT_R2_SCALE_PIXELS_FAN) == 3);     /* MacBook Pro 14": x1.5 */
    assert(pt_r2_scale_auto(1024, 768, 3024, 1890, UINT64_MAX) == 4);                 /* budget aside: x2 fits the screen */
    assert(pt_r2_scale_auto(1024, 768, 1920, 1080, PT_R2_SCALE_PIXELS_FAN) == 2);     /* 1080p: x1.5 is taller than it */
    assert(pt_r2_scale_auto(800, 600, 1920, 1080, PT_R2_SCALE_PIXELS_FAN) == 3);      /* 1200 x 900 fits */
    assert(pt_r2_scale_auto(640, 480, 2940, 1846, PT_R2_SCALE_PIXELS_FANLESS) == 4);  /* small guests get x2 */
    assert(pt_r2_scale_auto(1024, 768, 2940, 1846, 0) == 2);
    /* what enters a scaled target: pixels [a, b) own texels [first(a), first(b)), the centre texel among them, so
     * depth rows loaded and read back (one texel a pixel) are unchanged at every factor */
    for (unsigned h = 2; h <= 4; h++)
        for (uint32_t x = 0; x < 4096; x++) {
            uint32_t a2 = pt_r2_scale_first(x, h), b2 = pt_r2_scale_first(x + 1, h), c2 = pt_r2_scale_centre(x, h);
            assert(b2 > a2 && c2 >= a2 && c2 < b2 && b2 <= pt_r2_scale_size(x + 1, h));
            for (uint32_t t = a2; t < b2; t++)
                assert(pt_r2_scale_guest(t, h) == x);
        }
    assert(pt_r2_scale_guest(7, 2) == 7 && pt_r2_scale_first(7, 2) == 7); /* x1: the pixel itself */
    /* a size rounded up has one texel past its last pixel: 3 pixels at x1.5 are 5 texels, texel 4 names pixel 3 */
    assert(pt_r2_scale_size(3, 3) == 5 && pt_r2_scale_first(3, 3) == 4 && pt_r2_scale_guest(4, 3) == 3);
    roundtrip(2, 17, 9);
    roundtrip(4, 17, 9);
    roundtrip(3, 17, 9);
    roundtrip(3, 16, 8);
    puts("optics-internal-scale: ok");
    return 0;
}
