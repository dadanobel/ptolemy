/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_PRIMITIVE_H
#define PT_R2_PRIMITIVE_H
#include <stdint.h>

typedef struct {
    uint32_t vertex[3], face[3], flat;
    uint8_t edge_mask; /* edges 0->1, 1->2, 2->0; excludes added diagonals */
} PtR2Triangle;
/* Pure index lowering, no expanded vertex copies. Values are source slots,
 * before applying the guest index buffer. face/flat preserve polygon-level
 * facing and provoking-vertex inputs; triangles alone are insufficient for
 * flat shading, two-sided colour and polygon line mode. Unsupported is 0;
 * an incomplete supported primitive succeeds with zero triangles. */
int pt_r2_triangle_count(unsigned primitive,uint32_t vertices,uint64_t *count);
int pt_r2_triangle(unsigned primitive,uint32_t vertices,uint64_t ordinal,
                    PtR2Triangle *);
#endif
