/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exact raw AoS snapshots, inaccessible padding, sparse indices and CPU fallback. */
#include "r300-test.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t source[3 * 32];
static size_t limit;
static unsigned reads;
static int reject_wide;
static unsigned lowest; /* bytes below this offset of the source cannot be read */
static int fetch(void *ctx, uint64_t a, void *out, size_t n) {
    (void)ctx;
    reads++;
    if (a < 0x4000 + lowest || a - 0x4000 > limit || n > limit - (a - 0x4000) || (reject_wide && n > 80))
        return 0;
    memcpy(out, source + (a - 0x4000), n);
    return 1;
}
static void put(unsigned at, float x, unsigned swap) {
    uint32_t u;
    memcpy(&u, &x, 4);
    for (unsigned k = 0; k < 4; k++)
        source[at + k] = (uint8_t)(u >> (8 * (swap ? 3 - k : k)));
}
int main(int argc, char **argv) {
    (void)argv;
    int expected = argc == 1;
    OpticsR300State *s = optics_r300_state_create(1);
    assert(s);
    wr(s, 0x43e0, 1440 | 1440u << 13);
    wr(s, 0x43e4, 1503 | 1503u << 13);
    wr(s, 0x2090, 1);
    wr(s, 0x4e38, 6u << 21 | 64);
    passthrough(s);
    for (unsigned be = 0; be < 2; be++)
        for (unsigned walk = 1; walk <= 2; walk++) {
            memset(source, 0xcd, sizeof source);
            limit = 2 * 32 + (be ? 24 : 28);
            reads = 0;
            for (unsigned v = 0; v < 3; v++)
                for (unsigned c = 0; c < 6; c++)
                    put(v * 32 + c * 4, c == 3 ? 1.0f : (float)(v * 8 + c) / 16.0f, be);
            for (unsigned v = 0; v < 3; v++) {
                source[v * 32 + 24] = 0;
                source[v * 32 + 25] = 127;
                source[v * 32 + 26] = 255;
                source[v * 32 + 27] = 64;
            }
            wr(s, 0x2140, 1u << 8 | (be ? 2 : 0));
            wr(s, 0x2150, 3u | ((1u | 1u << 8 | (be ? 1u << 13 : 0)) << 16));
            wr(s, 0x2154, 0x8004 | 2u << 8 | 1u << 13);
            wr(s, 0x21e0, 0xf688f688);
            wr(s, 0x21e4, 0xf688);
            OpticsAos aos = {.count = be ? 2 : 3, .array = {{4, 8, 0x4000}, {2, 8, 0x4010}, {1, 8, 0x4018}}};
            uint32_t p[] = {4u | walk << 4 | 3u << 16, 2u | 0u << 16, 1};
            OpticsDraw d;
            const char *why = NULL;
            assert(optics_draw_build(&d, regs, s, walk == 1 ? 0x36 : 0x34, p, walk == 1 ? 3 : 1, &aos, fetch, NULL,
                                     &why));
            assert((d.raw_vertex != NULL) == expected);
            assert((d.vertex == NULL) == expected);
            assert(d.vertices == 3);
            if (expected) {
                assert(reads == 1 && d.raw_stride == 32);
                for (size_t k = limit; k < sizeof source; k++)
                    assert(d.raw_vertex[k] == 0);
            }
            for (unsigned v = 0; v < 3; v++) {
                float input[OPTICS_DRAW_INPUTS][4];
                optics_draw_input(&d, v, input);
                for (unsigned c = 0; c < 4; c++)
                    assert(input[0][c] == (c == 3 ? 1.0f : (float)(v * 8 + c) / 16.0f));
                assert(input[1][0] == (float)(v * 8 + 4) / 16.0f && input[1][1] == (float)(v * 8 + 5) / 16.0f &&
                       input[1][2] == 0 && input[1][3] == 1);
                if (!be) {
                    assert(input[2][0] == 0 && input[2][1] == 127 / 255.0f && input[2][2] == 1 &&
                           input[2][3] == 64 / 255.0f);
                }
                for (unsigned j = be ? 2 : 3; j < 16; j++)
                    assert(input[j][0] == 0 && input[j][1] == 0 && input[j][2] == 0 && input[j][3] == 1);
            }
            if (walk == 1)
                assert(d.index[0] == 2 && d.index[1] == 0 && d.index[2] == 1);
            /* Producer changes after snapshot cannot change either representation. */
            memset(source, 0, sizeof source);
            float input[16][4];
            optics_draw_input(&d, 2, input);
            assert(input[0][0] == 1.0f);
            OpticsVertexOut out[3];
            assert(optics_draw_vertices(&d, out, &why));
            assert(out[2].window[0] == 1.0f);
            optics_draw_free(&d);
            assert(!d.vertex && !d.raw_vertex && !d.index);
            optics_draw_free(&d);
        }
    { /* An indexed draw reads from its lowest index on: indices 2 1 2 never touch vertex 0. */
        for (unsigned v = 0; v < 3; v++)
            for (unsigned c = 0; c < 6; c++)
                put(v * 32 + c * 4, c == 3 ? 1.0f : (float)(v * 8 + c) / 16.0f, 0);
        wr(s, 0x2140, 1u << 8);
        wr(s, 0x2150, 3u | ((1u | 1u << 8) << 16));
        limit = sizeof source;
        lowest = 32;
        OpticsAos low = {.count = 3, .array = {{4, 8, 0x4000}, {2, 8, 0x4010}, {1, 8, 0x4018}}};
        uint32_t q[] = {4u | 1u << 4 | 3u << 16, 2u | 1u << 16, 2};
        OpticsDraw t;
        const char *no = NULL;
        assert(optics_draw_build(&t, regs, s, 0x36, q, 3, &low, fetch, NULL, &no));
        assert(t.vertices == 2 && t.index[0] == 1 && t.index[1] == 0 && t.index[2] == 1);
        float input[OPTICS_DRAW_INPUTS][4];
        optics_draw_input(&t, 0, input);
        assert(input[0][0] == 8 / 16.0f && input[0][1] == 9 / 16.0f && input[1][0] == 12 / 16.0f);
        optics_draw_input(&t, 1, input);
        assert(input[0][0] == 16 / 16.0f && input[1][1] == 21 / 16.0f);
        optics_draw_free(&t);
        lowest = 0;
    }
    /* A layout outside the raw profile must still build through the normalized decoder. */
    wr(s, 0x2140, 1u << 8 | 1);
    wr(s, 0x2150, 3 | 1u << 13);
    wr(s, 0x21e0, 0xf688);
    limit = sizeof source;
    OpticsAos aos = {.count = 1, .array = {{4, 8, 0x4000}}};
    uint32_t p[] = {4 | 2u << 4 | 3u << 16};
    OpticsDraw d;
    const char *why = NULL;
    assert(optics_draw_build(&d, regs, s, 0x34, p, 1, &aos, fetch, NULL, &why));
    assert(d.vertex && !d.raw_vertex);
    optics_draw_free(&d);
    /* A failed speculative combined fetch falls back without publishing borrowed bytes. */
    for (unsigned c = 0; c < 24; c++)
        put(4 * c, (float)(c + 1), 0);
    wr(s, 0x2140, 1u << 8);
    wr(s, 0x2150, 3u | ((1u | 1u << 8 | 1u << 13) << 16));
    wr(s, 0x21e0, 0xf688f688);
    aos.count = 2;
    aos.array[1] = aos.array[0];
    aos.array[1].components = 2;
    aos.array[1].offset += 16;
    reject_wide = 1;
    assert(optics_draw_build(&d, regs, s, 0x34, p, 1, &aos, fetch, NULL, &why));
    assert(d.vertex && !d.raw_vertex);
    float a[16][4], b[16][4];
    optics_draw_input(&d, 0, a);
    optics_draw_free(&d);
    /* PVS bypass: the stream vectors in order are the output vertex, whatever their PVS input field (here 8). */
    wr(s, 0x2150, 3u | ((1u | 8u << 8 | 1u << 13) << 16));
    assert(optics_draw_build(&d, regs, s, 0x34, p, 1, &aos, fetch, NULL, &why));
    optics_draw_input(&d, 0, b);
    optics_draw_free(&d);
    assert(a[1][0] != 0 && !memcmp(a, b, sizeof a));
    wr(s, 0x2150, 3u | ((1u | 1u << 8 | 1u << 13) << 16));
    reject_wide = 0;
    wr(s, 0x2090, 0);
    assert(!optics_draw_build(&d, regs, s, 0x34, p, 1, &aos, fetch, NULL, &why));
    assert(!d.vertex && !d.raw_vertex && !d.index);
    /* One array for three streams (Cro-Mag Rally's terrain, Tiger's driver): a 6-dword vertex of FLOAT_3, BYTE (normalized)
     * and FLOAT_2. Raw layout swizzles, then the game's own (position w 1, colour BGRA, texture coordinate z 0 w 1). */
    memset(source, 0, sizeof source);
    for (unsigned v = 0; v < 3; v++) {
        for (unsigned c = 0; c < 3; c++)
            put(v * 24 + 4 * c, (float)(v * 8 + c) / 16.0f, 0);
        for (unsigned c = 0; c < 4; c++)
            source[v * 24 + 12 + c] = (uint8_t)(10 * (c + 1) + v);
        put(v * 24 + 16, (float)v / 4.0f, 0);
        put(v * 24 + 20, (float)v / 8.0f, 0);
    }
    limit = sizeof source;
    wr(s, 0x2090, 1);
    wr(s, 0x2140, 1u << 8);
    wr(s, 0x2150, 2u | (0x8004u | 1u << 8) << 16);
    wr(s, 0x2154, 1u | 2u << 8 | 1u << 13);
    OpticsAos one = {.count = 1, .array = {{6, 6, 0x4000}}};
    const uint32_t ext[2][3] = {{0xf688, 0xf688, 0xf688}, {0xfa88, 0xf60a, 0xfb08}};
    for (unsigned e = 0; e < 2; e++) {
        wr(s, 0x21e0, ext[e][0] | ext[e][1] << 16);
        wr(s, 0x21e4, ext[e][2]);
        assert(optics_draw_build(&d, regs, s, 0x34, p, 1, &one, fetch, NULL, &why));
        for (unsigned v = 0; v < 3; v++) {
            float in[OPTICS_DRAW_INPUTS][4];
            optics_draw_input(&d, v, in);
            for (unsigned c = 0; c < 3; c++)
                assert(in[0][c] == (float)(v * 8 + c) / 16.0f);
            assert(in[0][3] == 1);
            for (unsigned c = 0; c < 4; c++) {
                unsigned byte = e && c < 3 ? 2 - c : c;
                assert(in[1][c] == (float)(10 * (byte + 1) + v) / 255.0f);
            }
            assert(in[2][0] == (float)v / 4.0f && in[2][1] == (float)v / 8.0f && in[2][2] == 0 && in[2][3] == 1);
        }
        optics_draw_free(&d);
    }
    one.array[0].components = 4; /* FLOAT_2 would run past the only array */
    assert(!optics_draw_build(&d, regs, s, 0x34, p, 1, &one, fetch, NULL, &why));
    optics_r300_state_destroy(s);
    puts("PASS: raw AoS LE/BE, UNORM8, indexed fetch, zero tail, immutable bytes, CPU fallback, one array for three streams");
    return 0;
}
