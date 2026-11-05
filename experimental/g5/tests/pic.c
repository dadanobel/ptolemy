/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_pic.h"
#include <assert.h>
#include <stdio.h>
#define OK(x) assert((x) == PT_G5_OK)
static void wr(PtG5Pic *p, unsigned c, uint32_t a, uint32_t v) {
    OK(pt_g5_pic_access(p, c, a, &v, true));
}
static uint32_t rd(PtG5Pic *p, unsigned c, uint32_t a) {
    uint32_t v;
    OK(pt_g5_pic_access(p, c, a, &v, false));
    return v;
}
static void setup(PtG5Pic *p, unsigned i, unsigned pr, unsigned vector, unsigned dest, bool level) {
    wr(p, 0, 0x10000 + i * 32, (level ? 0x400000 : 0) | (pr << 16) | vector);
    wr(p, 0, 0x10010 + i * 32, dest);
}
int main(void) {
    PtG5Pic p;
    OK(pt_g5_pic_init(&p, 4));
    assert(rd(&p, 0, 0x1000) == 0x7f0302);
    wr(&p, 0, 0x1020, 0x20000000);
    for (unsigned c = 0; c < 4; c++) {
        assert(rd(&p, c, 0x90) == c);
        wr(&p, c, 0x80, 0);
    }
    wr(&p, 0, 0x10a0, 12u << 16 | 220);
    wr(&p, 2, 0x40, 0xb);
    assert(pt_g5_pic_irqs(&p) == 0xb);
    for (unsigned c = 0; c < 4; c++)
        if (c != 2) {
            assert(rd(&p, c, 0xa0) == 220);
            wr(&p, c, 0xb0, 0);
        }
    assert(!pt_g5_pic_irqs(&p));
    assert(rd(&p, 2, 0xa0) == 255);
    setup(&p, 1, 4, 33, 1, false);
    OK(pt_g5_pic_input(&p, 1, true));
    OK(pt_g5_pic_input(&p, 1, false));
    assert(pt_g5_pic_irqs(&p) == 1);
    assert(rd(&p, 0, 0xa0) == 33);
    assert(rd(&p, 0, 0x10020) & 0x40000000);
    setup(&p, 2, 4, 34, 1, false);
    OK(pt_g5_pic_input(&p, 2, true));
    assert(!pt_g5_pic_irqs(&p));
    setup(&p, 3, 8, 35, 1, false);
    OK(pt_g5_pic_input(&p, 3, true));
    assert(pt_g5_pic_irqs(&p) == 1);
    assert(rd(&p, 0, 0xa0) == 35);
    wr(&p, 0, 0xb0, 0);
    assert(!pt_g5_pic_irqs(&p));
    wr(&p, 0, 0xb0, 0);
    assert(rd(&p, 0, 0xa0) == 34);
    wr(&p, 0, 0xb0, 0);
    setup(&p, 4, 3, 36, 8, true);
    OK(pt_g5_pic_input(&p, 4, true));
    assert(rd(&p, 0, 0x230a0) == 36);
    wr(&p, 0, 0x230b0, 0);
    assert(pt_g5_pic_irqs(&p) == 8);
    OK(pt_g5_pic_input(&p, 4, false));
    assert(!pt_g5_pic_irqs(&p));
    setup(&p, 5, 5, 37, 15, false);
    for (unsigned c = 0; c < 4; c++) {
        OK(pt_g5_pic_input(&p, 5, true));
        OK(pt_g5_pic_input(&p, 5, false));
        assert(pt_g5_pic_irqs(&p) == (1u << c));
        assert(rd(&p, c, 0xa0) == 37);
        wr(&p, c, 0xb0, 0);
    }
    wr(&p, 0, 0x1120, 10u << 16 | 240);
    wr(&p, 0, 0x1130, 6);
    wr(&p, 0, 0x1110, 100);
    pt_g5_pic_tick(&p, 99);
    assert(!pt_g5_pic_irqs(&p));
    assert(rd(&p, 0, 0x1100) == 1);
    pt_g5_pic_tick(&p, 1);
    assert(pt_g5_pic_irqs(&p) == 6);
    assert(rd(&p, 0, 0x1100) == 0x80000064);
    for (unsigned c = 1; c <= 2; c++) {
        assert(rd(&p, c, 0xa0) == 240);
        wr(&p, c, 0xb0, 0);
    }
    pt_g5_pic_tick(&p, 250);
    assert(rd(&p, 0, 0x1100) == 0x80000032);
    wr(&p, 0, 0x1110, 0x80000064);
    pt_g5_pic_tick(&p, 999);
    assert(rd(&p, 0, 0x1100) == 0x80000032);
    for (unsigned c = 1; c <= 2; c++) {
        assert(rd(&p, c, 0xa0) == 240);
        wr(&p, c, 0xb0, 0);
    }
    wr(&p, 0, 0x1120, 0x800a00f0);
    wr(&p, 0, 0x1110, 100);
    pt_g5_pic_tick(&p, 100);
    wr(&p, 0, 0x1120, 0x000a00f0);
    assert(!pt_g5_pic_irqs(&p));
    wr(&p, 0, 0x1090, 4);
    assert(pt_g5_pic_reset_lines(&p) == 4 && rd(&p, 2, 0x80) == 15);
    wr(&p, 0, 0x1020, 0x80000000);
    assert(!pt_g5_pic_reset_lines(&p) && !pt_g5_pic_irqs(&p));
    assert(rd(&p, 0, 0x10e0) == 255 && rd(&p, 3, 0x80) == 15);
    uint32_t v = 123;
    assert(pt_g5_pic_access(&p, 0, 0x1070, &v, false) == PT_G5_UNSUPPORTED && v == 123);
    pt_g5_pic_destroy(&p);
    puts("PASS PIC: four CPU banks, multicast IPI, fair I/O routing, nesting, EOI, level/edge, timers, reset");
}
