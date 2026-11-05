/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Vertex stream element decode of the R300 draw builder (BYTE, D3DCOLOR, SHORT, SIGNED/NORMALIZE). */
#include "../src/optics/draw.c"
#include <assert.h>
#include <stdio.h>
#include "r300-test.h"
int main(void) {
    float r[4];
    uint32_t w[4] = {0x80ff4000, 0x00017fff, 0, 0};
    decode_stream(4 | 1u << 15, 0, w, r); /* BYTE, NORMALIZE: x = bits 7:0 */
    assert(r[0] == 0 && fabsf(r[1] - 64 / 255.0f) < 1e-6f && r[2] == 1 && fabsf(r[3] - 128 / 255.0f) < 1e-6f);
    decode_stream(5 | 1u << 15, 0, w, r); /* D3DCOLOR: x,y,z = bytes 2,1,0 */
    assert(r[0] == 1 && fabsf(r[1] - 64 / 255.0f) < 1e-6f && r[2] == 0);
    decode_stream(4 | 1u << 14 | 1u << 15, 1, w, r); /* signed byte, method 1: 0x80 -> -1 (clamped), 0xff -> -1/127 */
    assert(r[3] == -1 && fabsf(r[2] + 1 / 127.0f) < 1e-6f);
    decode_stream(4 | 1u << 14 | 1u << 15, 0, w, r); /* method 0: 0x80 -> -128/127, below -1 */
    assert(fabsf(r[3] + 128 / 127.0f) < 1e-6f && fabsf(r[0] - 0) < 1e-6f);
    decode_stream(7, 0, w, r); /* SHORT_4, raw integers */
    assert(r[0] == 0x4000 && r[1] == 0x80ff && r[2] == 0x7fff && r[3] == 1);
    decode_stream(6 | 1u << 15, 0, w, r); /* SHORT_2 normalized; z,w default */
    assert(fabsf(r[0] - 0x4000 / 65535.0f) < 1e-6f && r[2] == 0 && r[3] == 1);
    decode_stream(1, 0, w, r);
    assert(r[2] == 0 && r[3] == 1); /* FLOAT_2 */
    assert(vap_swap(0x11223344, 0) == 0x11223344 && vap_swap(0x11223344, 1) == 0x22114433 &&
           vap_swap(0x11223344, 2) == 0x44332211 && vap_swap(0x11223344, 3) == 0x33441122);
    { /* DISCARD_SRC_PIXELS only with blending on (Halo's shadow blur: blending off, the field set, its zero pixels written) */
        OpticsR300State *s = optics_r300_state_create(1);
        assert(s);
        triangle_state(s);
        wr(s, 0x4e38, 6u << 21 | 64);
        passthrough(s);
        const uint32_t cases[][4] = {{0x2726000e, 0x27260000, 0, 0},
                                     {0x2726000f, 0x27260000, 1, 1},
                                     {0x2320001f, 0x23200000, 3, 1},
                                     {0x00000038, 0, 0, 0}}; /* CBLEND, ABLEND, discard, blending */
        for (unsigned c = 0; c < 4; c++) {
            OpticsDraw d;
            const char *why = NULL;
            wr(s, 0x4e04, cases[c][0]);
            wr(s, 0x4e08, cases[c][1]);
            assert(optics_draw_build(&d, regs, s, 0x35, TRIANGLE, 13, NULL, NULL, NULL, &why));
            assert(d.discard == cases[c][2] && (d.blend != 0) == (cases[c][3] != 0));
            optics_draw_free(&d);
        }
        /* DXT1 with the alpha select ONE (Tiger's driver, SimCity 4): alpha from the block, so its transparent texels
         * stay transparent; W8Z8Y8X8 with ONE keeps it. Program: TEX LD unit 0 into temp 1, out = temp 1 (optics-dxt). */
        const uint32_t tf[] = {8,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0x400000,
                               (1u << 15) | (1u << 6),
                               (7u << 26) | 1u,
                               (1u << 24) | 1u,
                               (21u << 7) | (20u << 14),
                               9u | (17u << 7) | (16u << 14)};
        for (unsigned k = 0; k < 12; k++)
            wr(s, US_HEAD[k], tf[k]);
        wr(s, 0x4104, 1);
        wr(s, 0x4480, 15 | 15u << 11);
        wr(s, 0x4540, 0x200000);
        const uint32_t formats[][2] = {{0xf, 0x0123},
                                       {0xc, 0x2105}}; /* TX_FORMAT, expected selects R,G,B,A as hex digits */
        for (unsigned c = 0; c < 2; c++) {
            OpticsDraw d;
            const char *why = NULL;
            wr(s, 0x4e04, 0);
            wr(s, 0x4e08, 0);
            wr(s, 0x44c0, formats[c][0] | 5u << 9 | 2u << 12 | 1u << 15 | 0u << 18); /* A ONE, R Z, G Y, B X */
            if (!optics_draw_build(&d, regs, s, 0x35, TRIANGLE, 13, NULL, NULL, NULL, &why)) {
                fprintf(stderr, "%s\n", why);
                assert(0);
            }
            const uint8_t *q = d.texture[0].select;
            assert((unsigned)(q[0] << 12 | q[1] << 8 | q[2] << 4 | q[3]) == formats[c][1]);
            optics_draw_free(&d);
        }
        optics_r300_state_destroy(s);
    }
    puts("optics-draw-decode: ok (stream elements, VAP swaps, source pixel discard only while blending, DXT1 alpha)");
    return 0;
}
