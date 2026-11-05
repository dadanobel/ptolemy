/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host-data blits (colour source, 8 bpp, as Leopard's window server uploads glyph masks) with and without destination
 * clipping, and BITBLT_MULTI with destination clipping. The glyph words are the ones Leopard sent. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
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
        ATLAS = 0x367000,
        P = 320
    };
    static uint8_t atlas[P * 16], out[P * 16];
    memset(atlas, 0xa5, sizeof atlas);
    assert(optics_frontend_gpu_write(d, ATLAS, atlas, sizeof atlas));
    static const uint32_t glyph[13] = {0x11d91300, 0x0ac67815, 0xff170088, 0xfd8d15ff, 0x172ce1ad,
                                       0x8d15ff18, 0xc4569e8d, 0x15ff7bb8, 0x009e8d15, 0xffee2200,
                                       0x9e8d15ff, 0x7b00009e, 0x8d000000};
    uint8_t bytes[52];
    for (unsigned i = 0; i < 13; i++)
        for (unsigned k = 0; k < 4; k++)
            bytes[4 * i + k] = (uint8_t)(glyph[i] >> (24 - 8 * k));
    wr(d, 0x146c, 0x03cc32fa);
    wr(d, 0x142c, (P / 64) << 22 | ATLAS >> 10);
    wr(d, 0x16ec, 0);
    wr(d, 0x16f0, P << 16 | P); /* GMC, DST_PITCH_OFFSET, scissor */
    wr(d, 0x1438, 0 << 16 | 12);
    wr(d, 0x143c, 7 << 16 | 7);
    for (unsigned i = 0; i < 13; i++)
        wr(d, 0x17c0, glyph[i]);
    assert(optics_frontend_gpu_read(d, ATLAS, out, sizeof out));
    for (unsigned y = 0; y < 16; y++)
        for (unsigned x = 0; x < P; x++)
            assert(out[y * P + x] == (y < 7 && x >= 12 && x < 19 ? bytes[y * 7 + x - 12] : 0xa5));
    assert(out[1 * P + 13] == 0xff && out[6 * P + 17] == 0x9e); /* the glyph's stems (a lowercase letter's) */
    /* Clipped: the same glyph at (6,2), scissor right 10, bottom 5: columns 6-9 of rows 2-4 only. */
    wr(d, 0x16f0, 5 << 16 | 10);
    wr(d, 0x1438, 2 << 16 | 6);
    wr(d, 0x143c, 7 << 16 | 7);
    for (unsigned i = 0; i < 13; i++)
        wr(d, 0x17c0 + 4 * (i % 8), glyph[i]);
    assert(optics_frontend_gpu_read(d, ATLAS, out, sizeof out));
    for (unsigned y = 2; y < 9; y++)
        for (unsigned x = 6; x < 13; x++) {
            uint8_t want = y < 5 && x < 10    ? bytes[(y - 2) * 7 + x - 6]
                           : x >= 12 && y < 7 ? bytes[y * 7 + x - 12]
                                              : 0xa5;
            assert(out[y * P + x] == want);
        }
    /* SRC_ENDIAN 2 left by an earlier copy: the same words, the same glyph at (24,8). */
    wr(d, 0x16f0, P << 16 | P);
    wr(d, 0x15d4, 2);
    wr(d, 0x1438, 8 << 16 | 24);
    wr(d, 0x143c, 7 << 16 | 7);
    for (unsigned i = 0; i < 13; i++)
        wr(d, 0x17c0, glyph[i]);
    wr(d, 0x15d4, 0);
    assert(optics_frontend_gpu_read(d, ATLAS, out, sizeof out));
    for (unsigned y = 8; y < 15; y++)
        assert(!memcmp(out + y * P + 24, bytes + (y - 8) * 7, 7) && out[y * P + 23] == 0xa5 && out[y * P + 31] == 0xa5);
    OpticsFrontendStats st;
    optics_frontend_stats(d, &st);
    assert(st.blits == 3 && !st.blits_refused);
    /* BITBLT_MULTI, 32 bpp, destination clipping as Leopard sends it (GMC 0x52cc36fb, 8 words). */
    enum {
        SRC = 0x100000,
        DST = 0x200000,
        SP = 4096,
        DP = 4096
    };
    static uint8_t src[SP * 8], dst[DP * 8];
    for (unsigned i = 0; i < sizeof src; i++)
        src[i] = (uint8_t)(i * 13 + i / SP);
    memset(dst, 0x5a, sizeof dst);
    assert(optics_frontend_gpu_write(d, SRC, src, sizeof src) && optics_frontend_gpu_write(d, DST, dst, sizeof dst));
    word(d, 0, 0xc0079b00);
    word(d, 1, 0x52cc36fb);
    word(d, 2, (SP / 64) << 22 | SRC >> 10);
    word(d, 3, (DP / 64) << 22 | DST >> 10);
    word(d, 4, 1 << 16 | 3);
    word(d, 5, 6 << 16 | 9); /* scissor: x 3-8, y 1-5 */
    word(d, 6, 10u << 16 | 0);
    word(d, 7, 1u << 16 | 0);
    word(d, 8, 12u << 16 | 8); /* src (10,0) -> dst (1,0), 12 x 8 */
    wr(d, 0x700, 0x800000);
    wr(d, 0x704, 9);
    wr(d, 0x714, 9);
    assert(optics_frontend_gpu_read(d, DST, dst, sizeof dst));
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < 16; x++)
            for (unsigned b = 0; b < 4; b++) {
                int in = x >= 3 && x < 9 && y >= 1 && y < 6;
                assert(dst[y * DP + 4 * x + b] == (in ? src[y * SP + 4 * (x - 1 + 10) + b] : 0x5a));
            }
    optics_frontend_stats(d, &st);
    assert(st.blits == 4 && !st.packets_refused && !st.blits_refused);
    pt_radeon_close(d);
    puts("PASS: host-data glyph upload (packed rows, high byte first whatever SRC_ENDIAN), scissor clipping, BITBLT_MULTI destination clipping");
    return 0;
}
