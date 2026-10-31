/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_RESOLVE_H
#define PT_R2_RESOLVE_H
#include <stdint.h>
typedef struct {float degamma[256];uint8_t regamma[4096];} PtR2Gamma;
/* Gamma 2.2 through an 8-bit degamma and a 12-bit regamma table. Not a claim
 * about undocumented physical Radeon filtering. Call once, outside encode. */
void pt_r2_gamma_init(PtR2Gamma *);
/* samples are logical RGBA bytes. Only count 1/2/4 is this native fast lane;
 * ctl bit0 enables resolve, bit1 gamma22 RGB, bit2 averages alpha (else sample 0).
 * Disabled/invalid input leaves result untouched. One sample copies unchanged. */
int pt_r2_resolve_pixel(const PtR2Gamma *,const uint8_t (*samples)[4],
                        unsigned count,uint32_t ctl,uint8_t result[4]);
#endif
