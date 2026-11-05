/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_fabric.h"
#include <string.h>
#include <errno.h>
#include <time.h>
static uint64_t ticks(PtG5Fabric *f, uint64_t ns) {
    return f->tb_base +
           (f->paused ? 0 : (uint64_t)(((__uint128_t)(ns - f->ns_base) * f->hz + f->remainder) / 1000000000));
}
PtG5Result pt_g5_fabric_init(PtG5Fabric *f, unsigned n, uint64_t hz, uint64_t ns) {
    if (!f || !n || n > 4 || !hz)
        return PT_G5_BAD_CONFIG;
    memset(f, 0, sizeof *f);
    if (pthread_mutex_init(&f->lock, NULL))
        return PT_G5_BAD_CONFIG;
    if (pthread_cond_init(&f->changed, NULL)) {
        pthread_mutex_destroy(&f->lock);
        return PT_G5_BAD_CONFIG;
    }
    f->cpus = n;
    f->hz = hz;
    f->ns_base = ns;
    f->epoch = 1;
    return PT_G5_OK;
}
void pt_g5_fabric_destroy(PtG5Fabric *f) {
    pthread_cond_destroy(&f->changed);
    pthread_mutex_destroy(&f->lock);
}
PtG5Result pt_g5_start(PtG5Fabric *f, unsigned c, uint64_t pc, uint64_t arg) {
    if (c >= f->cpus || (pc & 3))
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&f->lock);
    PtG5Result r = PT_G5_BAD_CONFIG;
    if (!(f->online & (1u << c)) && !f->pending[c].flags && !f->active[c].flags) {
        f->pending[c] = (PtG5Job){.flags = PT_G5_JOB_START, .entry = pc, .argument = arg, .epoch = f->epoch};
        /* Starting owners join every barrier, even before acknowledging reset. */
        f->online |= 1u << c;
        f->ack[c] = f->issued;
        r = PT_G5_OK;
        pthread_cond_broadcast(&f->changed);
    }
    pthread_mutex_unlock(&f->lock);
    return r;
}
PtG5Result pt_g5_stopped(PtG5Fabric *f, unsigned c) {
    if (c >= f->cpus)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&f->lock);
    bool busy = (f->online & (1u << c)) || f->pending[c].flags || f->active[c].flags;
    pthread_mutex_unlock(&f->lock);
    return busy ? PT_G5_RETRY : PT_G5_OK;
}
PtG5Result pt_g5_stop(PtG5Fabric *f, unsigned c) {
    if (c >= f->cpus)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&f->lock);
    if (f->online & (1u << c)) {
        f->pending[c].flags |= PT_G5_JOB_STOP;
        f->pending[c].epoch = f->epoch;
    }
    pthread_cond_broadcast(&f->changed);
    pthread_mutex_unlock(&f->lock);
    return PT_G5_OK;
}
PtG5Result pt_g5_ipi(PtG5Fabric *f, unsigned destinations, unsigned vector) {
    if (vector >= 4 || (destinations & ~((1u << f->cpus) - 1)))
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&f->lock);
    for (unsigned c = 0; c < f->cpus; c++)
        if ((destinations & f->online) & (1u << c)) {
            f->pending[c].flags |= PT_G5_JOB_IPI;
            f->pending[c].ipi |= 1u << vector;
            f->pending[c].epoch = f->epoch;
        }
    pthread_cond_broadcast(&f->changed);
    pthread_mutex_unlock(&f->lock);
    return PT_G5_OK;
}
uint64_t pt_g5_shootdown(PtG5Fabric *f) {
    pthread_mutex_lock(&f->lock);
    uint64_t ticket = 0;
    if (f->issued != UINT64_MAX) {
        ticket = ++f->issued;
        for (unsigned c = 0; c < f->cpus; c++)
            if (f->online & (1u << c)) {
                f->pending[c].flags |= PT_G5_JOB_TLB;
                f->pending[c].ticket = ticket;
                f->pending[c].epoch = f->epoch;
            }
        pthread_cond_broadcast(&f->changed);
    }
    pthread_mutex_unlock(&f->lock);
    return ticket;
}
static bool take_locked(PtG5Fabric *f, unsigned c, PtG5Job *out) {
    bool found = f->pending[c].flags && !f->active[c].flags && f->cookie != UINT64_MAX;
    if (found) {
        f->active[c] = f->pending[c];
        f->active[c].cookie = ++f->cookie;
        *out = f->active[c];
        memset(&f->pending[c], 0, sizeof f->pending[c]);
    }
    return found;
}
bool pt_g5_take(PtG5Fabric *f, unsigned c, PtG5Job *out) {
    if (c >= f->cpus || !out)
        return false;
    pthread_mutex_lock(&f->lock);
    bool found = take_locked(f, c, out);
    pthread_mutex_unlock(&f->lock);
    return found;
}
static uint64_t monotonic_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
PtG5Result pt_g5_wait(PtG5Fabric *f, unsigned c, PtG5Job *out, unsigned milliseconds) {
    if (c >= f->cpus || !out || milliseconds > 60000)
        return PT_G5_BAD_CONFIG;
    uint64_t end = monotonic_ns() + (uint64_t)milliseconds * 1000000;
    pthread_mutex_lock(&f->lock);
    PtG5Result r = PT_G5_RETRY;
    if (f->active[c].flags)
        r = PT_G5_BAD_CONFIG;
    else
        for (;;) {
            if (take_locked(f, c, out)) {
                r = PT_G5_OK;
                break;
            }
            uint64_t now = monotonic_ns();
            if (now >= end)
                break;
            uint64_t left = end - now;
            struct timespec relative = {(time_t)(left / 1000000000), (long)(left % 1000000000)};
            int err = pthread_cond_timedwait_relative_np(&f->changed, &f->lock, &relative);
            if (err && err != ETIMEDOUT) {
                r = PT_G5_BAD_CONFIG;
                break;
            }
        }
    pthread_mutex_unlock(&f->lock);
    return r;
}
PtG5Result pt_g5_complete(PtG5Fabric *f, unsigned c, const PtG5Job *job) {
    if (c >= f->cpus || !job)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&f->lock);
    PtG5Result r = PT_G5_RETRY;
    if (f->active[c].flags && job->epoch == f->epoch && job->cookie == f->active[c].cookie) {
        job = &f->active[c];
        if (job->flags & PT_G5_JOB_TLB && job->ticket > f->ack[c])
            f->ack[c] = job->ticket;
        if (job->flags & PT_G5_JOB_STOP) {
            f->online &= ~(1u << c);
            /* Work published while STOP was borrowed cannot survive a new CPU reset. */
            memset(&f->pending[c], 0, sizeof f->pending[c]);
        }
        memset(&f->active[c], 0, sizeof f->active[c]);
        pthread_cond_broadcast(&f->changed);
        r = PT_G5_OK;
    }
    pthread_mutex_unlock(&f->lock);
    return r;
}
bool pt_g5_synced(PtG5Fabric *f, uint64_t ticket) {
    pthread_mutex_lock(&f->lock);
    bool done = ticket && ticket <= f->issued;
    for (unsigned c = 0; c < f->cpus; c++)
        if ((f->online & (1u << c)) && f->ack[c] < ticket)
            done = false;
    pthread_mutex_unlock(&f->lock);
    return done;
}
PtG5Result pt_g5_fabric_reset(PtG5Fabric *f, uint64_t now) {
    pthread_mutex_lock(&f->lock);
    PtG5Result r = PT_G5_BAD_CONFIG;
    bool busy = false;
    for (unsigned c = 0; c < f->cpus; c++)
        busy |= f->active[c].flags != 0;
    if (!f->online && !busy && f->epoch != UINT64_MAX) {
        memset(f->pending, 0, sizeof f->pending);
        memset(f->ack, 0, sizeof f->ack);
        memset(f->tb_offset, 0, sizeof f->tb_offset);
        f->epoch++;
        f->issued = 0;
        f->ns_base = now;
        f->tb_base = 0;
        f->remainder = 0;
        f->paused = false;
        r = PT_G5_OK;
    }
    pthread_mutex_unlock(&f->lock);
    return r;
}
PtG5Result pt_g5_clock_ticks(PtG5Fabric *f, uint64_t ns, uint64_t *out) {
    if (!out)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&f->lock);
    PtG5Result r = PT_G5_BAD_CONFIG;
    if (ns >= f->ns_base) {
        *out = ticks(f, ns);
        r = PT_G5_OK;
    }
    pthread_mutex_unlock(&f->lock);
    return r;
}
uint64_t pt_g5_tb(PtG5Fabric *f, unsigned cpu, uint64_t ns) {
    pthread_mutex_lock(&f->lock);
    uint64_t v = cpu < f->cpus && ns >= f->ns_base ? ticks(f, ns) + f->tb_offset[cpu] : 0;
    pthread_mutex_unlock(&f->lock);
    return v;
}
PtG5Result pt_g5_tb_write(PtG5Fabric *f, unsigned cpu, uint64_t ns, uint64_t value) {
    pthread_mutex_lock(&f->lock);
    PtG5Result r = PT_G5_BAD_CONFIG;
    if (cpu < f->cpus && ns >= f->ns_base) {
        f->tb_offset[cpu] = value - ticks(f, ns);
        r = PT_G5_OK;
    }
    pthread_mutex_unlock(&f->lock);
    return r;
}
PtG5Result pt_g5_pause(PtG5Fabric *f, uint64_t ns, bool paused) {
    pthread_mutex_lock(&f->lock);
    PtG5Result r = PT_G5_BAD_CONFIG;
    if (ns >= f->ns_base) {
        uint64_t value = ticks(f, ns);
        if (!f->paused)
            f->remainder = (uint64_t)(((__uint128_t)(ns - f->ns_base) * f->hz + f->remainder) % 1000000000);
        f->tb_base = value;
        f->ns_base = ns;
        f->paused = paused;
        r = PT_G5_OK;
    }
    pthread_mutex_unlock(&f->lock);
    return r;
}
