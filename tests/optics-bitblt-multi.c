/* SPDX-License-Identifier: GPL-2.0-or-later */
/* BITBLT_MULTI (packet3 0x9B, and 0x1B without CNTL) with the observed register settings, generated pixels and no
 * guest data. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void wr(PtRadeon *d, unsigned a, uint32_t v) {
    uint8_t b[4];
    for (unsigned k = 0; k < 4; k++)
        b[k] = (uint8_t)(v >> (k * 8));
    assert(!pt_radeon_mmio(d, a, b, 4, 1));
}
static void word(PtRadeon *d, unsigned i, uint32_t v) {
    uint8_t b[4];
    for (unsigned k = 0; k < 4; k++)
        b[k] = (uint8_t)(v >> (k * 8));
    assert(optics_frontend_gpu_write(d, 0x800000 + 4 * i, b, 4));
}
int main(void) {
    static uint8_t ram[4096];
    PtRadeonConfig c = {.ram = {ram, sizeof ram, NULL}};
    PtRadeon *d;
    assert(!pt_radeon_create(&d, &c));
    enum {
        SP = 4096,
        DP = 3584,
        HEIGHT = 200,
        DST = 0x900000,
        W = 23,
        H = 11,
        SX = 123,
        SY = 148
    };
    uint8_t *source = malloc(SP * HEIGHT), *target = malloc(DP * 32);
    assert(source && target);
    for (unsigned i = 0; i < SP * HEIGHT; i++)
        source[i] = (uint8_t)(i * 7 + (i / SP) * 37);
    memset(target, 0xa5, DP * 32);
    assert(optics_frontend_gpu_write(d, 0, source, SP * HEIGHT));
    assert(optics_frontend_gpu_write(d, DST, target, DP * 32));
    wr(d, 0x16e0, 0x4e002400);
    wr(d, 0x15d4, 2); /* A preceding upload must not byte-swap a logical video copy. */
    word(d, 0, 0xc0049b00);
    word(d, 1, 0x52cc36f1);
    word(d, 2, 0x10000000);
    word(d, 3, SX << 16 | SY);
    word(d, 4, 0);
    word(d, 5, W << 16 | H);
    wr(d, 0x700, 0x800000);
    wr(d, 0x704, 9);
    wr(d, 0x714, 6);
    assert(optics_frontend_gpu_read(d, DST, target, DP * 32));
    for (unsigned y = 0; y < 32; y++)
        for (unsigned x = 0; x < DP; x++)
            assert(target[y * DP + x] == (y < H && x < W * 4 ? source[(SY + y) * SP + SX * 4 + x] : 0xa5));
    OpticsFrontendStats stats;
    optics_frontend_stats(d, &stats);
    assert(stats.blits == 1 && !stats.blits_refused && !stats.packets_refused);
    /* Multiple rectangles in one packet and both pitch/offset setup fields. */
    word(d, 6, 0xc0089b00);
    word(d, 7, 0x52cc36f3);
    word(d, 8, 0x10000000);
    word(d, 9, 0x4e002400);
    word(d, 10, SX << 16 | SY);
    word(d, 11, 3u << 16 | 15u);
    word(d, 12, 7u << 16 | 2u);
    word(d, 13, (SX + 7u) << 16 | SY);
    word(d, 14, 12u << 16 | 19u);
    word(d, 15, 5u << 16 | 3u);
    wr(d, 0x714, 16);
    assert(optics_frontend_gpu_read(d, DST, target, DP * 32));
    for (unsigned y = 0; y < 2; y++)
        assert(!memcmp(target + (15 + y) * DP + 3 * 4, source + (SY + y) * SP + SX * 4, 7 * 4));
    for (unsigned y = 0; y < 3; y++)
        assert(!memcmp(target + (19 + y) * DP + 12 * 4, source + (SY + y) * SP + (SX + 7) * 4, 5 * 4));
    optics_frontend_stats(d, &stats);
    assert(stats.blits == 3 && !stats.packets_refused);
    /* Truncation is rejected before setup or pixels change. */
    uint8_t before[4];
    assert(optics_frontend_gpu_read(d, DST, before, 4));
    word(d, 16, 0xc0039b00);
    word(d, 17, 0x52cc36f1);
    word(d, 18, 0x10000000);
    word(d, 19, 0);
    word(d, 20, 0);
    wr(d, 0x714, 21);
    uint8_t after[4];
    assert(optics_frontend_gpu_read(d, DST, after, 4));
    assert(!memcmp(before, after, 4));
    optics_frontend_stats(d, &stats);
    assert(stats.packets_refused == 1);
    /* 0x1B: rectangles only, on the state the last accepted CNTL packet left (observed order: 0x9B, then 0x1B). */
    word(d, 21, 0xc0021b00);
    word(d, 22, (SX + 2u) << 16 | SY);
    word(d, 23, 30u << 16 | 5u);
    word(d, 24, 4u << 16 | 2u);
    wr(d, 0x714, 25);
    assert(optics_frontend_gpu_read(d, DST, target, DP * 32));
    for (unsigned y = 0; y < 2; y++)
        assert(!memcmp(target + (5 + y) * DP + 30 * 4, source + (SY + y) * SP + (SX + 2) * 4, 4 * 4));
    optics_frontend_stats(d, &stats);
    assert(stats.blits == 4 && stats.packets_refused == 1);
    /* 0x92 (CNTL_BITBLT), as Leopard's Preview scrolls (t847: GMC 52cc36f1, source given, destination the default
     * pitch/offset, the same surface): up, then down, each overlapping its source. The result is a copy of the source
     * rectangle as it was before the packet. */
    enum {
        S = DST + 64 * DP,
        ROWS = 24,
        RW = 20,
        RH = 16
    };
    uint8_t *want = malloc(DP * ROWS), *got = malloc(DP * ROWS), *rect = malloc(RW * 4 * RH);
    assert(want && got && rect);
    for (unsigned i = 0; i < DP * ROWS; i++)
        want[i] = (uint8_t)(i * 5 + (i / DP) * 11);
    assert(optics_frontend_gpu_write(d, S, want, DP * ROWS));
    wr(d, 0x16e0, (DP / 64) << 22 | S >> 10);
    for (unsigned n = 0; n < 2; n++) {
        unsigned sy = n ? 2 : 6, dy = n ? 6 : 2, at = 25 + 6 * n;
        word(d, at, 0xc0049200);
        word(d, at + 1, 0x52cc36f1);
        word(d, at + 2, (DP / 64) << 22 | S >> 10);
        word(d, at + 3, 1u << 16 | sy);
        word(d, at + 4, 1u << 16 | dy);
        word(d, at + 5, (unsigned)RW << 16 | RH);
        wr(d, 0x714, at + 6);
        for (unsigned y = 0; y < RH; y++)
            memcpy(rect + y * RW * 4, want + (sy + y) * DP + 4, RW * 4);
        for (unsigned y = 0; y < RH; y++)
            memcpy(want + (dy + y) * DP + 4, rect + y * RW * 4, RW * 4);
        assert(optics_frontend_gpu_read(d, S, got, DP * ROWS));
        assert(!memcmp(got, want, DP * ROWS));
    }
    optics_frontend_stats(d, &stats);
    assert(stats.blits == 6 && stats.packets_refused == 1);
    pt_radeon_close(d);
    free(source);
    free(target);
    free(want);
    free(got);
    free(rect);
    puts("PASS: 0x9B settings, default pitch/offset, XY order, multiple rectangles, canonical colour bytes, truncation, 0x1B on inherited state, 0x92 overlapping scrolls");
    return 0;
}
