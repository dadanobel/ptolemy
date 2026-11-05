/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5.h"
#include "g5_fabric.h"
#include "g5_of.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sched.h>
#define OK(x) assert((x) == PT_G5_OK)
static void be(uint8_t *p, uint64_t v) {
    for (unsigned i = 0; i < 8; i++)
        p[7 - i] = (uint8_t)(v >> (i * 8));
}
static uint64_t rng = UINT64_C(0x771cbe450198caf3);
static uint64_t random64(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static uint32_t xform(unsigned op, unsigned t, unsigned a, unsigned b, bool rc) {
    return 31u << 26 | t << 21 | a << 16 | b << 11 | op << 1 | rc;
}
static uint32_t md(unsigned k, unsigned n, unsigned m, bool rc) {
    return 30u << 26 | 3 << 21 | 4 << 16 | (n & 31) << 11 | (m & 31) << 6 | (m & 32) | k << 2 | (n >> 5) << 1 | rc;
}
static uint64_t mask_ref(unsigned first, unsigned last) {
    uint64_t m = 0;
    unsigned i = first;
    for (;;) {
        m |= UINT64_C(1) << (63 - i);
        if (i == last)
            break;
        i = (i + 1) & 63;
    }
    return m;
}
static uint64_t rotate_ref(uint64_t a, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 64; i++)
        v |= ((a >> i) & 1) << ((i + n) & 63);
    return v;
}
static void integer(void) {
    for (unsigned sf = 0; sf < 2; sf++)
        for (unsigned n = 0; n < 64; n++)
            for (unsigned m = 0; m < 64; m++) {
                uint64_t a = random64(), old = random64(), rot = rotate_ref(a, n);
                for (unsigned k = 0; k < 4; k++) {
                    PtG5Integer c = {.msr = sf ? PT_G5_MSR_SF : 0};
                    c.gpr[3] = a;
                    c.gpr[4] = old;
                    unsigned first = k == 1 ? 0 : m, last = k == 0 ? 63 : k == 1 ? m : 63 - n;
                    uint64_t mask = mask_ref(first, last), want = (rot & mask) | (k == 3 ? old & ~mask : 0);
                    OK(pt_g5_integer(&c, md(k, n, m, true)));
                    assert(c.gpr[4] == want);
                    assert(c.pc == 4 && c.cr >> 28 == pt_g5_compare(want, 0, sf, true, false));
                }
                for (unsigned k = 8; k <= 9; k++) {
                    PtG5Integer c = {.msr = sf ? PT_G5_MSR_SF : 0};
                    c.gpr[3] = a;
                    c.gpr[5] = n;
                    uint32_t in = 30u << 26 | 3 << 21 | 4 << 16 | 5 << 11 | (m & 31) << 6 | (m & 32) | (k << 1);
                    OK(pt_g5_integer(&c, in));
                    assert(c.gpr[4] == (rot & mask_ref(k == 8 ? m : 0, k == 8 ? 63 : m)));
                }
            }
    for (unsigned n = 0; n < 128; n++)
        for (unsigned rep = 0; rep < 80; rep++) {
            uint64_t a = random64();
            PtG5Integer c = {.msr = PT_G5_MSR_SF};
            c.gpr[3] = a;
            c.gpr[5] = n;
            OK(pt_g5_integer(&c, xform(27, 3, 4, 5, false)));
            assert(c.gpr[4] == (n >= 64 ? 0 : a << n));
            OK(pt_g5_integer(&c, xform(539, 3, 4, 5, false)));
            assert(c.gpr[4] == (n >= 64 ? 0 : a >> n));
            OK(pt_g5_integer(&c, xform(794, 3, 4, 5, false)));
            uint64_t value = a;
            bool lost = false;
            for (unsigned i = 0; i < n; i++) {
                lost |= value & 1;
                value = (value >> 1) | (value & UINT64_C(0x8000000000000000));
            }
            assert(c.gpr[4] == value && ((c.xer >> 29) & 1) == ((a >> 63) && lost));
            if (n < 64) {
                uint32_t in = 31u << 26 | 3 << 21 | 4 << 16 | (n & 31) << 11 | 413 << 2 | (n >> 5) << 1;
                OK(pt_g5_integer(&c, in));
                assert(c.gpr[4] == value);
            }
        }
    for (unsigned i = 0; i < 100000; i++) {
        uint64_t a = random64(), b = random64();
        PtG5Integer c = {.msr = PT_G5_MSR_SF};
        c.gpr[4] = a;
        c.gpr[5] = b;
        OK(pt_g5_integer(&c, xform(9, 3, 4, 5, false)));
        /* Independent limb product, without a 128-bit multiply. */
        uint64_t al = (uint32_t)a, bl = (uint32_t)b, ah = a >> 32, bh = b >> 32;
        uint64_t mid = (al * bl >> 32) + (uint32_t)(al * bh) + (uint32_t)(ah * bl);
        uint64_t hi = ah * bh + (al * bh >> 32) + (ah * bl >> 32) + (mid >> 32);
        assert(c.gpr[3] == hi);
        OK(pt_g5_integer(&c, xform(73, 3, 4, 5, false)));
        assert(c.gpr[3] == hi - ((a >> 63) ? b : 0) - ((b >> 63) ? a : 0));
        OK(pt_g5_integer(&c, xform(233, 3, 4, 5, false)));
        assert(c.gpr[3] == a * b);
        for (unsigned sf = 0; sf < 2; sf++) {
            c.msr = sf ? PT_G5_MSR_SF : 0;
            c.xer = 0;
            OK(pt_g5_integer(&c, xform(10, 3, 4, 5, false)));
            assert(c.gpr[3] == a + b);
            assert(((c.xer >> 29) & 1) == (sf ? a + b < a : (uint64_t)(uint32_t)a + (uint32_t)b > UINT32_MAX));
        }
    }
    assert(pt_g5_compare(UINT64_C(0x8000000000000000), UINT64_MAX, true, true, false) == 8);
    assert(pt_g5_compare(UINT64_MAX, 0, true, false, true) == 5);
    PtG5Integer c = {.msr = PT_G5_MSR_SF};
    c.gpr[4] = UINT64_C(0x8000000000000000);
    c.gpr[5] = UINT64_MAX;
    OK(pt_g5_integer(&c, xform(489 | 512, 3, 4, 5, true)));
    assert((c.xer >> 30) == 3);
    c.gpr[4] = 7;
    c.gpr[5] = 2;
    OK(pt_g5_integer(&c, xform(489 | 512, 3, 4, 5, true)));
    assert(c.gpr[3] == 3 && c.xer >> 30 == 2);
    PtG5Integer before = c;
    assert(pt_g5_integer(&c, 0) == PT_G5_UNSUPPORTED);
    assert(!memcmp(&c, &before, sizeof c));
    puts("PASS integer: rotate/mask exhaustive, shifts, wide multiply, carry, CR, divide, refusal");
}
static void layout(void) {
    PtG5Layout l;
    for (unsigned gib = 2; gib <= 32; gib *= 2) {
        /* Synthetic board map, not an asserted Apple U4 address map. */
        PtG5Range r[2] = {{0, 2ull << 30}, {4ull << 30, (uint64_t)(gib - 2) << 30}}, io = {2ull << 30, 2ull << 30};
        OK(pt_g5_layout(&l, r, gib == 2 ? 1 : 2, &io, 1, 64ull << 20, 40));
        assert(l.engine.base >= (gib == 2 ? 4ull << 30 : ((uint64_t)gib + 2) << 30));
        if (gib >= 8)
            assert(l.engine.base != (8ull << 30));
    }
    uint32_t dma = 123;
    assert(!pt_g5_dma32(1ull << 32, 1, &dma) && dma == 123);
    assert(pt_g5_dma32(UINT32_MAX, 1, &dma) && dma == UINT32_MAX);
    assert(!pt_g5_dma32(UINT32_MAX, 2, &dma));
    PtG5Range bad[] = {{0, 1ull << 32}, {8ull << 30, 4ull << 30}}, io = {3ull << 30, 1ull << 30};
    assert(pt_g5_layout(&l, bad, 2, &io, 1, 64ull << 20, 36) == PT_G5_BAD_CONFIG);
    bad[0] = (PtG5Range){0, 1ull << 42};
    assert(pt_g5_layout(&l, bad, 1, NULL, 0, 16384, 40) == PT_G5_BAD_CONFIG);
    uint8_t reg[256];
    memset(reg, 0xa5, sizeof reg);
    size_t used = 0;
    PtG5Range region = {4ull << 30, 16ull << 30};
    OK(pt_g5_of_memory(&region, 1, 1, NULL, 0, &used));
    assert(used == 60);
    assert(pt_g5_of_memory(&region, 1, 1, reg, used - 1, &used) == PT_G5_BAD_CONFIG && reg[0] == 0xa5);
    OK(pt_g5_of_memory(&region, 1, 1, reg, sizeof reg, &used));
    uint64_t end = region.base, sum = 0;
    for (size_t at = 0; at < used; at += 12) {
        uint64_t addr = 0;
        uint32_t count = 0;
        for (unsigned j = 0; j < 8; j++)
            addr = addr << 8 | reg[at + j];
        for (unsigned j = 8; j < 12; j++)
            count = count << 8 | reg[at + j];
        assert(addr == end && count);
        end += count;
        sum += count;
    }
    assert(sum == region.size);
    OK(pt_g5_of_memory(&region, 1, 2, reg, sizeof reg, &used));
    assert(used == 16 && reg[11] == 4);
    puts("PASS layout: 2/4/8/16/32 GiB plans, engine separation, overlap, DMA32 overflow");
}
static void pte_install(PtG5Memory *m, uint64_t where, uint64_t top, uint64_t low) {
    uint8_t p[16];
    be(p, top);
    be(p + 8, low);
    OK(pt_g5_write(m, where, p, 16));
}
static void mmu(void) {
    uint8_t *table = calloc(1, 1 << 18);
    assert(table);
    uint64_t tb = 8ull << 30;
    PtG5Bank bank = {{tb, 1 << 18}, table, false};
    PtG5Memory mem;
    OK(pt_g5_memory_init(&mem, &bank, 1));
    PtG5Mmu m = {.sdr1 = tb};
    uint64_t vsid = (1ull << 36) | 0x1234, ea = 0xfedc000012345678ull;
    uint64_t rs = (vsid << 12) | PT_G5_SLB_KP;
    uint64_t rb = (ea & ~0xfffffffull) | (1u << 27) | 3;
    OK(pt_g5_slbmte(&m, rs, rb, true));
    uint64_t back;
    OK(pt_g5_slbmfe(&m, 3, true, &back));
    assert(back == rs);
    PtG5Mmu old = m;
    assert(pt_g5_slbmte(&m, rs, rb + 64, true) == PT_G5_BAD_CONFIG);
    assert(!memcmp(&m, &old, sizeof m));
    uint64_t group = tb + (((vsid ^ ((ea >> 12) & 0xffff)) & 0x7ff) << 7), avpn = (vsid << 5) | ((ea >> 23) & 31);
    uint64_t physical = 16ull << 30;
    pte_install(&mem, group + 112, (avpn << 7) | 1, physical | 2 | 0x40);
    PtG5Translation t;
    uint64_t msr = PT_G5_MSR_SF | PT_G5_MSR_DR | PT_G5_MSR_IR;
    OK(pt_g5_translate(&m, &mem, ea, msr, PT_G5_STORE, &t));
    assert(t.pa == physical + (ea & 4095) && t.page_size == 4096 && t.wimg == 2 && t.writable);
    uint64_t p[2];
    OK(pt_g5_pte_read(&mem, group + 112, p));
    assert((p[1] & 0x1d0) == 0x190);
    pte_install(&mem, group + 112, (avpn << 7) | 1, physical | 2);
    OK(pt_g5_translate(&m, &mem, ea, msr, PT_G5_LOAD, &t));
    assert(!t.writable);
    OK(pt_g5_pte_read(&mem, group + 112, p));
    assert((p[1] & 0x180) == 0x100);
    OK(pt_g5_translate(&m, &mem, ea, msr, PT_G5_STORE, &t));
    assert(t.writable);
    OK(pt_g5_pte_read(&mem, group + 112, p));
    assert((p[1] & 0x180) == 0x180);
    uint64_t stale[2] = {p[0], p[1] ^ 0x80};
    assert(pt_g5_pte_commit(&mem, group + 112, stale, 0) == PT_G5_RETRY);
    for (unsigned key = 0; key < 2; key++)
        for (unsigned pp = 0; pp < 4; pp++)
            for (unsigned st = 0; st < 2; st++) {
                m.slb[3].flags = key ? PT_G5_SLB_KP : 0;
                pte_install(&mem, group + 112, (avpn << 7) | 1, physical | pp);
                bool read = !(key && !pp), write = read && pp != 3 && !(key && pp == 1);
                PtG5Result r = pt_g5_translate(&m, &mem, ea, msr | PT_G5_MSR_PR, st ? PT_G5_STORE : PT_G5_LOAD, &t);
                assert(r == ((st ? write : read) ? PT_G5_OK : PT_G5_PROTECTION));
            }
    pte_install(&mem, group + 112, (avpn << 7) | 1, physical | 2 | 8);
    assert(pt_g5_translate(&m, &mem, ea, msr, PT_G5_FETCH, &t) == PT_G5_PROTECTION);
    memset(table, 0, 1 << 18);
    m.slb[3].flags = PT_G5_SLB_L;
    group = tb + (((~(vsid ^ ((ea >> 24) & 15))) & 0x7ff) << 7);
    avpn = (vsid << 5) | ((ea >> 23) & 30);
    pte_install(&mem, group, (avpn << 7) | 3, physical | 2);
    OK(pt_g5_translate(&m, &mem, ea, msr, PT_G5_LOAD, &t));
    assert(t.page_size == (1 << 24) && t.pa == physical + (ea & 0xffffff));
    m.slb[0] = m.slb[3];
    m.slb[0].esid = 0;
    pt_g5_slbia(&m);
    assert(m.slb[0].valid && !m.slb[3].valid);
    assert(pt_g5_translate(&m, &mem, ea, msr, PT_G5_LOAD, &t) == PT_G5_SEGMENT);
    pt_g5_slbie(&m, 0);
    assert(!m.slb[0].valid);
    OK(pt_g5_translate(&m, &mem, 16ull << 30, PT_G5_MSR_SF, PT_G5_LOAD, &t));
    assert(t.pa == (16ull << 30));
    OK(pt_g5_translate(&m, &mem, 16ull << 30, 0, PT_G5_LOAD, &t));
    assert(t.pa == 0);
    assert(pt_g5_translate(&m, &mem, 0, 1, PT_G5_LOAD, &t) == PT_G5_UNSUPPORTED);
    assert(pt_g5_dcbz_size(0, 0) == 128 && pt_g5_dcbz_size(128, 0) == 32 && pt_g5_dcbz_size(64, 0) == 0 &&
           pt_g5_dcbz_size(64, 1u << 21) == 128);
    PtG5Mmu bridge = {0};
    uint32_t sr = 0x70543210, copy;
    OK(pt_g5_mtsr(&bridge, 7, sr));
    OK(pt_g5_mfsr(&bridge, 7, &copy));
    assert(copy == sr);
    assert(bridge.slb[7].esid == 7 && bridge.slb[7].vsid == 0x543210);
    pt_g5_memory_destroy(&mem);
    free(table);
    puts("PASS MMU: high PA/VSID, primary/secondary hash, 4K/16M, keys, RC compare-update, SLB, dcbz");
}
typedef struct {
    PtG5Memory *mem;
    unsigned cpu;
} Counter;
static void *counter(void *arg) {
    Counter *c = arg;
    for (unsigned i = 0; i < 15000; i++) {
        bool done = false;
        while (!done) {
            uint64_t value;
            OK(pt_g5_reserve(c->mem, c->cpu, 16ull << 30, 8, &value));
            OK(pt_g5_conditional(c->mem, c->cpu, 16ull << 30, 8, value + 1, &done));
        }
    }
    return NULL;
}
static void reservations(void) {
    uint8_t data[4096] = {0};
    PtG5Memory m;
    uint64_t base = 16ull << 30, v;
    bool done;
    PtG5Bank bank = {{base, sizeof data}, data, false};
    OK(pt_g5_memory_init(&m, &bank, 1));
    for (unsigned off = 0; off < 128; off++) {
        OK(pt_g5_reserve(&m, 0, base, 8, &v));
        uint8_t same = data[off];
        OK(pt_g5_write(&m, base + off, &same, 1));
        OK(pt_g5_conditional(&m, 0, base, 8, 1, &done));
        assert(!done);
    }
    OK(pt_g5_reserve(&m, 0, base, 8, &v));
    uint8_t x = 1, zero = 0;
    OK(pt_g5_write(&m, base, &x, 1));
    OK(pt_g5_write(&m, base, &zero, 1));
    OK(pt_g5_conditional(&m, 0, base, 8, 1, &done));
    assert(!done);
    OK(pt_g5_reserve(&m, 0, base, 8, &v));
    OK(pt_g5_write(&m, base + 128, &x, 1));
    OK(pt_g5_conditional(&m, 0, base, 8, 0, &done));
    assert(done);
    assert(pt_g5_reserve(&m, 0, base + 1, 8, &v) == PT_G5_ALIGNMENT);
    pthread_t threads[4];
    Counter c[4];
    for (unsigned i = 0; i < 4; i++) {
        c[i] = (Counter){&m, i};
        assert(!pthread_create(&threads[i], NULL, counter, &c[i]));
    }
    for (unsigned i = 0; i < 4; i++)
        assert(!pthread_join(threads[i], NULL));
    OK(pt_g5_reserve(&m, 0, base, 8, &v));
    assert(v == 60000);
    uint8_t copy[8];
    assert(pt_g5_read(&m, 0, copy, 8) == PT_G5_BUS);
    assert(pt_g5_write(&m, UINT64_MAX, &zero, 1) == PT_G5_BUS);
    pt_g5_memory_destroy(&m);
    PtG5Bank alias[2] = {{{base, 256}, data, false}, {{base + 4096, 256}, data + 128, false}};
    assert(pt_g5_memory_init(&m, alias, 2) == PT_G5_BAD_CONFIG);
    PtG5Bank ro = {{base, sizeof data}, data, true};
    OK(pt_g5_memory_init(&m, &ro, 1));
    uint8_t saved = data[0];
    assert(pt_g5_write(&m, base, &x, 1) == PT_G5_BUS && data[0] == saved);
    memset(copy, 0xa5, sizeof copy);
    assert(pt_g5_read(&m, base + 4092, copy, 8) == PT_G5_BUS);
    for (unsigned i = 0; i < sizeof copy; i++)
        assert(copy[i] == 0xa5);
    pt_g5_memory_destroy(&m);
    puts("PASS reservations: every byte of 128B, same-value store, ABA, neighbor control, 4 owners/60000 increments");
}
static void fabric(void) {
    PtG5Fabric f;
    OK(pt_g5_fabric_init(&f, 4, 25000000, 1000));
    PtG5Job j[4];
    for (unsigned i = 0; i < 4; i++) {
        OK(pt_g5_start(&f, i, 0x100, i));
        assert(pt_g5_take(&f, i, &j[i]));
    }
    uint64_t first = pt_g5_shootdown(&f);
    assert(!pt_g5_synced(&f, first));
    for (unsigned i = 0; i < 4; i++) {
        assert(!pt_g5_take(&f, i, &j[i]));
        OK(pt_g5_complete(&f, i, &j[i]));
    }
    for (unsigned i = 0; i < 4; i++)
        assert(pt_g5_take(&f, i, &j[i]));
    uint64_t next = pt_g5_shootdown(&f);
    for (unsigned i = 0; i < 4; i++)
        OK(pt_g5_complete(&f, i, &j[i]));
    assert(pt_g5_synced(&f, first) && !pt_g5_synced(&f, next));
    for (unsigned i = 0; i < 4; i++) {
        assert(pt_g5_take(&f, i, &j[i]));
        OK(pt_g5_complete(&f, i, &j[i]));
    }
    assert(pt_g5_synced(&f, next));
    assert(pt_g5_complete(&f, 0, &j[0]) == PT_G5_RETRY);
    OK(pt_g5_ipi(&f, 0xa, 2));
    assert(!pt_g5_take(&f, 0, &j[0]));
    for (unsigned i = 1; i < 4; i += 2) {
        assert(pt_g5_take(&f, i, &j[i]) && j[i].ipi == 4);
        OK(pt_g5_complete(&f, i, &j[i]));
    }
    assert(pt_g5_tb(&f, 3, 1000001000) == 25000000);
    OK(pt_g5_pause(&f, 1000001000, true));
    assert(pt_g5_tb(&f, 0, 9000001000) == 25000000);
    OK(pt_g5_tb_write(&f, 2, 9000001000, 123));
    assert(pt_g5_tb(&f, 2, 9000001000) == 123);
    uint64_t oscillator;
    OK(pt_g5_clock_ticks(&f, 9000001000, &oscillator));
    assert(oscillator == 25000000);
    OK(pt_g5_pause(&f, 9000001000, false));
    assert(pt_g5_tb(&f, 2, 10000001000) == 25000123);
    assert(pt_g5_fabric_reset(&f, 0) == PT_G5_BAD_CONFIG);
    for (unsigned i = 0; i < 4; i++) {
        OK(pt_g5_stop(&f, i));
        assert(pt_g5_stopped(&f, i) == PT_G5_RETRY);
        assert(pt_g5_take(&f, i, &j[i]));
        assert(pt_g5_stopped(&f, i) == PT_G5_RETRY);
        OK(pt_g5_complete(&f, i, &j[i]));
        assert(pt_g5_stopped(&f, i) == PT_G5_OK);
    }
    OK(pt_g5_fabric_reset(&f, 0));
    assert(pt_g5_complete(&f, 0, &j[0]) == PT_G5_RETRY);
    assert(pt_g5_wait(&f, 0, &j[0], 1) == PT_G5_RETRY);
    for (unsigned k = 0; k < 4; k++) {
        OK(pt_g5_pause(&f, k * 20 + 10, true));
        OK(pt_g5_pause(&f, k * 20 + 20, false));
    }
    assert(pt_g5_tb(&f, 0, 80) == 1);
    OK(pt_g5_start(&f, 0, 0x100, 0));
    assert(pt_g5_take(&f, 0, &j[0]));
    OK(pt_g5_complete(&f, 0, &j[0]));
    OK(pt_g5_stop(&f, 0));
    assert(pt_g5_take(&f, 0, &j[0]));
    next = pt_g5_shootdown(&f);
    OK(pt_g5_ipi(&f, 1, 0));
    OK(pt_g5_complete(&f, 0, &j[0]));
    assert(pt_g5_synced(&f, next));
    assert(!pt_g5_take(&f, 0, &j[0]));
    OK(pt_g5_start(&f, 0, 0x100, 0));
    assert(pt_g5_take(&f, 0, &j[0]));
    assert(j[0].flags == PT_G5_JOB_START);
    OK(pt_g5_complete(&f, 0, &j[0]));
    OK(pt_g5_stop(&f, 0));
    assert(pt_g5_take(&f, 0, &j[0]));
    OK(pt_g5_complete(&f, 0, &j[0]));
    pt_g5_fabric_destroy(&f);
    puts("PASS fabric: start, coalesced TLB barriers, delayed/duplicate/stale acknowledgments, IPI, pause/timebase, stop/reset");
}
int main(void) {
    integer();
    layout();
    mmu();
    reservations();
    fabric();
    return 0;
}
