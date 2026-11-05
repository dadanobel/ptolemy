/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_FABRIC_H
#define PT_G5_FABRIC_H
#include "g5.h"
/* Host coordination, not a guest MPIC register model. CPU owners apply work and
 * acknowledge AFTER their page tables, JIT links and local TLB are synchronized. */
enum {
    PT_G5_JOB_START = 1,
    PT_G5_JOB_STOP = 2,
    PT_G5_JOB_TLB = 4,
    PT_G5_JOB_IPI = 8
};
typedef struct {
    unsigned flags, ipi;
    uint64_t entry, argument, epoch, ticket, cookie;
} PtG5Job;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    unsigned cpus, online;
    uint64_t epoch, issued, ack[4];
    PtG5Job pending[4], active[4];
    uint64_t cookie;
    uint64_t tb_base, ns_base, hz, remainder, tb_offset[4];
    bool paused;
} PtG5Fabric;
PtG5Result pt_g5_fabric_init(PtG5Fabric *, unsigned, uint64_t, uint64_t);
void pt_g5_fabric_destroy(PtG5Fabric *);
PtG5Result pt_g5_start(PtG5Fabric *, unsigned, uint64_t, uint64_t);
PtG5Result pt_g5_stop(PtG5Fabric *, unsigned);
PtG5Result pt_g5_stopped(PtG5Fabric *, unsigned);
PtG5Result pt_g5_ipi(PtG5Fabric *, unsigned, unsigned);
uint64_t pt_g5_shootdown(PtG5Fabric *);
bool pt_g5_take(PtG5Fabric *, unsigned, PtG5Job *);
PtG5Result pt_g5_wait(PtG5Fabric *, unsigned, PtG5Job *, unsigned);
PtG5Result pt_g5_complete(PtG5Fabric *, unsigned, const PtG5Job *);
bool pt_g5_synced(PtG5Fabric *, uint64_t);
/* Reset only after every owner has stopped and finished all borrowed jobs. */
PtG5Result pt_g5_fabric_reset(PtG5Fabric *, uint64_t);
/* Unadjusted virtual oscillator for DEC; TB writes must not retime DEC. */
PtG5Result pt_g5_clock_ticks(PtG5Fabric *, uint64_t, uint64_t *);
uint64_t pt_g5_tb(PtG5Fabric *, unsigned, uint64_t);
PtG5Result pt_g5_tb_write(PtG5Fabric *, unsigned, uint64_t, uint64_t);
PtG5Result pt_g5_pause(PtG5Fabric *, uint64_t, bool);
#endif
