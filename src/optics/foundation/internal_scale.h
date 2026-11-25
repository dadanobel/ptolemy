/* SPDX-License-Identifier: GPL-2.0-or-later
 * Internal resolution of 3D targets. A colour target with a depth buffer is drawn larger than the guest asked for:
 * the factor is h / 2 with h = 2 (as is), 3 (x1.5) or 4 (x2). The guest keeps seeing its own size: what it reads
 * back is reduced here. These are the rules every place that touches such a target follows; the renderer applies
 * them, tests/optics-internal-scale.c pins them. */
#ifndef PT_R2_INTERNAL_SCALE_H
#define PT_R2_INTERNAL_SCALE_H
#include <stddef.h>
#include <stdint.h>

enum { PT_R2_SCALE_ONE = 2, PT_R2_SCALE_MAX = 4 };

/* A size of the target: rounded up, so the last guest pixel is covered whole. */
static inline uint32_t pt_r2_scale_size(uint32_t guest, unsigned h) {
    return (guest * h + 1) / 2;
}
/* A continuous quantity in guest pixels (viewport origin and size, point size, line width) in target pixels. */
static inline float pt_r2_scale_length(float guest, unsigned h) {
    return guest * (float)h * 0.5f;
}
/* The guest's scissor [x0, x1) x [y0, y1) on a target of tw x th: edges move outward, so no fragment the guest
 * would draw is cut. The caller removes the register's 1440 offset before, not after. */
typedef struct {
    uint32_t x0, y0, x1, y1;
} PtR2ScaleBox;
static inline PtR2ScaleBox pt_r2_scale_scissor(PtR2ScaleBox g, unsigned h, uint32_t tw, uint32_t th) {
    PtR2ScaleBox t = {g.x0 * h / 2, g.y0 * h / 2, (g.x1 * h + 1) / 2, (g.y1 * h + 1) / 2};
    t.x1 = t.x1 > tw ? tw : t.x1;
    t.y1 = t.y1 > th ? th : t.y1;
    t.x0 = t.x0 > t.x1 ? t.x1 : t.x0;
    t.y0 = t.y0 > t.y1 ? t.y1 : t.y0;
    return t;
}
/* Depth and stencil are not averaged: the guest pixel reads the one target texel under its centre. */
static inline uint32_t pt_r2_scale_centre(uint32_t guest, unsigned h) {
    return (2 * guest + 1) * h / 4;
}
/* What enters a scaled target (the colour it had before it was scaled, depth rows from the guest): each target texel
 * takes the guest pixel under its centre. Guest pixels [a, b) own the texels [first(a), first(b)), and the centre
 * texel of a pixel is one of its own, so depth that went in reads back unchanged at every factor and colour at x2
 * (2 x 2 equal texels). At x1.5 the texel between two guest pixels takes the second one: colour enlarged and then
 * reduced differs from what went in (the first pixel of each pair gets a third of the second). The renderer keeps
 * the guest-size colour until the target is drawn into, so only then does it show.
 * A target's last texel, when its size was rounded up, is past the last pixel: the caller clamps. */
static inline uint32_t pt_r2_scale_guest(uint32_t texel, unsigned h) {
    return (2 * texel + 1) / h;
}
/* The first target texel of guest pixel `guest`, the inverse of pt_r2_scale_guest. */
static inline uint32_t pt_r2_scale_first(uint32_t guest, unsigned h) {
    return guest * h / 2;
}
/* Colour read back by the guest: each guest pixel is the area average of the target texels it covers (2 x 2 at x2;
 * at x1.5 weights 2:1 or 1:2 along each axis), four 8-bit channels, rounded to nearest. At h = 2 it is a copy.
 * The target is at least pt_r2_scale_size() of the guest size in each direction. A texel that reaches past the
 * guest's last pixel (the last one of an odd size at x1.5) is left out: its centre is on the viewport's edge, so
 * no draw fills it reliably; the last pixel is then its one whole texel. */
static inline void pt_r2_scale_down8(const uint8_t *target, size_t target_stride, uint8_t *guest, size_t guest_stride,
                                     uint32_t gw, uint32_t gh, unsigned h) {
    for (uint32_t y = 0; y < gh; y++)
        for (uint32_t x = 0; x < gw; x++) {
            uint32_t sum[4] = {0, 0, 0, 0}, area = 0;
            /* in half texels the pixel covers [x * h, x * h + h); texel t covers [2t, 2t + 2) */
            for (uint32_t ty = y * h / 2; 2 * ty < y * h + h && 2 * ty + 2 <= gh * h; ty++) {
                uint32_t wy = (2 * ty + 2 < y * h + h ? 2 * ty + 2 : y * h + h) - (2 * ty > y * h ? 2 * ty : y * h);
                for (uint32_t tx = x * h / 2; 2 * tx < x * h + h && 2 * tx + 2 <= gw * h; tx++) {
                    uint32_t wx = (2 * tx + 2 < x * h + h ? 2 * tx + 2 : x * h + h) - (2 * tx > x * h ? 2 * tx : x * h);
                    const uint8_t *p = target + ty * target_stride + 4 * (size_t)tx;
                    area += wx * wy;
                    for (unsigned k = 0; k < 4; k++)
                        sum[k] += wx * wy * p[k];
                }
            }
            for (unsigned k = 0; k < 4; k++)
                guest[y * guest_stride + 4 * (size_t)x + k] = (uint8_t)((sum[k] + area / 2) / area);
        }
}
/* The switch's text - OPTICS_INTERNAL, which the product sets from the machine's settings: "1" as the guest asks,
 * "2", and x1.5 for "1.5", for no text and for any other: the default (owner, 23.11). */
enum { PT_R2_SCALE_DEFAULT = 3 };
static inline unsigned pt_r2_scale_parse(const char *s) {
    if (s && s[0] == '1' && !s[1])
        return PT_R2_SCALE_ONE;
    if (s && s[0] == '2' && !s[1])
        return 4;
    return PT_R2_SCALE_DEFAULT;
}
/* Guest MSAA on a scaled target: from x2 the larger target already smooths edges, so at most this many samples.
 * One constant, pending the owner's decision. */
enum { PT_R2_SCALE_MSAA_CAP = 2 };
static inline unsigned pt_r2_scale_samples(unsigned samples, unsigned h) {
    return h >= 4 && samples > PT_R2_SCALE_MSAA_CAP ? (unsigned)PT_R2_SCALE_MSAA_CAP : samples;
}
/* What a scaled target costs against the allocation limit: the guest-size bytes times the factor squared. */
static inline uint64_t pt_r2_scale_bytes(uint64_t guest_bytes, unsigned h) {
    return guest_bytes * h * h / 4;
}
/* "Auto": the largest factor whose target is no larger than the screen area the guest is shown on (more pixels
 * than the screen has are wasted) and no more than max_pixels a frame (the GPU budget; 0 keeps the guest's size).
 * The two budgets are provisional: the displays note's model (3.25 ms for 1024 x 768, 8 ms without a fan, 12 ms
 * with one) until the GPU time at x1 and x2 is measured. */
enum { PT_R2_SCALE_PIXELS_FANLESS = 1940000, PT_R2_SCALE_PIXELS_FAN = 2900000 };
static inline unsigned pt_r2_scale_auto(uint32_t gw, uint32_t gh, uint32_t screen_w, uint32_t screen_h,
                                        uint64_t max_pixels) {
    for (unsigned h = PT_R2_SCALE_MAX; h > PT_R2_SCALE_ONE; h--)
        if ((uint64_t)gw * h <= 2 * (uint64_t)screen_w && (uint64_t)gh * h <= 2 * (uint64_t)screen_h &&
            (uint64_t)pt_r2_scale_size(gw, h) * pt_r2_scale_size(gh, h) <= max_pixels)
            return h;
    return PT_R2_SCALE_ONE;
}
#endif
