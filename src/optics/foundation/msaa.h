/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_MSAA_H
#define PT_R2_MSAA_H
#include <stdint.h>

enum {
    PT_R2_MSAA_COUNT = 1u << 0,
    PT_R2_MSAA_POSITION_RANGE = 1u << 1,
    PT_R2_MSAA_POSITION_GRID = 1u << 2,
    PT_R2_MSAA_POSITION_API = 1u << 3
};
typedef struct {
    uint32_t aa_config, tile_config, mspos[2], resolve_ctl;
    /* Query supportsTextureSampleCount once per device: bit N means N samples. */
    uint32_t metal_sample_counts;
    int programmable_positions;
} PtR2MsaaInput;
typedef struct {
    uint8_t x, y;             /* R300 subpixel units. */
    uint8_t metal_x, metal_y; /* Exact 1/16 units, valid only without issues. */
} PtR2Sample;
typedef struct {
    uint32_t issues;
    uint8_t count, denominator, gamma22, average_alpha;
    PtR2Sample position[6];
} PtR2MsaaPlan;

/* This checks sample geometry, not format/blend/depth equivalence. It never
 * silently rounds R300's 1/12 positions onto Metal's 1/16 grid. */
PtR2MsaaPlan pt_r2_msaa_plan(PtR2MsaaInput input);
/* Raw attachment payload only: excludes alignment/tile metadata, heaps, VRAM,
 * textures, and in-flight versions. Overflow leaves bytes untouched. */
int pt_r2_msaa_payload_bytes(uint32_t width, uint32_t height, uint32_t color_bytes,
                           uint32_t depth_bytes, const PtR2MsaaPlan *plan,
                           int resolved_color, uint64_t *bytes);
#endif
