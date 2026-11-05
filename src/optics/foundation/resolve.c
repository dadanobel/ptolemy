/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "resolve.h"
#include <math.h>

void pt_r2_gamma_init(PtR2Gamma *g) {
    if (!g)
        return;
    for (unsigned i = 0; i < 256; i++)
        g->degamma[i] = powf(i / 255.0f, 2.2f);
    for (unsigned i = 0; i < 4096; i++)
        g->regamma[i] = (uint8_t)lrintf(255.0f * powf(i / 4095.0f, 1.0f / 2.2f));
}
