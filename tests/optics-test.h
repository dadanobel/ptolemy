/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared by the CP thread tests: ring and scratch registers, 32-bit MMIO and VRAM access to the device d, and its ring. */
#include "frontend.h"
#include <assert.h>
enum {
    RB = 0x10000,
    SCRATCH = 0x20000,
    CP_RB_BASE = 0x700,
    CP_RB_CNTL = 0x704,
    CP_RB_RPTR = 0x710,
    CP_RB_WPTR = 0x714,
    SCRATCH_UMSK = 0x770,
    SCRATCH_ADDR = 0x774,
    RBBM_STATUS = 0xe40,
    SCRATCH_REG0 = 0x15e0
};
static PtRadeon *d;
static uint32_t wptr;
static inline void mmio(uint32_t a, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    assert(!pt_radeon_mmio(d, a, b, 4, 1));
}
static inline uint32_t rd(uint32_t a) {
    uint8_t b[4];
    assert(!pt_radeon_mmio(d, a, b, 4, 0));
    return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
}
static inline void vw(uint32_t off, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    assert(!pt_radeon_vram(d, off, b, 4, 1));
}
static inline uint32_t vr(uint32_t off) {
    uint8_t b[4];
    assert(!pt_radeon_vram(d, off, b, 4, 0));
    return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
}
static inline void ring(uint32_t w) {
    vw(RB + 4 * (wptr & 1023), w);
    wptr = (wptr + 1) & 1023;
}
static inline void reg(uint32_t a, uint32_t v) {
    ring(a >> 2);
    ring(v);
} /* type-0, one register */
