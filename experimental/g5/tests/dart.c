/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_dart.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define OK(x) assert((x) == PT_G5_OK)
static void entry(PtG5Memory *m, uint64_t base, uint32_t word) {
    uint8_t b[4] = {(uint8_t)(word >> 24), (uint8_t)(word >> 16), (uint8_t)(word >> 8), (uint8_t)word};
    OK(pt_g5_write(m, base + 8, b, 4));
}
int main(void) {
    uint8_t table[4096] = {0}, data[4096] = {0}, direct[4096] = {0};
    uint64_t tb = 8ull << 30, ram = 16ull << 30;
    PtG5Bank banks[] = {{{tb, 4096}, table, false}, {{ram, 4096}, data, false}, {{0x80000000, 4096}, direct, false}};
    PtG5Memory m;
    OK(pt_g5_memory_init(&m, banks, 3));
    PtG5DartConfig d = {(uint32_t)(tb >> 12), 1, true};
    PtG5DartFault f;
    PtG5DmaMapping out;
    entry(&m, tb, 0x80000000u | (uint32_t)(ram >> 12));
    OK(pt_g5_dart_translate(&d, &m, 0x2013, true, false, &out, &f));
    assert(!f.valid && out.pa == ram + 19 && out.length == 4096 - 19);
    uint8_t x = 0xaa;
    OK(pt_g5_write(&m, out.pa, &x, 1));
    assert(data[19] == 0xaa);
    entry(&m, tb, 0xa0000000u | (uint32_t)(ram >> 12));
    memset(&out, 0x55, sizeof out);
    PtG5DmaMapping before = out;
    assert(pt_g5_dart_translate(&d, &m, 0x2000, true, true, &out, &f) == PT_G5_PROTECTION);
    assert(f.valid && f.exception == 0x8000002bu && !memcmp(&out, &before, sizeof out));
    OK(pt_g5_dart_translate(&d, &m, 0x2000, false, false, &out, &f));
    entry(&m, tb, 0xc0000000u | (uint32_t)(ram >> 12));
    assert(pt_g5_dart_translate(&d, &m, 0x2000, false, false, &out, &f) == PT_G5_PROTECTION && (f.exception & 7) == 2);
    entry(&m, tb, 0);
    assert(pt_g5_dart_translate(&d, &m, 0x2000, false, false, &out, &f) == PT_G5_PAGE && (f.exception & 7) == 1);
    assert(pt_g5_dart_translate(&d, &m, 1ull << 22, false, false, &out, &f) == PT_G5_BUS && (f.exception & 7) == 0);
    entry(&m, tb, 0x80000000u | (uint32_t)(ram >> 12));
    OK(pt_g5_dart_translate(&d, &m, 0x80000011, false, false, &out, &f));
    assert(out.pa == 0x80000011);
    OK(pt_g5_dart_translate(&d, &m, 0x8000000000ull + ram + 3, false, false, &out, &f));
    assert(out.pa == ram + 3);
    assert(pt_g5_dart_translate(&d, &m, 0x9000000000ull, false, false, &out, &f) == PT_G5_BUS);
    assert(pt_g5_dart_translate(&d, &m, 1ull << 40, false, false, &out, &f) == PT_G5_BUS);
    d.enabled = false;
    OK(pt_g5_dart_translate(&d, &m, ram + 7, false, false, &out, &f));
    assert(out.pa == ram + 7);
    assert(pt_g5_dart_translate(&d, &m, 1ull << 36, false, false, &out, &f) == PT_G5_BUS);
    d.enabled = true;
    d.pages = 0;
    /* Zero means maximum table size, not zero mappings: this reaches a missing table page. */
    assert(pt_g5_dart_translate(&d, &m, 1ull << 22, false, false, &out, &f) == PT_G5_BUS && (f.exception & 7) == 4);
    pt_g5_memory_destroy(&m);
    puts("PASS CPC945 DART: table above 4GiB, target at 16GiB, read/write protection, faults, bypass aliases, bounds");
}
