/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "msaa.h"
#include <stddef.h>

PtR2MsaaPlan pt_r2_msaa_plan(PtR2MsaaInput in)
{
    static const uint8_t counts[4] = {2, 3, 4, 6};
    PtR2MsaaPlan p = {0};
    p.count = in.aa_config & 1 ? counts[(in.aa_config >> 1) & 3] : 1;
    p.denominator = in.tile_config & (1u << 16) ? 16 : 12;
    /* Resolve policy is kept separate from sample placement. A hardware
     * average is not equivalent when gamma or sample-zero alpha is requested. */
    p.gamma22 = !!(in.resolve_ctl & 2);
    p.average_alpha = !!(in.resolve_ctl & 4);
    if (!(in.metal_sample_counts & (1u << p.count))) p.issues |= PT_R2_MSAA_COUNT;
    if (p.count == 1) {
        p.position[0] = (PtR2Sample){p.denominator / 2, p.denominator / 2, 8, 8};
        return p;
    }
    if (!in.programmable_positions) p.issues |= PT_R2_MSAA_POSITION_API;
    for (unsigned i = 0; i < p.count; i++) {
        unsigned packed = (in.mspos[i / 3] >> ((i % 3) * 8)) & 0xff;
        PtR2Sample *s = &p.position[i];
        s->x = packed & 15;
        s->y = packed >> 4;
        if (s->x >= p.denominator || s->y >= p.denominator) {
            p.issues |= PT_R2_MSAA_POSITION_RANGE;
        } else if ((s->x * 16) % p.denominator || (s->y * 16) % p.denominator) {
            p.issues |= PT_R2_MSAA_POSITION_GRID;
        } else {
            s->metal_x = s->x * 16 / p.denominator;
            s->metal_y = s->y * 16 / p.denominator;
        }
    }
    return p;
}

int pt_r2_msaa_payload_bytes(uint32_t w, uint32_t h, uint32_t color,
                           uint32_t depth, const PtR2MsaaPlan *p,
                           int resolved_color, uint64_t *bytes)
{
    if (!p || !bytes || !w || !h || !color ||
        (p->count != 1 && p->count != 2 && p->count != 3 &&
         p->count != 4 && p->count != 6)) return 0;
    uint64_t bpp = ((uint64_t)color + depth) * p->count;
    if (resolved_color) bpp += color;
    uint64_t pixels = (uint64_t)w * h;
    if (pixels > UINT64_MAX / bpp) return 0;
    *bytes = pixels * bpp;
    return 1;
}
