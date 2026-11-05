/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared by the R300 draw tests: the register file beside the state, one triangle's state and packet, and the
 * passthrough US program (colour = constant 0). */
#include "draw.h"
#include "r300_state.h"
static uint32_t regs[0x10000 / 4];
static inline void wr(OpticsR300State *s, unsigned a, uint32_t v) {
    regs[a / 4] = v;
    (void)optics_r300_write(s, a, v);
}
static const unsigned US_HEAD[12] = {0x4600, 0x4604, 0x4608, 0x4610, 0x4614, 0x4618,
                                     0x461c, 0x4620, 0x46c0, 0x47c0, 0x48c0, 0x49c0}; /* a 12-word US program */
static inline void triangle_state(OpticsR300State *s) {
    wr(s, 0x43e0, 1440 | 1440u << 13);
    wr(s, 0x43e4, 1503 | 1503u << 13);
    wr(s, 0x2140, 1u << 8);
    wr(s, 0x20b4, 4);
    wr(s, 0x2150, 3u | 1u << 13);
    wr(s, 0x21e0, 0xf000 | 1u << 3 | 2u << 6 | 3u << 9);
    wr(s, 0x2090, 1);
}
static inline void passthrough(OpticsR300State *s) {
    static const uint32_t fs[] = {
            0, 0, 0, 0, 0, 0, 0x400000, 0, 7u << 26, 1u << 24, (21u << 7) | (20u << 14), 9u | (17u << 7) | (16u << 14)};
    for (unsigned k = 0; k < 12; k++)
        wr(s, US_HEAD[k], fs[k]);
}
static const uint32_t TRIANGLE[13] = {4u | 3u << 4 | 3u << 16,
                                      0,
                                      0,
                                      0,
                                      0x3f800000,
                                      0,
                                      0,
                                      0,
                                      0x3f800000,
                                      0,
                                      0,
                                      0,
                                      0x3f800000}; /* VAP_VF_CNTL, 3 vertices */
