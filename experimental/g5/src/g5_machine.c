/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_machine.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    struct PtG5Machine *machine;
    unsigned id;
    PtG5Cpu state;
    PtG5Result fault;
    pthread_mutex_t lock;
    atomic_bool kick;
    bool running, stop_complete;
} Core;
struct PtG5Machine {
    PtG5MachineConfig config;
    PtG5Memory memory;
    PtG5Pic pic;
    PtG5Owners *owners;
    Core core[4];
    PtG5CpuBus bus;
    pthread_mutex_t clock_lock;
    struct {
        uint64_t base, at;
        bool enabled;
    } clock[4];
};
static uint64_t ticks(void *user, unsigned cpu) {
    PtG5Machine *m = user;
    pthread_mutex_lock(&m->clock_lock);
    uint64_t v = m->clock[cpu].base;
    if (m->clock[cpu].enabled)
        v += m->config.ticks(m->config.user) - m->clock[cpu].at;
    pthread_mutex_unlock(&m->clock_lock);
    return v;
}
PtG5Result pt_g5_machine_clock(PtG5Machine *m, unsigned cpu, uint64_t *out) {
    if (!m || cpu >= m->config.cpus || !out)
        return PT_G5_BAD_CONFIG;
    *out = ticks(m, cpu);
    return PT_G5_OK;
}
PtG5Result pt_g5_machine_timebase_enable(PtG5Machine *m, unsigned cpu, bool enabled) {
    if (!m || cpu >= m->config.cpus)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&m->clock_lock);
    uint64_t raw = m->config.ticks(m->config.user);
    if (m->clock[cpu].enabled)
        m->clock[cpu].base += raw - m->clock[cpu].at;
    m->clock[cpu].at = raw;
    m->clock[cpu].enabled = enabled;
    pthread_mutex_unlock(&m->clock_lock);
    return PT_G5_OK;
}
static PtG5Result mmio(void *user, unsigned cpu, uint64_t pa, void *bytes, size_t n, bool write) {
    PtG5Machine *m = user;
    return m->config.mmio ? m->config.mmio(m->config.user, cpu, pa, bytes, n, write) : PT_G5_BUS;
}
static PtG5Result invalidate(void *user, unsigned cpu, uint64_t *ticket) {
    (void)cpu;
    PtG5Machine *m = user;
    PtG5Barrier b;
    PtG5Result r = pt_g5_owners_shootdown(m->owners, &b);
    if (r == PT_G5_OK)
        *ticket = b.ticket;
    return r;
}
static PtG5Result synced(void *user, uint64_t ticket) {
    PtG5Machine *m = user; /* This machine never resets the owner fabric epoch in place. */
    return pt_g5_owners_synced(m->owners, (PtG5Barrier){1, ticket});
}
static PtG5Result create(void *user, unsigned id, void **out) {
    PtG5Machine *m = user;
    Core *c = &m->core[id];
    c->machine = m;
    c->id = id;
    pt_g5_cpu_init(&c->state, id, 0, m->config.boot_msr, ticks(m, id));
    *out = c;
    return PT_G5_OK;
}
static PtG5Result apply(void *context, const PtG5Job *job) {
    Core *c = context;
    PtG5Machine *m = c->machine;
    pthread_mutex_lock(&c->lock);
    if (job->flags & PT_G5_JOB_START) {
        pt_g5_cpu_init(&c->state, c->id, job->entry, m->config.boot_msr, ticks(m, c->id));
        c->state.r.gpr[3] = job->argument;
        pt_g5_reservation_clear(&m->memory, c->id);
        c->fault = PT_G5_OK;
        c->running = true;
        c->stop_complete = false;
    }
    if (job->flags & PT_G5_JOB_TLB) {
        /* The reference executor walks on every access and has no native links.
         * A JIT adapter MUST invalidate its own mappings/links here before return. */
        c->state.mmu.generation++;
        atomic_thread_fence(memory_order_seq_cst);
    }
    if (job->flags & PT_G5_JOB_STOP) {
        c->running = false;
        c->stop_complete = true;
        pt_g5_reservation_clear(&m->memory, c->id);
    }
    pthread_mutex_unlock(&c->lock);
    return PT_G5_OK;
}
static PtG5Result run(void *context) {
    Core *c = context;
    PtG5Machine *m = c->machine;
    pthread_mutex_lock(&c->lock);
    for (unsigned count = 0; count < 256 && c->running; count++) {
        if (atomic_exchange_explicit(&c->kick, false, memory_order_acq_rel))
            break;
        PtG5Result r = pt_g5_cpu_step(&c->state, &m->bus, !!(pt_g5_pic_irqs(&m->pic) & (1u << c->id)));
        if (r == PT_G5_RETRY)
            break;
        if (r != PT_G5_OK) {
            c->fault = r;
            c->running = false;
            c->stop_complete = true;
            pthread_mutex_unlock(&c->lock);
            return r;
        }
    }
    pthread_mutex_unlock(&c->lock);
    /* Return to the coordinator even on a stalled tlbsync, so it can apply the
     * issuer's own ticket. An unsupported instruction fails the whole team closed. */
    return PT_G5_OK;
}
static PtG5Result kick(void *context) {
    Core *c = context;
    atomic_store_explicit(&c->kick, true, memory_order_release);
    return PT_G5_OK;
}
static PtG5Result destroy(void *context) {
    (void)context;
    return PT_G5_OK;
}
static const PtG5OwnerOps ops = {create, apply, run, destroy, kick};
PtG5Result pt_g5_machine_open(PtG5Machine **out, const PtG5MachineConfig *config) {
    if (!out)
        return PT_G5_BAD_CONFIG;
    *out = NULL;
    if (!config || !config->ticks || !config->cpus || config->cpus > 4 || !config->banks || config->banks > 8)
        return PT_G5_BAD_CONFIG;
    PtG5Machine *m = calloc(1, sizeof *m);
    if (!m)
        return PT_G5_BAD_CONFIG;
    m->config = *config;
    PtG5Result r = pt_g5_memory_init(&m->memory, config->bank, config->banks);
    if (r != PT_G5_OK) {
        free(m);
        return r;
    }
    r = pt_g5_pic_init(&m->pic, config->cpus);
    if (r != PT_G5_OK) {
        pt_g5_memory_destroy(&m->memory);
        free(m);
        return r;
    }
    if (pthread_mutex_init(&m->clock_lock, NULL)) {
        pt_g5_pic_destroy(&m->pic);
        pt_g5_memory_destroy(&m->memory);
        free(m);
        return PT_G5_BAD_CONFIG;
    }
    uint64_t initial = config->ticks(config->user);
    for (unsigned i = 0; i < config->cpus; i++) {
        m->clock[i].base = m->clock[i].at = initial;
        m->clock[i].enabled = true;
    }
    unsigned n = 0;
    for (; n < config->cpus; n++) {
        if (pthread_mutex_init(&m->core[n].lock, NULL))
            break;
        atomic_init(&m->core[n].kick, false);
        m->core[n].stop_complete = true;
    }
    m->bus = (PtG5CpuBus){&m->memory, m, ticks, invalidate, synced, mmio};
    r = n == config->cpus ? pt_g5_owners_open(&m->owners, config->cpus, &ops, m) : PT_G5_BAD_CONFIG;
    if (r != PT_G5_OK) {
        for (unsigned i = 0; i < n; i++)
            pthread_mutex_destroy(&m->core[i].lock);
        pthread_mutex_destroy(&m->clock_lock);
        pt_g5_pic_destroy(&m->pic);
        pt_g5_memory_destroy(&m->memory);
        free(m);
        return r;
    }
    *out = m;
    return PT_G5_OK;
}
PtG5Result pt_g5_machine_start(PtG5Machine *m, unsigned cpu, uint64_t pc, uint64_t argument) {
    if (!m || cpu >= m->config.cpus)
        return PT_G5_BAD_CONFIG;
    Core *c = &m->core[cpu];
    pthread_mutex_lock(&c->lock);
    if (!c->stop_complete) {
        pthread_mutex_unlock(&c->lock);
        return PT_G5_BAD_CONFIG;
    }
    c->stop_complete = false;
    pthread_mutex_unlock(&c->lock);
    PtG5Result r = pt_g5_owners_start(m->owners, cpu, pc, argument);
    if (r != PT_G5_OK) {
        pthread_mutex_lock(&c->lock);
        c->stop_complete = true;
        pthread_mutex_unlock(&c->lock);
    }
    return r;
}
PtG5Result pt_g5_machine_stop(PtG5Machine *m, unsigned cpu) {
    if (!m || cpu >= m->config.cpus)
        return PT_G5_BAD_CONFIG;
    PtG5Result r = pt_g5_owners_stop(m->owners, cpu);
    if (r != PT_G5_OK)
        return r;
    return pt_g5_owners_wait_stopped(m->owners, cpu, 5000);
}
PtG5Result pt_g5_machine_snapshot(PtG5Machine *m, unsigned cpu, PtG5Cpu *state, PtG5Result *fault) {
    if (!m || cpu >= m->config.cpus || !state || !fault)
        return PT_G5_BAD_CONFIG;
    Core *c = &m->core[cpu];
    pthread_mutex_lock(&c->lock);
    PtG5Result r = c->stop_complete ? PT_G5_OK : PT_G5_RETRY;
    if (r == PT_G5_OK) {
        *state = c->state;
        *fault = c->fault;
    }
    pthread_mutex_unlock(&c->lock);
    return r;
}
PtG5Result pt_g5_machine_dma_read(PtG5Machine *m, uint64_t pa, void *p, size_t n) {
    return pt_g5_read(&m->memory, pa, p, n);
}
PtG5Result pt_g5_machine_dma_write(PtG5Machine *m, uint64_t pa, const void *p, size_t n) {
    return pt_g5_write(&m->memory, pa, p, n);
}
PtG5Result pt_g5_machine_pic(PtG5Machine *m, unsigned cpu, uint32_t offset, uint32_t *value, bool write) {
    return pt_g5_pic_access(&m->pic, cpu, offset, value, write);
}
PtG5Result pt_g5_machine_irq(PtG5Machine *m, unsigned source, bool level) {
    return pt_g5_pic_input(&m->pic, source, level);
}
PtG5Result pt_g5_machine_close(PtG5Machine *m) {
    if (!m)
        return PT_G5_BAD_CONFIG;
    PtG5Result r = pt_g5_owners_close(m->owners);
    for (unsigned c = 0; c < m->config.cpus; c++)
        pthread_mutex_destroy(&m->core[c].lock);
    pthread_mutex_destroy(&m->clock_lock);
    pt_g5_pic_destroy(&m->pic);
    pt_g5_memory_destroy(&m->memory);
    free(m);
    return r;
}
