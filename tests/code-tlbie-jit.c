/* SPDX-License-Identifier: GPL-2.0-or-later
 * K4: a tlbie keeps the page generation (the jump-cache cells) only while every translation its cells were filled with
 * gives the same physical page for a fetch: jc_fill lists them and marks the generation (PG_CELLS); code_same checks
 * them against the BATs and the guest's page table - a changed RPN, a cleared R, a removed PTE, a fetch the key no
 * longer allows or more translations than the list holds each move the generation; and the VM's tlbie leaves for
 * the host when cells may hold the page number. */
#include "jit-test.h"
static uint8_t gmem[1 << 20];
static uint32_t gens[65536];
static void be(uint8_t *p, uint32_t v) {
    p[0] = v >> 24, p[1] = v >> 16, p[2] = v >> 8, p[3] = (uint8_t)v;
}
static uint8_t *pte(CPU *c, uint32_t vsid, uint32_t q, uint32_t w1) { // primary PTEG, slot 0
    uint32_t h = (vsid & 0x7FFFF) ^ q, pteg = (c->spr[SPR_SDR1] & 0xFFFF0000u) | (h & 0x3FF) << 6;
    be(gmem + pteg, 0x80000000u | vsid << 7 | q >> 10);
    be(gmem + pteg + 4, w1);
    return gmem + pteg;
}
static void fill(CPU *c, uint32_t pc, uint32_t ra) { // a lookup exit's fill, as jit_run makes it after block()
    c->pc = pc;
    blk_ra = ra;
    jc_fill(c, (uint32_t *)0x1000, 0);
}
int main(void) {
    static CPU cpu;
    CPU *c = &cpu;
    c->preg[0] = (PRegion){.base = 0, .size = sizeof gmem, .host = gmem};
    c->npreg = 1;
    c->spr[287] = 0x00080000; // 750
    c->spr[SPR_SDR1] = 0x00010000; // 64 KiB page table at 64 KiB
    jit_pggen = gens;
    kir = 1;
    uint32_t q = 0x0775, vsid = 0x123456, pc = 0x10000000 | q << 12, ra = 0x00042000;
    c->sr[1] = vsid;
    uint8_t *e = pte(c, vsid, q, ra | 0x100 | 2);
    fill(c, pc, ra);
    assert(gens[q] & PG_CELLS && njc_xl[q] == 1);
    assert(code_same(c, q));
    be(e + 4, 0x00043000 | 0x100 | 2); // another physical page
    assert(!code_same(c, q));
    be(e + 4, ra | 2); // R cleared: the next fetch must set it
    assert(!code_same(c, q));
    be(e + 4, ra | 0x100 | 0); // PP 0 with key 1: no fetch
    c->sr[1] = vsid | 0x40000000u;
    njc_xl[q] = 0;
    fill(c, pc, ra);
    assert(!code_same(c, q));
    be(e + 4, ra | 0x100 | 2);
    assert(code_same(c, q));
    be(e, 0); // removed
    assert(!code_same(c, q));
    /* A BAT translation stays without any PTE; a fill in real mode lists nothing. */
    njc_xl[q] = 0;
    c->spr[528] = 0x20000000 | 2; // IBAT0: EA 0x20000000, 128 KiB, supervisor
    c->spr[529] = 0x00060000 | 2;
    kir = 1;
    fill(c, 0x20000000 | (q & 0x1f) << 12, 0x00060000 | (q & 0x1f) << 12);
    kir = 0;
    fill(c, 0x00000000 | (q & 0x1f) << 12, (q & 0x1f) << 12);
    kir = 1;
    assert(njc_xl[q & 0x1f] == 1 && code_same(c, q & 0x1f));
    /* More translations than the list holds: the generation always moves. */
    for (uint32_t k = 0; k < JC_XL + 1; k++) {
        c->sr[3] = 0x200000 + k;
        pte(c, 0x200000 + k, 0x0123, (0x00070000 + (k << 12)) | 0x100 | 2);
        fill(c, 0x30000000 | 0x0123 << 12, 0x00070000 + (k << 12));
    }
    assert(njc_xl[0x0123] == JC_XL + 1 && !code_same(c, 0x0123));
    /* A tlbie translated for the VM leaves for the host when bit 31 of the generation (a 32-bit load) is set. */
    static uint32_t buf[8192];
    memset(cmap, -1, sizeof cmap);
    cwr = 0;
    cp = buf;
    exit_common = buf + 4096;
    jit_mapped_off = 0x1000;
    jit_pggen_off = 0x2000;
    kpr = 0;
    assert(tr(0x7c00aa64, 0x2000) == 0); // tlbie r21
    int tested = 0;
    for (uint32_t *p = buf; p < cp; p++)
        tested |= (*p & 0xFFF8001Fu) == (0x37F80000u | T1); // tbnz w17, #31
    assert(tested);
    puts("PASS K4: tlbie keeps cells only for unchanged code translations");
}
