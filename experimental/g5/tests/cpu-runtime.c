/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_cpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OK(x) assert((x) == PT_G5_OK)
static uint64_t clock_ticks;
static bool flushed;
static unsigned device_calls;
static PtG5Result device(void *u, unsigned cpu, uint64_t pa, void *bytes, size_t n, bool write) {
    (void)u;
    (void)cpu;
    (void)pa;
    (void)bytes;
    (void)n;
    (void)write;
    device_calls++;
    return PT_G5_OK;
}
static uint64_t ticks(void *u, unsigned cpu) {
    (void)u;
    (void)cpu;
    return clock_ticks;
}
static PtG5Result shoot(void *u, unsigned cpu, uint64_t *t) {
    (void)u;
    assert(cpu == 0);
    *t = 37;
    return PT_G5_OK;
}
static PtG5Result synced(void *u, uint64_t t) {
    (void)u;
    assert(t == 37);
    return flushed ? PT_G5_OK : PT_G5_RETRY;
}
static uint32_t x(unsigned xo, unsigned t, unsigned a, unsigned b) {
    return 31u << 26 | t << 21 | a << 16 | b << 11 | xo << 1;
}
static uint32_t mtspr(unsigned n, unsigned t) {
    return x(467, t, n & 31, n >> 5);
}
static uint32_t mfspr(unsigned n, unsigned t) {
    return x(339, t, n & 31, n >> 5);
}
static void put(uint8_t *p, uint64_t v) {
    for (unsigned i = 8; i; i--) {
        p[i - 1] = (uint8_t)v;
        v >>= 8;
    }
}
static void reset(PtG5Cpu *c) {
    pt_g5_cpu_init(c, 0, 0x1000, PT_G5_MSR_SF | PT_G5_MSR_ME, clock_ticks);
}
int main(void) {
    uint8_t *lo = calloc(1, 1 << 20), *hi = calloc(1, 8192);
    assert(lo && hi);
    const uint64_t high = UINT64_C(0x200000000);
    PtG5Bank banks[] = {{{0, 1 << 20}, lo, false}, {{high, 8192}, hi, false}};
    PtG5Memory memory;
    OK(pt_g5_memory_init(&memory, banks, 2));
    PtG5CpuBus bus = {&memory, NULL, ticks, shoot, synced, NULL};
    PtG5Cpu c;
    reset(&c);
    c.r.gpr[3] = high + 4093;
    c.r.gpr[5] = UINT64_C(0xfedcba9876543210);
    OK(pt_g5_cpu_execute(&c, &bus, 62u << 26 | 5u << 21 | 3u << 16));
    OK(pt_g5_cpu_execute(&c, &bus, 58u << 26 | 6u << 21 | 3u << 16));
    assert(c.r.gpr[6] == c.r.gpr[5]);
    reset(&c);
    c.r.gpr[3] = high + 128;
    c.r.gpr[5] = 11;
    OK(pt_g5_cpu_execute(&c, &bus, x(84, 6, 0, 3)));
    OK(pt_g5_cpu_execute(&c, &bus, x(214, 5, 0, 3) | 1));
    assert(c.r.cr >> 28 == 2);
    OK(pt_g5_cpu_execute(&c, &bus, x(84, 6, 0, 3)));
    uint8_t byte = 8;
    OK(pt_g5_write(&memory, high + 200, &byte, 1));
    OK(pt_g5_cpu_execute(&c, &bus, x(214, 5, 0, 3) | 1));
    assert(c.r.cr >> 28 == 0);
    c.dsisr = 0x12345678;
    c.r.gpr[3]++;
    OK(pt_g5_cpu_execute(&c, &bus, x(84, 6, 0, 3)));
    assert(c.exception == 0x600 && c.dar == high + 129 && c.dsisr == (0x12345678 & ~0x02000000u));
    reset(&c);
    c.r.msr = PT_G5_MSR_HV | PT_G5_MSR_ME;
    c.r.gpr[3] = UINT64_C(0x100001000);
    c.r.gpr[5] = UINT64_C(0x1234567887654321);
    OK(pt_g5_cpu_execute(&c, &bus, 62u << 26 | 5u << 21 | 3u << 16));
    assert(!memcmp(lo + 0x1000, (uint8_t[]){0x12, 0x34, 0x56, 0x78, 0x87, 0x65, 0x43, 0x21}, 8));
    OK(pt_g5_cpu_execute(&c, &bus, 14u << 26 | 5u << 21 | 5u << 16 | 1));
    assert(c.r.gpr[5] == UINT64_C(0x1234567887654322));
    /* Update destination and base are not committed on a translation exception. */
    reset(&c);
    c.r.msr |= PT_G5_MSR_DR;
    c.r.gpr[3] = high;
    c.r.gpr[5] = 99;
    c.dsisr = UINT32_MAX;
    OK(pt_g5_cpu_execute(&c, &bus, 58u << 26 | 5u << 21 | 3u << 16 | 5));
    assert(c.exception == 0x380 && c.dar == high + 4 && c.r.gpr[3] == high && c.r.gpr[5] == 99 && c.srr0 == 0x1000 &&
           c.dsisr == 0);
    /* HPT data protection and no-execute must set different architectural reasons. */
    reset(&c);
    c.mmu.sdr1 = 0x40000;
    OK(pt_g5_slbmte(&c.mmu, 1u << 12, 1u << 27, true));
    uint64_t address = 0x1000, group = 0x40000; /* VSID=1 xor page=1. */
    put(lo + group, (UINT64_C(1) << 12) | 1);
    put(lo + group + 8, 0x8000 | 3);
    c.r.msr |= PT_G5_MSR_DR;
    c.r.gpr[3] = address;
    OK(pt_g5_cpu_execute(&c, &bus, 62u << 26 | 5u << 21 | 3u << 16));
    assert(c.exception == 0x300 && c.dsisr == 0x0a000000);
    reset(&c);
    c.mmu.sdr1 = 0x40000;
    OK(pt_g5_slbmte(&c.mmu, (1u << 12) | PT_G5_SLB_N, 1u << 27, true));
    c.r.msr |= PT_G5_MSR_IR;
    OK(pt_g5_cpu_step(&c, &bus, false));
    assert(c.exception == 0x400 && (c.srr1 & 0x78000000) == 0x10000000);
    /* An exception preserves a reservation and high SRR0; rfid restores width. */
    reset(&c);
    c.r.pc = high + 0x100;
    c.r.msr |= PT_G5_MSR_EE | PT_G5_MSR_RI;
    uint64_t oldmsr = c.r.msr;
    uint64_t v;
    OK(pt_g5_reserve(&memory, 0, high, 8, &v));
    OK(pt_g5_cpu_exception(&c, 0x500, c.r.pc, 0));
    assert(c.srr0 == high + 0x100 && c.srr1 == oldmsr && c.r.pc == 0x500 &&
           c.r.msr == (PT_G5_MSR_SF | PT_G5_MSR_HV | PT_G5_MSR_ME));
    bool stored;
    OK(pt_g5_conditional(&memory, 0, high, 8, 17, &stored));
    assert(stored);
    OK(pt_g5_cpu_execute(&c, &bus, 0x4c000024));
    assert(c.r.pc == high + 0x100 && c.r.msr == oldmsr);
    c.srr0 = high + 0x123;
    c.srr1 = PT_G5_MSR_PR | PT_G5_MSR_ME;
    OK(pt_g5_cpu_execute(&c, &bus, 0x4c000024));
    assert(c.r.pc == 0x120 && (c.r.msr & (PT_G5_MSR_IR | PT_G5_MSR_DR)) == (PT_G5_MSR_IR | PT_G5_MSR_DR));
    reset(&c);
    c.r.msr |= PT_G5_MSR_PR;
    OK(pt_g5_cpu_execute(&c, &bus, x(178, 3, 0, 0)));
    assert(c.exception == 0x700 && (c.srr1 & 0x40000));
    reset(&c);
    c.r.gpr[3] = PT_G5_MSR_EE;
    OK(pt_g5_cpu_execute(&c, &bus, x(178, 3, 1, 0)));
    assert((c.r.msr & (PT_G5_MSR_SF | PT_G5_MSR_ME | PT_G5_MSR_EE)) == (PT_G5_MSR_SF | PT_G5_MSR_ME | PT_G5_MSR_EE));
    reset(&c);
    OK(pt_g5_cpu_execute(&c, &bus, x(306, 0, 0, 0)));
    assert(c.tlb_ticket == 37);
    uint64_t pc = c.r.pc;
    assert(pt_g5_cpu_execute(&c, &bus, x(566, 0, 0, 0)) == PT_G5_RETRY && c.r.pc == pc);
    flushed = true;
    OK(pt_g5_cpu_execute(&c, &bus, x(566, 0, 0, 0)));
    assert(c.r.pc == pc + 4 && !c.tlb_ticket);
    reset(&c);
    c.r.gpr[3] = 0x12345678;
    OK(pt_g5_cpu_execute(&c, &bus, mtspr(285, 3)));
    clock_ticks = 3;
    OK(pt_g5_cpu_execute(&c, &bus, mfspr(268, 4)));
    assert(c.r.gpr[4] == UINT64_C(0x1234567800000003));
    uint32_t dec;
    OK(pt_g5_dec_read(&c.dec, clock_ticks, &dec));
    assert(dec == 0x7ffffffc);
    pt_g5_dec_write(&c.dec, clock_ticks, 0xffffffff);
    c.r.msr |= PT_G5_MSR_EE;
    OK(pt_g5_cpu_step(&c, &bus, true));
    assert(c.exception == 0x500);
    c.r.msr |= PT_G5_MSR_EE;
    OK(pt_g5_cpu_step(&c, &bus, false));
    assert(c.exception == 0x900);
    reset(&c);
    PtG5Integer before = c.r;
    assert(pt_g5_cpu_execute(&c, &bus, 0) == PT_G5_UNSUPPORTED);
    assert(!memcmp(&c.r, &before, sizeof before));
    reset(&c);
    bus.mmio = device;
    c.r.gpr[3] = high + 16384;
    OK(pt_g5_cpu_execute(&c, &bus, 62u << 26 | 5u << 21 | 3u << 16));
    assert(device_calls == 1 && !c.exception);
    memory.bank[0].readonly = true;
    c.r.gpr[3] = 0x1000;
    OK(pt_g5_cpu_execute(&c, &bus, 62u << 26 | 5u << 21 | 3u << 16));
    assert(device_calls == 1 && c.exception == 0x200);
    pt_g5_memory_destroy(&memory);
    free(hi);
    free(lo);
    puts("PASS G5 CPU: high addresses, SF=0/1, memory/reservations, precise faults, rfid, privilege, clock/DEC, deferred tlbsync, unknown refusal");
}
