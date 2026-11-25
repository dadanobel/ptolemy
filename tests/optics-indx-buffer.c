/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Indices from a buffer, as Mesa's r300 draws: 3D_DRAW_INDX_2 with the VF_CNTL word alone, then INDX_BUFFER with the
 * indices' address and their number of dwords. The executor must get the INDX_2 draw the pair stands for - 16-bit pairs
 * and 32-bit indices, from VRAM as stored and from system memory through VAP_CNTL_STATUS's swap - and nothing when the buffer is too short, outside GPU memory, or does not follow its draw. */
#include "optics-test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VERT = 0x30000, INDX = 0x31000 };
static unsigned drawn, got_indices, got_vertices;
static uint32_t got[16];
static int draw_(void *o, PtRadeon *dv, const OpticsDraw *dr, const char **why) {
    (void)o;
    (void)dv;
    (void)why;
    drawn++;
    got_indices = dr->indices;
    got_vertices = dr->vertices;
    for (uint32_t i = 0; i < dr->indices && i < 16; i++)
        got[i] = dr->index[i];
    return OPTICS_EXEC_DONE;
}
static void state(void) { /* the state of tests/optics-draw-cleanup.c, and one array of four 4-float vertices in VRAM */
    static const uint32_t a[] = {0x43e0, 0x43e4, 0x2140, 0x20b4, 0x2150, 0x21e0, 0x2090, 0x4600, 0x4604, 0x4608,
                                 0x4610, 0x4614, 0x4618, 0x461c, 0x4620, 0x46c0, 0x47c0, 0x48c0, 0x49c0, 0x4e38,
                                 0x20c0, 0x20c4, 0x20c8};
    const uint32_t v[] = {1440 | 1440u << 13, 1503 | 1503u << 13, 1u << 8, 4, 3u | 1u << 13,
                          0xf000 | 1u << 3 | 2u << 6 | 3u << 9, 1, 0, 0, 0, 0, 0, 0, 0x400000, 0, 7u << 26, 1u << 24,
                          (21u << 7) | (20u << 14), 9u | (17u << 7) | (16u << 14), 6u << 21 | 64,
                          1, 4 | 4u << 8, VERT};
    for (unsigned i = 0; i < sizeof a / sizeof *a; i++)
        reg(a[i], v[i]);
    for (unsigned i = 0; i < 16; i++)
        vw(VERT + 4 * i, i % 4 == 3 ? 0x3f800000 : 0);
}
static void indexed(uint32_t vf, uint32_t word0, uint32_t address, uint32_t dwords, int with_draw) {
    if (with_draw) {
        ring(0xc0000000u | 0u << 16 | 0x36u << 8);
        ring(vf);
    }
    ring(0xc0000000u | 2u << 16 | 0x33u << 8);
    ring(word0);
    ring(address);
    ring(dwords);
    mmio(CP_RB_WPTR, wptr);
}
int main(void) {
    static const OpticsExecutor ex = {.draw = draw_};
    optics_frontend_set_executor(&ex);
    uint8_t *ram = calloc(1, 1 << 20);
    assert(ram);
    PtRadeonConfig c = {.ram = {ram, 1 << 20, NULL}};
    assert(!pt_radeon_create(&d, &c));
    mmio(CP_RB_BASE, RB);
    mmio(CP_RB_CNTL, 9);
    wptr = 0;
    state();
    OpticsFrontendStats s;
    const uint32_t triangles = 4u | 1u << 4; /* triangle list, walk: indices */
    /* 16-bit indices, two to a dword, low half first: 0 1 2, 2 1 3 */
    vw(INDX, 0 | 1u << 16);
    vw(INDX + 4, 2 | 2u << 16);
    vw(INDX + 8, 1 | 3u << 16);
    indexed(triangles | 6u << 16, 0x80000810, INDX, 3, 1);
    static const uint32_t six[6] = {0, 1, 2, 2, 1, 3};
    assert(drawn == 1 && got_indices == 6 && got_vertices == 4 && !memcmp(got, six, sizeof six));
    /* an odd count: the last dword's high half is not an index */
    vw(INDX + 4, 2 | 0xffffu << 16);
    indexed(triangles | 3u << 16, 0x80000810, INDX, 2, 1);
    assert(drawn == 2 && got_indices == 3 && got_vertices == 3 && !memcmp(got, six, 3 * sizeof *six));
    /* 32-bit indices (VF_CNTL bit 11): a dword each */
    vw(INDX, 3);
    vw(INDX + 4, 1);
    vw(INDX + 8, 0);
    indexed(triangles | 1u << 11 | 3u << 16, 0x80000810, INDX, 3, 1);
    static const uint32_t three[3] = {3, 1, 0};
    assert(drawn == 3 && got_indices == 3 && got_vertices == 4 && !memcmp(got, three, sizeof three));
    /* Leopard's forms (u195): an offset of one 32-bit index as SKIP_COUNT 1, the skipped dword counted in the size */
    vw(INDX + 0x100, 9);
    vw(INDX + 0x104, 3);
    vw(INDX + 0x108, 1);
    vw(INDX + 0x10c, 0);
    indexed(triangles | 1u << 11 | 3u << 16, 0x80010810, INDX + 0x100, 4, 1);
    assert(drawn == 4 && got_indices == 3 && !memcmp(got, three, sizeof three));
    /* and 16-bit indices from a buffer's second halfword: base + 2, one dword more than the indices fill */
    vw(INDX + 0x100, 0x7777 | 0u << 16);
    vw(INDX + 0x104, 1 | 2u << 16);
    vw(INDX + 0x108, 2 | 1u << 16);
    vw(INDX + 0x10c, 3 | 0x7777u << 16);
    indexed(triangles | 6u << 16, 0x80000810, INDX + 0x102, 4, 1);
    assert(drawn == 5 && got_indices == 6 && !memcmp(got, six, sizeof six));
    indexed(triangles | 6u << 16, 0x80000810, INDX + 0x102, 3, 1); /* without the extra dword: too short, refused */
    assert(drawn == 5);
    /* system memory (one PCI GART page at 0x80000000) with VC_SWAP 32-bit: the dwords as a big-endian guest stores them */
    static const uint8_t big[12] = {0, 1, 0, 0, 0, 2, 0, 2, 0, 3, 0, 1};
    memcpy(ram + 0x41000, big, sizeof big);
    ram[0x40001] = 0x10;
    ram[0x40002] = 0x04; /* the page table's entry, little-endian: 0x41000 */
    mmio(0x0ab0, 0x40000);
    mmio(0x01dc, 0x80000000);
    mmio(0x01e0, 0x80000fff);
    mmio(0x01d0, 1);
    reg(0x2140, 1u << 8 | 2);
    indexed(triangles | 6u << 16, 0x80000810, 0x80000000, 3, 1);
    assert(drawn == 6 && got_indices == 6 && got_vertices == 4 && !memcmp(got, six, sizeof six));
    optics_frontend_stats(d, &s);
    assert(!s.failed && s.draws == 6 && !s.draws_refused && s.packets_refused == 1);
    /* refused, and nothing drawn: fewer dwords than the draw needs; indices outside GPU memory; another form of the
     * packet; INDX_BUFFER without its draw (also: the refusals above did not leave one waiting), or with another
     * packet between the two */
    indexed(triangles | 6u << 16, 0x80000810, INDX, 2, 1);
    indexed(triangles | 6u << 16, 0x80000810, 0x7ff00000, 3, 1);
    indexed(triangles | 6u << 16, 0x80000814, INDX, 3, 1); /* another destination */
    indexed(0, 0x80000810, INDX, 3, 0);
    ring(0xc0000000u | 0u << 16 | 0x36u << 8);
    ring(triangles | 6u << 16);
    ring(0xc0000000u | 0u << 16 | 0x2fu << 8); /* 3D_LOAD_VBPNTR: one array, as it is */
    ring(1);
    indexed(0, 0x80000810, INDX, 3, 0);
    optics_frontend_stats(d, &s);
    assert(drawn == 6 && s.draws == 6 && s.packets_refused == 6 && !s.failed);
    /* and the next one draws again */
    indexed(triangles | 1u << 11 | 3u << 16, 0x80000810, INDX, 3, 1);
    assert(drawn == 7);
    pt_radeon_close(d);
    free(ram);
    puts("optics-indx-buffer: ok");
    return 0;
}
