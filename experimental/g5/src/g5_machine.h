/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_MACHINE_H
#define PT_G5_MACHINE_H
#include "g5_cpu.h"
#include "g5_pic.h"
#include "g5_owners.h"

typedef struct PtG5Machine PtG5Machine;
typedef struct {
    unsigned cpus;
    uint64_t boot_msr;
    PtG5Bank bank[8];
    unsigned banks;
    void *user;
    uint64_t (*ticks)(void *);
    PtG5Result (*mmio)(void *, unsigned, uint64_t, void *, size_t, bool);
} PtG5MachineConfig;
/* Executable reference machine for CPU/platform integration. No firmware, invented
 * board addresses, host core pinning or claimed native G5 JIT is hidden here. */
PtG5Result pt_g5_machine_open(PtG5Machine **, const PtG5MachineConfig *);
PtG5Result pt_g5_machine_start(PtG5Machine *, unsigned, uint64_t, uint64_t);
/* Stop waits for the owner callback. Snapshot is refused until that boundary. */
PtG5Result pt_g5_machine_stop(PtG5Machine *, unsigned);
PtG5Result pt_g5_machine_snapshot(PtG5Machine *, unsigned, PtG5Cpu *, PtG5Result *);
PtG5Result pt_g5_machine_dma_read(PtG5Machine *, uint64_t, void *, size_t);
PtG5Result pt_g5_machine_dma_write(PtG5Machine *, uint64_t, const void *, size_t);
PtG5Result pt_g5_machine_pic(PtG5Machine *, unsigned, uint32_t, uint32_t *, bool);
PtG5Result pt_g5_machine_irq(PtG5Machine *, unsigned, bool);
/* Board TBEN hook: gate the selected core's TB/DEC oscillator without changing
 * the shared virtual clock or other CPUs. TB register writes remain CPU-local. */
PtG5Result pt_g5_machine_timebase_enable(PtG5Machine *, unsigned, bool);
PtG5Result pt_g5_machine_clock(PtG5Machine *, unsigned, uint64_t *);
PtG5Result pt_g5_machine_close(PtG5Machine *);
#endif
