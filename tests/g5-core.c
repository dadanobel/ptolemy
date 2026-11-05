/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ppc.h"
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
#include "../src/agent_guest.c"
#define HI UINT64_C(0x200000000)
#define HT UINT64_C(0x100000000)
int sys_call(CPU *c) {
    (void)c;
    abort();
}
static uint64_t now;
static uint64_t ticks(CPU *c) {
    (void)c;
    return now;
}
static unsigned external;
static uint32_t pending(CPU *c, uint32_t ack) {
    (void)c;
    (void)ack;
    return external;
}
static uint32_t x(unsigned op, unsigned d, unsigned a, unsigned b) {
    return 31u << 26 | d << 21 | a << 16 | b << 11 | op << 1;
}
static uint32_t mfs(unsigned d, unsigned spr) {
    return x(339, d, spr & 31, spr >> 5);
}
static uint32_t mts(unsigned d, unsigned spr) {
    return x(467, d, spr & 31, spr >> 5);
}
static void ready(CPU *c, int sf) {
    c->halted = 0;
    c->exit_code = 0;
    c->exception970 = 0;
    c->msr = MSR_ME | MSR_FP | MSR_VEC;
    c->msr_hi = sf ? 0x90000000 : 0x10000000;
    ppc_set_pc64(c, 0x100);
    ppc_set_spr64(c, 311, 0);
}
static void exec(CPU *c, uint32_t in) {
    assert(!ppc970_execute(c, in));
    assert(!c->halted && !c->exception970);
}
static uint64_t read64(Pt970Bus *b, uint64_t a) {
    uint8_t p[8];
    assert(!ppc970_bus_read(b, a, p, 8));
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}
static void write64(Pt970Bus *b, uint64_t a, uint64_t v) {
    uint8_t p[8];
    wbe64(p, v);
    assert(!ppc970_bus_write(b, a, p, 8));
}
static uint64_t pte(CPU *c, Pt970Bus *bus, uint64_t ea, uint64_t pa, unsigned flags) {
    uint64_t vsid = 0x12345, index = ea >> 28;
    c->slb[0].esid = index | UINT64_C(1) << 63;
    c->slb[0].vsid = vsid << 12;
    ppc_set_spr64(c, SPR_SDR1, HT);
    uint64_t slot = HT + (((vsid ^ (ea >> 12 & 65535)) & 2047) << 7);
    write64(bus, slot, ((vsid << 5 | (ea >> 23 & 31)) << 7) | 1);
    write64(bus, slot + 8, pa | flags);
    return slot;
}
static void memory(CPU *c, Pt970Bus *bus) {
    ready(c, 1);
    ppc_set_gpr64(c, 3, HI);
    ppc_set_gpr64(c, 4, UINT64_C(0x1122334455667788));
    exec(c, 62u << 26 | 4u << 21 | 3u << 16);
    exec(c, 58u << 26 | 5u << 21 | 3u << 16);
    assert(ppc_gpr64(c, 5) == UINT64_C(0x1122334455667788));
    assert(read64(bus, HI) == ppc_gpr64(c, 5));
    ready(c, 0);
    ppc_set_gpr64(c, 3, UINT64_C(0x1234567800001000));
    ppc_set_gpr64(c, 4, 9);
    exec(c, 37u << 26 | 4u << 21 | 3u << 16 | 4);
    assert(ppc_gpr64(c, 3) == 0x1004);
    ready(c, 1);
    uint64_t ea = UINT64_C(0x1234567800001000), slot = pte(c, bus, ea, HI, 0x42);
    c->msr |= MSR_DR;
    ppc_set_gpr64(c, 3, ea);
    exec(c, 58u << 26 | 5u << 21 | 3u << 16);
    assert(ppc_gpr64(c, 5) == UINT64_C(0x1122334455667788));
    assert((read64(bus, slot + 8) & 0x1f8) == 0x140); /* R update preserves W before the first store. */
    exec(c, 62u << 26 | 4u << 21 | 3u << 16);
    assert((read64(bus, slot + 8) & 0x1f8) == 0x190); /* C update clears W and sets M. */
    c->msr = MSR_PR | MSR_ME;
    uint64_t value;
    assert(!ppc970_ld(c, ea, 8, &value)); /* PR forces translation. */
    c->slb[0].esid = UINT64_C(1) << 63;
    pte(c, bus, 0x8000, HI, 2);
    uint32_t small = 0xabcdef;
    int writable;
    assert(pt_xlate(c, 0x8000, 0, 0, &small, &writable) && small == 0xabcdef);
    slot = pte(c, bus, ea, HI, 0x22);
    c->msr = MSR_DR | MSR_ME;
    int ok;
    assert(ppc970_reservation(c, ea, 8, 0, &value, &ok));
    assert(c->exception970 == 0x300 && (c->spr[SPR_DSISR] & 0x04000000));
    ready(c, 1);
    c->msr |= MSR_DR;
    assert(ppc970_ld(c, ea + 1, 4, &value));
    assert(c->exception970 == 0x600);
    ready(c, 1);
    c->msr |= MSR_DR;
    c->spr[SPR_DSISR] = 0x13579;
    assert(ppc970_st(c, ea + 1, 4, 0));
    assert(c->exception970 == 0x600 && c->spr[SPR_DSISR] == 0x02013579);
    ready(c, 1);
    c->msr |= MSR_DR;
    uint64_t initial = read64(bus, HI);
    ppc_set_gpr64(c, 3, ea + 3);
    ppc_set_gpr64(c, 4, 0);
    assert(!ppc970_execute(c, x(1014, 0, 3, 4)));
    assert(c->exception970 == 0x600 && ppc_spr64(c, SPR_DAR) == ea + 3 && read64(bus, HI) == initial);
    ready(c, 1);
    c->msr |= MSR_DR;
    ppc_set_gpr64(c, 3, ea);
    assert(!ppc970_execute(c, 46u << 26 | 31u << 21 | 3u << 16));
    assert(c->exception970 == 0x600);
    ready(c, 1);
    c->msr |= MSR_DR;
    assert(!ppc970_execute(c, x(597, 31, 3, 4)));
    assert(c->exception970 == 0x600);
    pte(c, bus, ea, HI, 2);
    ready(c, 1);
    c->msr |= MSR_DR;
    ppc_set_gpr64(c, 3, ea + 0xffc);
    uint8_t before[8], after[8];
    assert(!ppc970_bus_read(bus, HI + 0xffc, before, 8));
    assert(ppc970_st(c, ea + 0xffc, 8, 0));
    assert(c->exception970 == 0x300);
    assert(!ppc970_bus_read(bus, HI + 0xffc, after, 8));
    assert(!memcmp(before, after, 8));
    ready(c, 1);
    c->msr |= MSR_DR;
    c->slb[0].esid = 0;
    ppc_set_gpr64(c, 3, ea);
    c->spr[SPR_DSISR] = 0x13579;
    assert(!ppc970_execute(c, 33u << 26 | 5u << 21 | 3u << 16 | 4));
    assert(c->exception970 == 0x380 && ppc_gpr64(c, 3) == ea);
    assert(ppc_spr64(c, SPR_DAR) == ea + 4 && c->spr[SPR_DSISR] == 0);
    /* A stale page/protection/store reason must not bypass XNU's SLB refill. */
    ready(c, 1);
    c->msr |= MSR_DR;
    c->slb[0].esid = 0;
    c->spr[SPR_DSISR] = UINT32_MAX;
    assert(ppc970_st(c, ea, 4, 0));
    assert(c->exception970 == 0x380 && c->spr[SPR_DSISR] == 0 && ppc_spr64(c, SPR_DAR) == ea);
    ready(c, 1);
    assert(ppc970_ld(c, HI + 0x100000, 8, &value));
    assert(c->exception970 == 0x200);
    ready(c, 1);
    c->msr = 0;
    assert(ppc970_ld(c, HI + 0x100000, 8, &value));
    assert(c->halted && c->exit_code == 71);
    ready(c, 1);
    ppc_set_gpr64(c, 3, HI + 0x100);
    ppc_set_gpr64(c, 4, 0);
    uint8_t lanes[16];
    for (unsigned i = 0; i < 16; i++)
        lanes[i] = i;
    assert(!ppc970_bus_write(bus, HI + 0x100, lanes, 16));
    exec(c, x(103, 7, 3, 4));
    assert(!memcmp(c->vr[7], lanes, 16));
    for (unsigned i = 0; i < 16; i++)
        c->vr[7][i] ^= 0xff;
    exec(c, x(231, 7, 3, 4));
    assert(!ppc970_bus_read(bus, HI + 0x100, lanes, 16));
    assert(lanes[0] == 255 && lanes[15] == 240);
    ready(c, 1);
    ppc_set_spr64(c, 1012, UINT64_C(1) << 39);
    assert(ppc970_ld(c, HI + 1, 8, &value));
    assert(c->exception970 == 0x600);
    ppc_set_spr64(c, 1012, 0);
    ready(c, 1);
    ppc_set_gpr64(c, 3, HI + 0x403);
    ppc_set_gpr64(c, 4, 0);
    write64(bus, HI + 0x420, 0x1234);
    write64(bus, HI + 0x400, 0x5678);
    ppc_set_spr64(c, 1014, 0x80);
    exec(c, x(1014, 0, 3, 4));
    assert(!read64(bus, HI + 0x400) && read64(bus, HI + 0x420) == 0x1234);
    ppc_set_spr64(c, 1014, 0x40);
    assert(!ppc970_execute(c, x(1014, 0, 3, 4)));
    assert(c->exception970 == 0x700 && (c->spr[SPR_SRR1] & 0x80000));
    ready(c, 1);
    exec(c, x(1014, 1, 3, 4));
    assert(!read64(bus, HI + 0x420));
    ppc_set_spr64(c, 1014, 0);
    ready(c, 1);
    ppc_set_gpr64(c, 3, HI + 29);
    assert(!ppc970_execute(c, 48u << 26 | 2u << 21 | 3u << 16));
    assert(c->exception970 == 0x600);
    ready(c, 1);
    ppc_set_gpr64(c, 3, HI + 0xffd);
    assert(!ppc970_execute(c, 54u << 26 | 2u << 21 | 3u << 16));
    assert(c->exception970 == 0x600 && (c->spr[SPR_DSISR] & 0x02000000));
    ready(c, 1);
    c->msr |= MSR_DR;
    c->slb[0].esid = 0;
    assert(!ppc970_execute(c, x(982, 0, 3, 4)));
    assert(c->exception970 == 0x380);
    puts("PASS 970 memory: high PA/EA, update, HPT R/C/W/M, PR, cross-page fault, DSISR, checkstop, VMX");
}
static void slb_selection(CPU *c, Pt970Bus *bus) {
    uint64_t ea = UINT64_C(0x1234567800001000), ra;
    int writable;
    for (unsigned index = 0; index < 64; index++) {
        ready(c, 1);
        memset(c->slb, 0, sizeof c->slb);
        uint64_t slot = pte(c, bus, ea, HI, 2), tag = c->slb[0].esid, vsid = c->slb[0].vsid;
        c->slb[0].esid = 0;
        c->slb[index].esid = tag;
        c->slb[index].vsid = vsid;
        assert(!pt_xlate970(c, ea, 0, 0, &ra, &writable) && ra == HI);
        /* The reference path must see a changed PTE on the very next access. */
        write64(bus, slot + 8, HI + 0x1000 | 2);
        assert(!pt_xlate970(c, ea, 0, 0, &ra, &writable) && ra == HI + 0x1000);
        unsigned other = (index + 1) & 63;
        c->slb[other] = c->slb[index];
        assert(pt_xlate970(c, ea, 0, 0, &ra, &writable) && c->halted && c->exit_code == 70);
        c->slb[other].esid = 0;
        c->slb[index].esid = tag & ~(UINT64_C(1) << 63);
        ready(c, 1);
        assert(pt_xlate970(c, ea, 0, 0, &ra, &writable) == PT970_SEGMENT && !c->halted);
    }
    memset(c->slb, 0, sizeof c->slb);
    puts("PASS 970 SLB selection: all 64 slots, immediate PTE changes, duplicate and invalid entries");
}
static void control(CPU *c, Pt970Bus *bus) {
    for (int sf = 0; sf < 2; sf++) {
        ready(c, sf);
        ppc_set_gpr64(c, 3, UINT64_C(0x123456789abcdef0));
        exec(c, mts(3, 275));
        exec(c, mfs(4, 259));
        assert(ppc_gpr64(c, 4) == ppc_gpr64(c, 3));
        c->msr |= MSR_PR;
        exec(c, mfs(4, 259));
        assert(ppc_gpr64(c, 4) == UINT64_C(0x123456789abcdef0));
        assert(!ppc970_execute(c, mfs(4, 275)));
        assert(c->exception970 == 0x700 && (c->spr[27] & 0x40000));
        ready(c, sf);
        c->msr |= MSR_PR;
        assert(!ppc970_execute(c, mts(3, 275)));
        assert(c->exception970 == 0x700 && (c->spr[27] & 0x40000));
        for (int pr = 0; pr < 2; pr++) {
            ready(c, sf);
            if (pr)
                c->msr |= MSR_PR;
            ppc_set_gpr64(c, 3, 7);
            assert(!ppc970_execute(c, mts(3, 259)));
            assert(c->exception970 == 0x700 && (c->spr[27] & 0x80000));
            assert(ppc_spr64(c, 275) == UINT64_C(0x123456789abcdef0));
        }
    }
    puts("PASS 970 USPRG3: full-width user/supervisor read alias, privileged backing and rejected writes");
    /* Firmware's HID5 contract: clearing two 32-byte slots must not erase the
     * allocator header in the preceding half of the same 128-byte cache line. */
    ready(c, 0);
    ppc_set_spr64(c, 1014, 0);
    exec(c, mfs(3, 1014));
    exec(c, 24u << 26 | 3u << 21 | 3u << 16 | 0x80);
    exec(c, mts(3, 1014));
    write64(bus, 0x4000, UINT64_C(0x7fffffffffffffff));
    write64(bus, 0x4040, UINT64_MAX);
    write64(bus, 0x4060, UINT64_MAX);
    ppc_set_gpr64(c, 3, 0x4040);
    ppc_set_gpr64(c, 4, 0);
    exec(c, x(1014, 0, 3, 4));
    ppc_set_gpr64(c, 3, 0x4060);
    exec(c, x(1014, 0, 3, 4));
    assert(read64(bus, 0x4000) == UINT64_C(0x7fffffffffffffff) && !read64(bus, 0x4040) && !read64(bus, 0x4060));
    exec(c, x(1014, 1, 3, 4));
    assert(!read64(bus, 0x4000));
    ppc_set_spr64(c, 1014, 0);
    puts("PASS 970 firmware dcbz contract: 32-byte allocator slots preserve header; dcbzl remains 128 bytes");
    ready(c, 1);
    ppc_set_gpr64(c, 3, 0x04308000);
    exec(c, mts(3, 276));
    exec(c, mfs(4, 277));
    assert(ppc_gpr64(c, 4) == 0);
    ppc_set_gpr64(c, 3, 0x8004);
    exec(c, mts(3, 277));
    ppc_set_gpr64(c, 3, 0x04300000);
    exec(c, mts(3, 276));
    exec(c, mfs(4, 276));
    assert(ppc_gpr64(c, 4) == 0 && c->biu_mode970 == 0x8004);
    ppc_set_gpr64(c, 3, 0);
    exec(c, mts(3, 277));
    ppc_set_gpr64(c, 3, 0x04308000);
    exec(c, mts(3, 276));
    exec(c, mfs(4, 277));
    assert(ppc_gpr64(c, 4) == 0x8004);
    ppc_set_gpr64(c, 3, 0x12348000);
    exec(c, mts(3, 276));
    exec(c, mfs(4, 276));
    assert(ppc_gpr64(c, 4) == 0x21);
    exec(c, mfs(4, 277));
    assert(ppc_gpr64(c, 4) == 0x8004); /* Failed reads do not invent data. */
    ppc_set_gpr64(c, 3, 0x04308000);
    exec(c, mts(3, 276));
    exec(c, mfs(4, 276));
    assert(ppc_gpr64(c, 4) == 0);
    ppc_set_gpr64(c, 3, 0x2000);
    exec(c, mts(3, 277));
    ppc_set_gpr64(c, 3, 0x04300000);
    assert(ppc970_execute(c, mts(3, 276)) && c->biu_mode970 == 0x8004); /* No false ECC control. */
    ready(c, 1);
    c->msr |= MSR_PR;
    assert(!ppc970_execute(c, mfs(3, 277)));
    assert(c->exception970 == 0x700);
    ready(c, 1);
    ppc_set_gpr64(c, 3, 0);
    exec(c, mts(3, 277));
    ppc_set_gpr64(c, 3, 0x04300000);
    exec(c, mts(3, 276));
    puts("PASS 970 SCOM: BIU mode, direction, status clearing, unknown address, privilege and unsupported controls");
    ready(c, 1);
    ppc_set_gpr64(c, 3, UINT64_C(0x123456789abcdef0));
    exec(c, mts(3, 304));
    ppc_set_gpr64(c, 3, UINT64_C(0xfedcba9876543210));
    exec(c, mts(3, 305));
    ppc970_exception(c, 0x500, ppc_pc64(c), 0);
    exec(c, 0x4c000024);
    exec(c, mfs(4, 304));
    assert(ppc_gpr64(c, 4) == UINT64_C(0x123456789abcdef0));
    exec(c, mfs(4, 305));
    assert(ppc_gpr64(c, 4) == UINT64_C(0xfedcba9876543210));
    c->msr |= MSR_PR;
    assert(!ppc970_execute(c, mfs(4, 304)));
    assert(c->exception970 == 0x700);
    ready(c, 1);
    c->msr |= MSR_PR;
    assert(!ppc970_execute(c, mts(3, 305)));
    assert(c->exception970 == 0x700 && ppc_spr64(c, 305) == UINT64_C(0xfedcba9876543210));
    puts("PASS 970 HSPRG0/1: full width, independent storage, exception preservation and privilege");
    ready(c, 1);
    ppc_set_pc64(c, HI + 0x1230);
    ppc_set_spr64(c, 311, 0xfff00000);
    uint64_t saved = ppc_msr64(c);
    ppc970_exception(c, 0x500, ppc_pc64(c), 0);
    assert(ppc_spr64(c, SPR_SRR0) == HI + 0x1230 && ppc_pc64(c) == 0xfff00500);
    assert(ppc_msr64(c) == (PT970_SF | PT970_HV | MSR_ME));
    assert(ppc_spr64(c, SPR_SRR1) == saved);
    exec(c, 0x4c000024);
    assert(ppc_pc64(c) == HI + 0x1230 && ppc_msr64(c) == saved);
    ready(c, 1);
    c->msr |= MSR_PR;
    assert(!ppc970_execute(c, mfs(3, 25)));
    assert(c->exception970 == 0x700 && (c->spr[27] & 0x40000));
    ready(c, 0);
    ppc_set_gpr64(c, 3, 0x80000000);
    exec(c, x(266, 3, 3, 3) | 1);
    assert(ppc_gpr64(c, 3) == UINT64_C(0x100000000) && cr_field(c, 0) == 2);
    ready(c, 0);
    ppc_set_gpr64(c, 3, 0x40000400);
    exec(c, x(242, 3, 0, 0));
    exec(c, x(595, 4, 0, 0));
    assert(ppc_gpr64(c, 4) == 0x40000400);
    ready(c, 1);
    ppc_set_gpr64(c, 3, 0x1000);
    int ok;
    uint64_t v;
    assert(!ppc970_reservation(c, 0x1000, 8, 0, &v, &ok));
    ppc970_exception(c, 0x500, 0x100, 0);
    v = 7;
    assert(!ppc970_reservation(c, 0x1000, 8, 1, &v, &ok) && ok);
    assert(!ppc970_reservation(c, 0x1000, 8, 0, &v, &ok));
    write64(bus, 0x1078, 0);
    v = 9;
    assert(!ppc970_reservation(c, 0x1000, 8, 1, &v, &ok) && !ok);
    ready(c, 1);
    ppc_set_gpr64(c, 3, UINT64_MAX);
    exec(c, mts(3, 311));
    assert(ppc_spr64(c, 311) == UINT64_C(0x3fffff00000));
    ready(c, 1);
    c->env_irq = pending;
    now = 1;
    ppc_set_gpr64(c, 3, 0);
    exec(c, mts(3, 22));
    now = 2;
    c->msr |= MSR_EE;
    external = PEND_EXT;
    ppc970_irq(c);
    assert(c->exception970 == 0x500);
    external = 0;
    exec(c, 0x4c000024);
    ppc970_irq(c);
    assert(c->exception970 == 0x900);
    exec(c, 0x4c000024);
    ppc970_irq(c);
    assert(c->exception970 == 0x900); /* Negative DEC remains pending. */
    ready(c, 1);
    ppc_set_gpr64(c, 3, 100);
    exec(c, mts(3, 22));
    now = 3;
    exec(c, mfs(4, 22));
    v = ppc_gpr64(c, 4);
    ppc_set_gpr64(c, 3, UINT64_C(0xffffffff));
    exec(c, mts(3, 284));
    exec(c, mfs(4, 22));
    assert(ppc_gpr64(c, 4) == v);
    ready(c, 1);
    assert(ppc970_execute(c, 0x00000001));
    assert(c->halted && c->unsupported970 == 0x00000001 && ppc_pc64(c) == 0x100);
    ready(c, 1);
    assert(!ppc970_execute(c, 0x4c000064));
    assert(c->exception970 == 0x700); /* rfi is illegal. */
    ready(c, 1);
    cr_set_field(c, 2, 13);
    exec(c, x(19, 3, 0, 0) | (1u << 20) | (0x20u << 12));
    assert(ppc_gpr64(c, 3) == 0x00d00000);
    ppc_set_gpr64(c, 3, 0x00a00000);
    exec(c, x(144, 3, 0, 0) | (1u << 20) | (0x20u << 12));
    assert(cr_field(c, 2) == 10);
    c->msr |= MSR_EE;
    c->exception970 = 0;
    external = PEND_DEC;
    ppc970_irq(c);
    assert(c->exception970 == 0);
    external = 0;
    puts("PASS 970 control: SF=0 firmware idiom, bridge SR, full SRR, rfid, privilege, HIOR, reservations, DEC and fail-closed");
}
static unsigned memory_calls, memory_size;
static int memory_write, memory_deny;
static uint64_t memory_address;
static int before_memory(CPU *c, uint64_t at, unsigned n, int write) {
    (void)c;
    if (at >= HT && at < HT + 0x40000)
        return 0; /* Count explicit data, not table walks. */
    memory_calls++;
    memory_address = at;
    memory_size = n;
    memory_write = write;
    return memory_deny;
}
static uint64_t hpt_slot, hpt_head;
static unsigned hpt_reads, hpt_writes;
static int hpt_publish, hpt_replace, hpt_deny;
static int before_pte(CPU *c, uint64_t at, unsigned n, int write) {
    if (at == hpt_slot && !write) {
        assert(n == 16);
        hpt_reads++;
        if (hpt_deny == 1)
            return 1;
        if (hpt_publish) {
            write64(c->bus970, hpt_slot, hpt_head);
            hpt_publish = 0;
        }
    }
    if (at == hpt_slot + 8 && write) {
        assert(n == 8);
        hpt_writes++;
        if (hpt_deny == 2)
            return 1;
        if (hpt_replace) {
            write64(c->bus970, hpt_slot + 8, HI + 0x1000 | 2);
            hpt_replace = 0;
        }
    }
    return 0;
}
static void hpt_coherence(CPU *c, Pt970Bus *bus) {
    uint64_t ea = 0x45678000, ra = 0, value;
    int writable, ok;
    ready(c, 1);
    hpt_slot = pte(c, bus, ea, HI, 2);
    hpt_head = read64(bus, hpt_slot);
    write64(bus, hpt_slot, 0);
    hpt_publish = 1;
    hpt_reads = hpt_writes = 0;
    c->env_ram970 = before_pte;
    assert(!pt_xlate970(c, ea, 0, 0, &ra, &writable) && ra == HI);
    assert(!hpt_publish && hpt_reads == 1 && hpt_writes == 1 && (read64(bus, hpt_slot + 8) & 0x100));
    /* GPU retirement can change a PTE before R/C publication: compare and retry. */
    hpt_replace = 1;
    hpt_reads = hpt_writes = 0;
    assert(!pt_xlate970(c, ea, 1, 0, &ra, &writable) && ra == HI + 0x1000 && writable);
    assert(hpt_reads == 2 && hpt_writes == 2 && (read64(bus, hpt_slot + 8) & 0x180) == 0x180);
    write64(bus, hpt_slot + 8, HI | 2);
    assert(!ppc970_reservation(c, hpt_slot + 8, 8, 0, &value, &ok));
    assert(!pt_xlate970(c, ea, 0, 0, &ra, &writable));
    assert(!ppc970_reservation(c, hpt_slot + 8, 8, 1, &value, &ok) && !ok);
    for (int deny = 1; deny <= 2; deny++) {
        ready(c, 1);
        c->msr |= MSR_DR;
        write64(bus, hpt_slot + 8, HI | 2);
        hpt_deny = deny;
        value = 0xfeed;
        assert(ppc970_ld(c, ea, 8, &value) && c->halted && c->exit_code == 74 && !c->exception970);
        assert(value == 0xfeed && read64(bus, hpt_slot + 8) == (HI | 2));
    }
    hpt_deny = 0;
    c->env_ram970 = NULL;
    ready(c, 1);
    puts("PASS 970 HPT ownership: retired read, R/C write, CAS retry, reservation invalidation and refusal");
}
static void coherence(CPU *c, Pt970Bus *bus) {
    ready(c, 1);
    c->env_ram970 = before_memory;
    memory_calls = 0;
    memory_deny = 0;
    uint64_t v;
    int ok;
    assert(!ppc970_st(c, 0x1000, 8, 0x12345678));
    assert(memory_calls == 1 && memory_address == 0x1000 && memory_size == 8 && memory_write);
    assert(!ppc970_ld(c, 0x1000, 8, &v) && v == 0x12345678);
    assert(memory_calls == 2 && !memory_write);
    uint32_t in;
    assert(!ppc970_ifetch(c, 0x1000, &in));
    assert(memory_calls == 3 && memory_size == 4 && !memory_write);
    assert(!ppc970_reservation(c, 0x1000, 8, 0, &v, &ok));
    assert(memory_calls == 4 && !memory_write);
    assert(!ppc970_reservation(c, 0x1000, 8, 1, &v, &ok) && ok);
    assert(memory_calls == 5 && memory_write);
    assert(!ppc970_zero(c, 0x1000, 128));
    assert(memory_calls == 6 && memory_size == 128 && memory_write);
    write64(bus, 0x1000, 0xfeed);
    memory_deny = 1;
    assert(ppc970_st(c, 0x1000, 8, 0) && c->halted && c->exit_code == 74 && read64(bus, 0x1000) == 0xfeed);
    ready(c, 1);
    assert(ppc970_ifetch(c, 0x1000, &in) && c->halted);
    ready(c, 1);
    assert(ppc970_reservation(c, 0x1000, 8, 0, &v, &ok) && c->halted);
    c->env_ram970 = NULL;
    memory_deny = 0;
    ready(c, 1);
    puts("PASS 970 CPU/GPU RAM boundary: load/store/fetch/reservation/zero, refusal preserves memory");
}
static void agent_pages(CPU *c, Pt970Bus *bus, uint8_t *lo) {
    ready(c, 1);
    c->msr |= MSR_DR;
    c->preg[0] = (PRegion){0, 1 << 20, lo, 0};
    c->npreg = 1;
    uint32_t physical[17];
    uint8_t bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint64_t value = 0;
    int ok;
    pte(c, bus, 0x8000, 0x1000, 2);
    pte(c, bus, 0x9000, 0x2000, 2);
    c->env_ram970 = before_memory;
    memory_calls = 0;
    memory_deny = 0;
    assert(!ppc970_reservation(c, 0x8ffc, 4, 0, &value, &ok));
    memory_calls = 0;
    assert(pages(c, 0x8ffc, 8, 1, physical) == PTA_OK && memory_calls == 2 && memory_write);
    copy_pages(c, 0x8ffc, bytes, 8, 1, physical);
    assert(!memcmp(lo + 0x1ffc, bytes, 4) && !memcmp(lo + 0x2000, bytes + 4, 4));
    assert(!ppc970_reservation(c, 0x8ffc, 4, 1, &value, &ok) && !ok);
    memory_calls = 0;
    assert(pages(c, 0x8ffc, 8, 0, physical) == PTA_OK && memory_calls == 2 && !memory_write);
    uint8_t back[8] = {0};
    copy_pages(c, 0x8ffc, back, 8, 0, physical);
    assert(!memcmp(bytes, back, 8));
    memory_deny = 1;
    assert(pages(c, 0x8ffc, 8, 1, physical) == PTA_FAULT);
    assert(!memcmp(lo + 0x1ffc, bytes, 4));
    memory_deny = 0;
    uint64_t slot = pte(c, bus, 0x9000, 0x2000, 3);
    assert(pages(c, 0x8ffc, 8, 1, physical) == PTA_FAULT);
    assert(!memcmp(lo + 0x1ffc, bytes, 4));
    write64(bus, slot, 0);
    assert(pages(c, 0x8ffc, 8, 0, physical) == PTA_FAULT);
    assert(ppc970_agent_prepare(c, 0xfffff, 2, 0));
    c->env_ram970 = NULL;
    c->npreg = 0;
    puts("PASS 970 agent: page-checked cross-page copy, GPU ownership, reservation invalidation, COW and rejection");
}
static uint64_t rng = UINT64_C(0x985ab2391);
static uint64_t random64(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static void slb_gap(CPU *c) {
    const uint64_t vsid = UINT64_C(0x1240490124c00);
    for (unsigned sf = 0; sf < 2; sf++)
        for (unsigned bit = 6; bit <= 26; bit++) {
            ready(c, sf);
            memset(c->slb, 0, sizeof c->slb);
            ppc_set_gpr64(c, 10, vsid);
            ppc_set_gpr64(c, 9, 0xe8000007u | (1u << bit));
            exec(c, x(402, 10, 0, 9));
            assert(c->slb[7].esid == (UINT64_C(1) << 63 | 14));
            assert(c->slb[7].vsid == vsid);
            ppc_set_gpr64(c, 9, 7 | (1u << bit));
            exec(c, x(915, 11, 0, 9));
            assert(ppc_gpr64(c, 11) == 0xe8000000);
            exec(c, x(851, 11, 0, 9));
            assert(ppc_gpr64(c, 11) == vsid);
        }
    ready(c, 1);
    ppc_set_gpr64(c, 9, 0xe0000007);
    assert(ppc970_execute(c, x(402, 10, 0, 9)) && c->halted); /* V=0 is not an invalidation instruction. */
    ready(c, 1);
    memset(c->slb, 0, sizeof c->slb);
    puts("PASS 970 SLB compatibility: EA gap discarded, six-bit selector/readback, fields retained, V=0 refused");
}
static void integer(CPU *c) {
    for (unsigned sf = 0; sf < 2; sf++)
        for (unsigned i = 0; i < 10000; i++) {
            ready(c, sf);
            uint64_t a = random64(), b = random64();
            ppc_set_gpr64(c, 3, a);
            ppc_set_gpr64(c, 4, b);
            exec(c, x(10, 5, 3, 4) | 1);
            assert(ppc_gpr64(c, 5) == a + b);
            assert(c->ca == (sf ? a + b < a : (uint64_t)(uint32_t)a + (uint32_t)b > UINT32_MAX));
            exec(c, x(235, 5, 3, 4));
            assert(ppc_gpr64(c, 5) == (uint64_t)((int64_t)(int32_t)a * (int32_t)b));
            exec(c, x(824, 3, 5, 7));
            assert(ppc_gpr64(c, 5) == (uint64_t)(int64_t)((int32_t)a >> 7));
            unsigned n = i & 31, mb = i >> 5 & 31, me = i >> 10 & 31;
            uint64_t mask = 0;
            unsigned bit = mb + 32;
            for (;;) {
                mask |= UINT64_C(1) << (63 - bit);
                if (bit == me + 32)
                    break;
                bit = (bit + 1) & 63;
            }
            uint32_t lo = (uint32_t)a, rot = n ? (lo << n) | (lo >> (32 - n)) : lo;
            exec(c, 21u << 26 | 3u << 21 | 5u << 16 | n << 11 | mb << 6 | me << 1);
            assert(ppc_gpr64(c, 5) == (((uint64_t)rot << 32 | rot) & mask));
        }
    puts("PASS 970 integer: 80,000 independent carry/word-product/sign/rotate comparisons in SF=0/1");
}
static void floating(CPU *c, Pt970Bus *bus) {
    ready(c, 1);
    c->fpscr = 0;
    c->fpr[2] = 4;
    exec(c, 63u << 26 | 1u << 21 | 2u << 11 | 22u << 1);
    assert(c->fpr[1] == 2);
    c->fpr[2] = 1.75;
    exec(c, 63u << 26 | 1u << 21 | 2u << 11 | 815u << 1);
    uint64_t v;
    memcpy(&v, &c->fpr[1], 8);
    assert(v == 1 && (c->fpscr & 0x20000));
    int64_t value = INT64_C(9007199254740993);
    memcpy(&c->fpr[2], &value, 8);
    c->fpscr = 2;
    exec(c, 63u << 26 | 1u << 21 | 2u << 11 | 846u << 1);
    assert(c->fpr[1] == 9007199254740994.0 && (c->fpscr & 0x60000) == 0x60000);
    c->fpr[2] = 0x1p63;
    c->fpscr = 0;
    exec(c, 63u << 26 | 1u << 21 | 2u << 11 | 814u << 1);
    memcpy(&v, &c->fpr[1], 8);
    assert(v == INT64_MAX && (c->fpscr & 0x100));
    c->fpr[2] = NAN;
    c->fpscr = 0x80;
    c->fpr[1] = 123;
    exec(c, 63u << 26 | 1u << 21 | 2u << 11 | 814u << 1);
    assert(c->fpr[1] == 123);
    for (unsigned bit = 0; bit < 32; bit++) {
        uint32_t word = 1u << bit;
        uint8_t bytes[4];
        wbe32(bytes, word);
        assert(!ppc970_bus_write(bus, HI + 0x200, bytes, 4));
        ppc_set_gpr64(c, 3, HI + 0x200);
        exec(c, 48u << 26 | 2u << 21 | 3u << 16);
        exec(c, 52u << 26 | 2u << 21 | 3u << 16 | 4);
        assert(!ppc970_bus_read(bus, HI + 0x204, bytes, 4));
        assert(be32(bytes) == word);
    }
    c->fpscr = 4;
    c->fpr[1] = 123;
    assert(ppc970_execute(c, 63u << 26 | 1u << 21 | 2u << 11 | 22u << 1));
    assert(c->fpr[1] == 123);
    ready(c, 1);
    puts("PASS 970 FP: sqrt, fctid(z), fcfid rounding/status, invalid suppression, high-address single load/store");
}
typedef struct {
    CPU *cpu;
    unsigned cycles;
} Worker;
static atomic_int io_entered, io_release, barrier_entered, barrier_done;
static unsigned barrier_xo;
static void pause_thread(void) {
    struct timespec t = {0, 1000000};
    nanosleep(&t, NULL);
}
static int slow_io(CPU *c, uint64_t pa, uint8_t *bytes, int n, int write) {
    (void)c;
    assert(pa == HI + 0x100000 && n == 4 && !write);
    atomic_store(&io_entered, 1);
    while (!atomic_load(&io_release))
        pause_thread();
    memset(bytes, 0, n);
    return 0;
}
static void *io_owner(void *p) {
    exec(p, 32u << 26 | 4u << 21 | 3u << 16);
    return NULL;
}
static void *barrier_owner(void *p) {
    atomic_store(&barrier_entered, 1);
    exec(p, x(barrier_xo, 0, 0, 0));
    atomic_store(&barrier_done, 1);
    return NULL;
}
static void quiescence(CPU **c) {
    const unsigned opcodes[] = {306, 566, 982};
    for (unsigned i = 0; i < 3; i++) {
        ready(c[0], 1);
        ready(c[1], 1);
        c[0]->env_mmio970 = slow_io;
        uint64_t ea = UINT64_C(0x1234567800001000);
        pte(c[0], c[0]->bus970, ea, HI + 0x100000, 2);
        c[0]->msr |= MSR_DR;
        ppc_set_gpr64(c[0], 3, ea);
        atomic_store(&io_entered, 0);
        atomic_store(&io_release, 0);
        atomic_store(&barrier_entered, 0);
        atomic_store(&barrier_done, 0);
        barrier_xo = opcodes[i];
        pthread_t a, b;
        assert(!pthread_create(&a, NULL, io_owner, c[0]));
        while (!atomic_load(&io_entered))
            pause_thread();
        assert(!pthread_create(&b, NULL, barrier_owner, c[1]));
        while (!atomic_load(&barrier_entered))
            pause_thread();
        for (unsigned k = 0; k < 20; k++)
            pause_thread();
        assert(!atomic_load(&barrier_done));
        atomic_store(&io_release, 1);
        assert(!pthread_join(a, NULL));
        assert(!pthread_join(b, NULL));
        assert(atomic_load(&barrier_done));
        c[0]->env_mmio970 = NULL;
    }
    puts("PASS 970 quiescence: tlbie/tlbsync/icbi drain an older in-flight translated access");
}
static void *fp_run(void *p) {
    CPU *c = p;
    fesetround(FE_DOWNWARD);
    c->fpr[2] = 1.75;
    for (unsigned i = 0; i < 10000; i++) {
        c->fpscr = c->pir970;
        exec(c, 63u << 26 | 1u << 21 | 2u << 11 | 814u << 1);
        uint64_t v;
        memcpy(&v, &c->fpr[1], 8);
        assert(v == (c->pir970 == 0 || c->pir970 == 2 ? 2u : 1u));
        assert(fegetround() == FE_DOWNWARD);
        c->fpscr = c->pir970;
        exec(c, 63u << 26 | 1u << 21 | 2u << 11 | 22u << 1);
        assert(fegetround() == FE_DOWNWARD);
    }
    fesetround(FE_TONEAREST);
    return NULL;
}
static void *run(void *p) {
    Worker *w = p;
    for (unsigned i = 0; i < w->cycles && !w->cpu->halted; i++) {
        assert(!interp_step(w->cpu));
        if (ppc_pc64(w->cpu) == 0x1028)
            return NULL;
    }
    assert(!"SMP workload did not finish");
    return NULL;
}
static void smp(CPU **c, Pt970Bus *bus) {
    uint32_t code[] = {14u << 26 | 4u << 21 | 500,
                       x(84, 5, 0, 3),
                       14u << 26 | 5u << 21 | 5u << 16 | 1,
                       x(214, 5, 0, 3) | 1,
                       0x4082fff4,
                       14u << 26 | 4u << 21 | 4u << 16 | 0xffff,
                       11u << 26 | 1u << 21 | 4u << 16,
                       0x4082ffe8,
                       x(306, 0, 0, 0),
                       x(566, 0, 0, 0),
                       0x48000000};
    uint8_t bytes[sizeof code];
    for (unsigned i = 0; i < sizeof code / 4; i++)
        wbe32(bytes + 4 * i, code[i]);
    assert(!ppc970_bus_write(bus, 0x1000, bytes, sizeof bytes));
    for (unsigned n = 1; n <= 4; n *= 2) {
        write64(bus, HI, 0);
        pthread_t thread[4];
        Worker w[4];
        for (unsigned i = 0; i < n; i++) {
            ready(c[i], 1);
            ppc_set_pc64(c[i], 0x1000);
            ppc_set_gpr64(c[i], 3, HI);
            w[i] = (Worker){c[i], 1000000};
            assert(!pthread_create(&thread[i], NULL, run, &w[i]));
        }
        for (unsigned i = 0; i < n; i++)
            assert(!pthread_join(thread[i], NULL));
        assert(read64(bus, HI) == 500 * n);
        printf("PASS 970 integrated interpreter: %u concurrent CPUs, contended reservation loop above 4 GiB\n", n);
    }
    pthread_t t[4];
    for (unsigned i = 0; i < 4; i++) {
        ready(c[i], 1);
        assert(!pthread_create(&t[i], NULL, fp_run, c[i]));
    }
    for (unsigned i = 0; i < 4; i++)
        assert(!pthread_join(t[i], NULL));
    puts("PASS 970 concurrent FP: 4 rounding modes, 80,000 operations, per-thread host rounding restored");
}
int main(void) {
    alarm(45);
    trace_unimpl = 0;
    uint8_t *lo = calloc(1, 1 << 20), *hi = calloc(1, 1 << 16), *ht = calloc(1, 1 << 18);
    assert(lo && hi && ht);
    Pt970Region r[] = {{0, 1 << 20, lo, 0}, {HI, 1 << 16, hi, 0}, {HT, 1 << 18, ht, 0}};
    Pt970Bus *bus = ppc970_bus_create(r, 3);
    assert(bus);
    CPU *c[4];
    for (unsigned i = 0; i < 4; i++) {
        c[i] = calloc(1, sizeof **c);
        assert(c[i]);
        c[i]->env_ticks970 = ticks;
        assert(!ppc970_attach(c[i], bus, i, HI));
        assert(ppc_pc64(c[i]) == HI + 0x100);
        if (!i) {
            agent_pages(c[0], bus, lo);
            hpt_coherence(c[0], bus);
            ready(c[0], 1);
            uint64_t v = 0;
            int ok;
            assert(!ppc970_reservation(c[0], 0x100, 8, 0, &v, &ok));
            assert(!ppc970_owner_write(bus, 0x100, 8));
            wbe32(lo + 0x100, 0x12345678);
            v = 9;
            assert(!ppc970_reservation(c[0], 0x100, 8, 1, &v, &ok) && !ok);
            assert(be32(lo + 0x100) == 0x12345678);
            assert(ppc970_owner_write(bus, 0x100000, 1));
            assert(ppc970_owner_write(bus, 0xfffff, 2));
        }
    }
    assert(ppc970_agent_prepare(c[0], 0x100, 8, 0));
    assert(ppc970_owner_write(bus, 0x100, 8)); /* Raw owner stores cannot become an SMP DMA bypass. */
    puts("PASS 970 owner DMA: single-owner reservation invalidation, bounds, SMP refusal");
    memory(c[0], bus);
    control(c[0], bus);
    coherence(c[0], bus);
    slb_gap(c[0]);
    slb_selection(c[0], bus);
    integer(c[0]);
    floating(c[0], bus);
    smp(c, bus);
    quiescence(c);
    ppc970_bus_destroy(bus);
    for (unsigned i = 0; i < 4; i++) {
        assert(!c[i]->bus970);
        free(c[i]);
    }
    free(lo);
    free(hi);
    free(ht);
    alarm(0);
    return 0;
}
