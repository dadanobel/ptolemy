/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_CPU_H
#define PT_G5_CPU_H
#include "g5.h"
#include "g5_dec.h"

#define PT_G5_MSR_HV (UINT64_C(1) << 60)
#define PT_G5_MSR_VEC (UINT64_C(1) << 25)
#define PT_G5_MSR_POW (UINT64_C(1) << 18)
#define PT_G5_MSR_EE UINT64_C(0x8000)
#define PT_G5_MSR_FP UINT64_C(0x2000)
#define PT_G5_MSR_ME UINT64_C(0x1000)
#define PT_G5_MSR_RI UINT64_C(2)
#define PT_G5_MSR_MASK (PT_G5_MSR_SF | PT_G5_MSR_HV | PT_G5_MSR_VEC | PT_G5_MSR_POW | UINT64_C(0xff36))

/* Canonical recovery state. No pointer or effective address is narrowed to G4's
 * CPU ABI. One owner writes this structure; a controller snapshots it only stopped. */
typedef struct {
    PtG5Integer r;
    uint64_t fpr[32];
    uint8_t vr[32][16];
    uint32_t fpscr, vscr, vrsave;
    uint64_t srr0, srr1, dar, hior, sprg[4], hid[4], tb_offset;
    uint32_t dsisr, pvr, pir;
    PtG5Mmu mmu;
    PtG5Decrementer dec;
    uint64_t tlb_ticket, retired;
    uint32_t last_instruction, exception;
    bool checkstop;
} PtG5Cpu;
typedef struct {
    PtG5Memory *memory;
    void *user;
    /* One machine-wide virtual oscillator. TB writes never change this clock. */
    uint64_t (*ticks)(void *, unsigned cpu);
    /* Return RETRY until all owners applied the ticket. The issuer must not wait
     * synchronously here: it must return to its owner loop to apply its own job. */
    PtG5Result (*invalidate)(void *, unsigned, uint64_t *);
    PtG5Result (*synced)(void *, uint64_t);
    /* Optional RAM-miss device access. Never called for reservation operations.
     * Bytes are in guest order. The callback must reject unsupported widths. */
    PtG5Result (*mmio)(void *, unsigned, uint64_t, void *, size_t, bool);
} PtG5CpuBus;

void pt_g5_cpu_init(PtG5Cpu *, unsigned, uint64_t, uint64_t, uint64_t);
/* A completed guest exception is OK with cpu.exception set. UNSUPPORTED means
 * missing implementation, not an invented illegal-instruction exception. */
PtG5Result pt_g5_cpu_step(PtG5Cpu *, const PtG5CpuBus *, bool external_irq);
PtG5Result pt_g5_cpu_execute(PtG5Cpu *, const PtG5CpuBus *, uint32_t);
PtG5Result pt_g5_cpu_exception(PtG5Cpu *, unsigned, uint64_t, uint32_t);
PtG5Result pt_g5_cpu_access(PtG5Cpu *, const PtG5CpuBus *, uint64_t, void *, unsigned, PtG5Access);
#endif
