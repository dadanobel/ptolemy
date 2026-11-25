/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OPTICS_DIAG=1: the tiling a surface was written with against the tiling its readers name. */
#include "frontend.h"
#include <assert.h>
#include <stdio.h>
static PtRadeon *d;
static void wr(unsigned a, uint32_t v) {
    uint8_t b[4];
    for (unsigned i = 0; i < 4; i++)
        b[i] = (uint8_t)(v >> (8 * i));
    assert(!pt_radeon_mmio(d, a, b, 4, 1));
}
static void copy(uint32_t src, uint32_t src_tile, uint32_t dst, uint32_t dst_tile) { /* one pixel, as the drivers do */
    wr(0x15ac, src);
    wr(0x1704, src_tile);
    wr(0x1404, dst);
    wr(0x1700, dst_tile);
    wr(0x143c, 1u << 16 | 1u);
}
static void expect(unsigned writer, uint64_t reads, uint64_t differs) {
    OpticsFrontendStats s;
    optics_frontend_stats(d, &s);
    assert(s.diag_reads[writer] == reads && s.diag_differs[writer] == differs);
}
int main(void) {
    static uint8_t ram[4096];
    PtRadeonConfig c = {.ram = {ram, sizeof ram, NULL}};
    assert(!pt_radeon_create(&d, &c));
    wr(0x146c, 3u | 6u << 8 | 0xccu << 16 | 2u << 24);
    wr(0x16cc, 0xffffffff);
    wr(0x15d4, 0);
    wr(0x1408, 256);
    wr(0x15b0, 256);
    wr(0x1434, 0);
    wr(0x1438, 0);
    wr(0x142c, 3u << 30); /* stale: DST_TILE, written after it, is what names the destination */
    copy(0x1000, 0, 0x2000, 1);
    expect(0, 1, 0); /* a linear source nobody wrote */
    copy(0x2000, 1, 0x3000, 0);
    expect(2, 1, 0); /* the 2D engine wrote it macro-tiled, a tiled source */
    copy(0x2000, 0, 0x3000, 0);
    expect(2, 2, 1); /* a linear source: another tiling */
    wr(0x0b04, 0x2000);
    wr(0x0b08, 0x2fff);
    wr(0x0b0c, 1u << 16);
    expect(2, 3, 1); /* a macro-tiled SURFACE region over it: the same */
    wr(0x0b0c, 2u << 16);
    expect(2, 4, 2); /* a micro-tiled one: another */
    wr(0x0b04, 0x5000);
    wr(0x0b08, 0x6fff);
    wr(0x0b0c, 1u << 16); /* over VRAM nobody wrote: the CPU becomes the writer of every page */
    copy(0x6000, 0, 0x3000, 0);
    expect(4, 1, 1);
    pt_radeon_close(d);
    puts("PASS: OPTICS_DIAG tiling by writer: 2D through DST_TILE/SRC_TILE, SURFACE regions, pages nobody wrote");
    return 0;
}
