/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_OWNERS_H
#define PT_G5_OWNERS_H
#include "g5_fabric.h"
typedef struct PtG5Owners PtG5Owners;
typedef struct {
    uint64_t epoch, ticket;
} PtG5Barrier;
typedef struct {
    /* create/apply/run/destroy execute on the same persistent thread per CPU.
     * A failed create cleans its own partial allocation. START must reset local
     * translation/JIT state, including before a stopped CPU rejoins the machine.
     * STOP is idempotent; close sends a final STOP with cookie=0 before destroy.
     * Callbacks finish their operation or return a hard error. RETRY is a backend
     * protocol error, not a pending barrier (retry translations inside run). */
    PtG5Result (*create)(void *user, unsigned cpu, void **context);
    PtG5Result (*apply)(void *context, const PtG5Job *);
    PtG5Result (*run)(void *context);
    PtG5Result (*destroy)(void *context);
    /* Cross-thread, thread-safe, nonblocking and STICKY until the next run.
     * hv_vcpus_exit has this property. kick must not reenter this API. Other
     * callbacks may publish work, but must not wait for their own acknowledgment. */
    PtG5Result (*kick)(void *context);
} PtG5OwnerOps;
PtG5Result pt_g5_owners_open(PtG5Owners **, unsigned, const PtG5OwnerOps *, void *);
PtG5Result pt_g5_owners_start(PtG5Owners *, unsigned, uint64_t, uint64_t);
PtG5Result pt_g5_owners_stop(PtG5Owners *, unsigned);
PtG5Result pt_g5_owners_wait_stopped(PtG5Owners *, unsigned, unsigned milliseconds);
PtG5Result pt_g5_owners_ipi(PtG5Owners *, unsigned, unsigned);
PtG5Result pt_g5_owners_shootdown(PtG5Owners *, PtG5Barrier *);
/* RETRY means unfinished; errors must never be treated as a completed barrier. */
PtG5Result pt_g5_owners_synced(PtG5Owners *, PtG5Barrier);
/* Controller thread only; never wait from an owner callback. RETRY on timeout. */
PtG5Result pt_g5_owners_wait(PtG5Owners *, PtG5Barrier, unsigned milliseconds);
PtG5Result pt_g5_owners_reset(PtG5Owners *);
/* Stop producers before close. All owner callbacks and vCPU destruction finish
 * before return; only then may the caller unmap shared memory/destroy the VM.
 * A broken backend that ignores kick can prevent close: no unsafe timed detach. */
PtG5Result pt_g5_owners_close(PtG5Owners *);
#endif
