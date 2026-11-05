/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_RESOLVE_H
#define PT_R2_RESOLVE_H
#include <stdint.h>
typedef struct {
    float degamma[256];
    uint8_t regamma[4096];
} PtR2Gamma;
/* Gamma 2.2 through an 8-bit degamma and a 12-bit regamma table. Not a claim
 * about undocumented physical Radeon filtering. Call once, outside encode. */
void pt_r2_gamma_init(PtR2Gamma *);
#endif
