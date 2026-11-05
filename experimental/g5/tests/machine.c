/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_machine.h"
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#define OK(x) assert((x) == PT_G5_OK)
static _Atomic uint64_t virtual_ticks;
static uint64_t ticks(void *p) {
    (void)p;
    return atomic_load(&virtual_ticks);
}
static uint32_t x(unsigned xo, unsigned t, unsigned a, unsigned b) {
    return 31u << 26 | t << 21 | a << 16 | b << 11 | xo << 1;
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 4; i; i--) {
        p[i - 1] = (uint8_t)v;
        v >>= 8;
    }
}
static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++)
        v = v << 8 | p[i];
    return v;
}
static uint64_t read64(PtG5Machine *m, uint64_t pa) {
    uint8_t b[8];
    OK(pt_g5_machine_dma_read(m, pa, b, 8));
    return get64(b);
}
static void wait_done(PtG5Machine *m, unsigned cpus, uint64_t high) {
    for (unsigned attempt = 0; attempt < 10000; attempt++) {
        unsigned done = 0;
        for (unsigned c = 0; c < cpus; c++)
            done += !!read64(m, high + 128 + 8 * (c + 1));
        if (done == cpus)
            return;
        usleep(1000);
    }
    assert(!"G5 owners did not complete the PPC workload");
}
int main(void) {
    alarm(30);
    const uint64_t high = UINT64_C(0x200000000);
    const unsigned iterations = 1000;
    uint32_t code[] = {14u << 26 | 4u << 21 | iterations,
                       x(306, 0, 0, 0),
                       x(566, 0, 0, 0),
                       x(84, 5, 0, 3),
                       14u << 26 | 5u << 21 | 5u << 16 | 1,
                       x(214, 5, 0, 3) | 1,
                       0x4082fff4,
                       14u << 26 | 4u << 21 | 4u << 16 | 0xffff,
                       11u << 26 | 1u << 21 | 4u << 16,
                       0x4082ffe8,
                       x(339, 6, 31, 31),
                       14u << 26 | 6u << 21 | 6u << 16 | 1,
                       x(266, 6, 6, 6),
                       x(266, 6, 6, 6),
                       x(266, 6, 6, 6),
                       14u << 26 | 6u << 21 | 6u << 16 | 128,
                       x(266, 7, 3, 6),
                       62u << 26 | 5u << 21 | 7u << 16,
                       0x48000000};
    for (unsigned count = 1; count <= 4; count *= 2) {
        uint8_t lo[16384] = {0}, hi[4096] = {0};
        for (unsigned i = 0; i < sizeof code / sizeof *code; i++)
            put32(lo + 0x1000 + 4 * i, code[i]);
        PtG5MachineConfig cfg = {.cpus = count, .boot_msr = PT_G5_MSR_SF | PT_G5_MSR_ME, .banks = 2, .ticks = ticks};
        cfg.bank[0] = (PtG5Bank){{0, sizeof lo}, lo, false};
        cfg.bank[1] = (PtG5Bank){{high, sizeof hi}, hi, false};
        PtG5Machine *m;
        OK(pt_g5_machine_open(&m, &cfg));
        for (unsigned c = 0; c < count; c++)
            OK(pt_g5_machine_start(m, c, 0x1000, high));
        wait_done(m, count, high);
        assert(read64(m, high) == (uint64_t)count * iterations);
        for (unsigned c = 0; c < count; c++) {
            OK(pt_g5_machine_stop(m, c));
            PtG5Cpu cpu;
            PtG5Result fault;
            OK(pt_g5_machine_snapshot(m, c, &cpu, &fault));
            assert(fault == PT_G5_OK && cpu.r.gpr[4] == 0 && cpu.pir == c && cpu.retired > 6000);
        }
        /* Immediate restart after STOP must not race the coordinator's acknowledgment. */
        uint8_t zero[8] = {0};
        OK(pt_g5_machine_dma_write(m, high + 136, zero, 8));
        OK(pt_g5_machine_start(m, 0, 0x1000, high));
        wait_done(m, 1, high);
        OK(pt_g5_machine_stop(m, 0));
        assert(read64(m, high) == (uint64_t)(count + 1) * iterations);
        OK(pt_g5_machine_close(m));
        printf("PASS G5 machine: %u persistent owners execute PPC reservation loop above 4 GiB; global tlbie/tlbsync; stop/restart\n",
               count);
    }
    {
        uint8_t bytes[4096] = {0};
        PtG5MachineConfig cfg = {.cpus = 4, .banks = 1, .ticks = ticks};
        cfg.bank[0] = (PtG5Bank){{0, sizeof bytes}, bytes, false};
        PtG5Machine *m;
        OK(pt_g5_machine_open(&m, &cfg));
        atomic_store(&virtual_ticks, 10);
        OK(pt_g5_machine_timebase_enable(m, 1, false));
        atomic_store(&virtual_ticks, 20);
        uint64_t a, b;
        OK(pt_g5_machine_clock(m, 0, &a));
        OK(pt_g5_machine_clock(m, 1, &b));
        assert(a == 20 && b == 10);
        OK(pt_g5_machine_timebase_enable(m, 1, false));
        OK(pt_g5_machine_timebase_enable(m, 1, true));
        atomic_store(&virtual_ticks, 25);
        OK(pt_g5_machine_clock(m, 0, &a));
        OK(pt_g5_machine_clock(m, 1, &b));
        assert(a == 25 && b == 15);
        OK(pt_g5_machine_close(m));
        puts("PASS G5 TBEN: per-core TB/DEC oscillator gating, common source, idempotent enable, no retroactive ticks");
    }
    /* A missing opcode must report a stopped failure, never execute as a NOP. */
    uint8_t unknown[4096] = {0};
    put32(unknown + 0x100, 14u << 26 | 4u << 21 | 1);
    put32(unknown + 0x104, 62u << 26 | 4u << 21 | 3u << 16);
    put32(unknown + 0x108, 0x48000000);
    PtG5MachineConfig cfg = {.cpus = 4, .banks = 1, .ticks = ticks, .boot_msr = PT_G5_MSR_SF};
    cfg.bank[0] = (PtG5Bank){{0, sizeof unknown}, unknown, false};
    PtG5Machine *m;
    OK(pt_g5_machine_open(&m, &cfg));
    for (unsigned c = 1; c < 4; c++)
        OK(pt_g5_machine_start(m, c, 0x100, 0x200 + 8 * c));
    for (unsigned c = 1; c < 4; c++) {
        for (unsigned retry = 0; retry < 10000 && !read64(m, 0x200 + 8 * c); retry++)
            usleep(1000);
        assert(read64(m, 0x200 + 8 * c) == 1);
    }
    OK(pt_g5_machine_start(m, 0, 0, 0));
    PtG5Cpu cpu;
    PtG5Result fault = PT_G5_OK;
    for (unsigned retry = 0; retry < 10000; retry++) {
        PtG5Result r = pt_g5_machine_snapshot(m, 0, &cpu, &fault);
        if (r == PT_G5_OK && fault == PT_G5_UNSUPPORTED)
            break;
        usleep(1000);
    }
    assert(fault == PT_G5_UNSUPPORTED && cpu.r.pc == 0 && cpu.last_instruction == 0);
    for (unsigned c = 1; c < 4; c++) {
        PtG5Result r = PT_G5_RETRY;
        for (unsigned retry = 0; retry < 10000 && r == PT_G5_RETRY; retry++) {
            r = pt_g5_machine_snapshot(m, c, &cpu, &fault);
            if (r == PT_G5_RETRY)
                usleep(1000);
        }
        assert(r == PT_G5_OK && fault == PT_G5_OK && cpu.r.pc == 0x108);
    }
    assert(pt_g5_machine_close(m) == PT_G5_UNSUPPORTED);
    puts("PASS G5 machine: unsupported instruction stops the owner team with exact PC/opcode");
    alarm(0);
}
