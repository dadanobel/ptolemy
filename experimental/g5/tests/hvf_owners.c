/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real HVF owner/kick/TLBI lifecycle, no PPC OS and no performance measurement. */
#include "g5_owners.h"
#include <Hypervisor/Hypervisor.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define HV(x)                                    \
    do {                                         \
        hv_return_t r_ = (x);                    \
        if (r_ != HV_SUCCESS) {                  \
            fprintf(stderr, "%s: %x\n", #x, r_); \
            return PT_G5_BUS;                    \
        }                                        \
    } while (0)
#define OK(x) assert((x) == PT_G5_OK)
typedef struct {
    hv_vcpu_t v;
    hv_vcpu_exit_t *exit;
    pthread_t owner;
    _Atomic unsigned runs, flushes, starts, stops, destroys;
} Context;
static Context context[4];
static PtG5Result create(void *u, unsigned cpu, void **out) {
    (void)u;
    Context *c = &context[cpu];
    c->owner = pthread_self();
    HV(hv_vcpu_create(&c->v, &c->exit, NULL));
    if (hv_vcpu_set_trap_debug_exceptions(c->v, true) != HV_SUCCESS ||
        hv_vcpu_set_reg(c->v, HV_REG_CPSR, 0x3c5) != HV_SUCCESS) {
        hv_vcpu_destroy(c->v);
        return PT_G5_BUS;
    }
    *out = c;
    return PT_G5_OK;
}
static PtG5Result apply(void *p, const PtG5Job *j) {
    Context *c = p;
    assert(pthread_equal(c->owner, pthread_self()));
    if (j->flags & PT_G5_JOB_START) {
        HV(hv_vcpu_set_reg(c->v, HV_REG_PC, 0));
        atomic_fetch_add(&c->starts, 1);
    }
    if (j->flags & PT_G5_JOB_TLB) {
        uint64_t saved;
        HV(hv_vcpu_get_reg(c->v, HV_REG_PC, &saved));
        HV(hv_vcpu_set_reg(c->v, HV_REG_PC, 0x100));
        do {
            HV(hv_vcpu_run(c->v));
        } while (c->exit->reason == HV_EXIT_REASON_CANCELED);
        if (c->exit->reason != HV_EXIT_REASON_EXCEPTION || c->exit->exception.syndrome >> 26 != 0x3c ||
            (c->exit->exception.syndrome & 0xffff) != 1)
            return PT_G5_BUS;
        HV(hv_vcpu_set_reg(c->v, HV_REG_PC, saved));
        atomic_fetch_add(&c->flushes, 1);
    }
    if (j->flags & PT_G5_JOB_STOP)
        atomic_fetch_add(&c->stops, 1);
    return PT_G5_OK;
}
static PtG5Result run(void *p) {
    Context *c = p;
    assert(pthread_equal(c->owner, pthread_self()));
    HV(hv_vcpu_run(c->v));
    if (c->exit->reason != HV_EXIT_REASON_CANCELED)
        return PT_G5_BUS;
    atomic_fetch_add(&c->runs, 1);
    return PT_G5_OK;
}
static PtG5Result kick(void *p) {
    Context *c = p;
    HV(hv_vcpus_exit(&c->v, 1));
    return PT_G5_OK;
}
static PtG5Result destroy(void *p) {
    Context *c = p;
    assert(pthread_equal(c->owner, pthread_self()));
    HV(hv_vcpu_destroy(c->v));
    atomic_fetch_add(&c->destroys, 1);
    return PT_G5_OK;
}
static void wait_sync(PtG5Owners *p, PtG5Barrier b) {
    OK(pt_g5_owners_wait(p, b, 2000));
}
int main(void) {
    alarm(15);
    void *ram;
    assert(!posix_memalign(&ram, 16384, 16384));
    memset(ram, 0, 16384);
    uint32_t *code = ram;
    code[0] = 0x14000000; /* b . */
    const uint32_t flush[] = {0xd5033a9f, 0xd508831f, 0xd5033b9f, 0xd5033fdf, 0xd4200020};
    memcpy(code + 64, flush, sizeof flush);
    __builtin___clear_cache(ram, (char *)ram + 16384);
    assert(hv_vm_create(NULL) == HV_SUCCESS);
    assert(hv_vm_map(ram, 0, 16384, HV_MEMORY_READ | HV_MEMORY_EXEC) == HV_SUCCESS);
    PtG5OwnerOps ops = {create, apply, run, destroy, kick};
    PtG5Owners *p;
    OK(pt_g5_owners_open(&p, 4, &ops, NULL));
    for (unsigned i = 0; i < 4; i++)
        OK(pt_g5_owners_start(p, i, 0, 0));
    PtG5Barrier b;
    for (unsigned i = 0; i < 32; i++) {
        OK(pt_g5_owners_shootdown(p, &b));
        wait_sync(p, b);
    }
    for (unsigned i = 0; i < 4; i++)
        OK(pt_g5_owners_stop(p, i));
    while (pt_g5_owners_reset(p) != PT_G5_OK)
        usleep(100);
    assert(pt_g5_owners_synced(p, b) == PT_G5_BAD_CONFIG);
    for (unsigned i = 0; i < 4; i++)
        OK(pt_g5_owners_start(p, i, 0, 0));
    OK(pt_g5_owners_shootdown(p, &b));
    wait_sync(p, b);
    OK(pt_g5_owners_close(p));
    for (unsigned i = 0; i < 4; i++)
        assert(atomic_load(&context[i].starts) == 2 && atomic_load(&context[i].flushes) == 33 &&
               atomic_load(&context[i].destroys) == 1);
    assert(hv_vm_unmap(0, 16384) == HV_SUCCESS);
    assert(hv_vm_destroy() == HV_SUCCESS);
    free(ram);
    alarm(0);
    puts("PASS real HVF: four persistent owners, 132 EL1 TLBI stubs, sticky kicks, stop/reset/restart and destruction before unmap");
}
