/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_owners.h"
#include <stdlib.h>
#include <errno.h>
#include <time.h>
struct PtG5Owners {
    pthread_mutex_t gate;
    pthread_cond_t changed;
    PtG5Fabric fabric;
    PtG5OwnerOps ops;
    void *user;
    struct Owner {
        struct PtG5Owners *team;
        pthread_t thread;
        void *context;
        unsigned cpu;
        bool live;
    } owner[PT_G5_CPUS];
    unsigned created, ready;
    bool closing;
    PtG5Result error;
};
static PtG5Result failure_code(PtG5Result r) {
    return r == PT_G5_RETRY ? PT_G5_BAD_CONFIG : r;
}
static void fail_locked(PtG5Owners *t, PtG5Result error) {
    error = failure_code(error);
    if (t->error == PT_G5_OK)
        t->error = error;
    t->closing = true;
    pthread_cond_broadcast(&t->changed);
    for (unsigned c = 0; c < t->created; c++)
        if (t->owner[c].live)
            (void)t->ops.kick(t->owner[c].context);
}
static PtG5Result notify_locked(PtG5Owners *t, unsigned mask) {
    pthread_cond_broadcast(&t->changed);
    for (unsigned c = 0; c < t->created; c++)
        if ((mask >> c & 1) && t->owner[c].live) {
            PtG5Result r = t->ops.kick(t->owner[c].context);
            if (r != PT_G5_OK) {
                fail_locked(t, r);
                return t->error;
            }
        }
    return PT_G5_OK;
}
static void *owner_main(void *arg) {
    struct Owner *o = arg;
    PtG5Owners *t = o->team;
    void *context = NULL;
    PtG5Result r = t->ops.create(t->user, o->cpu, &context);
    bool running = false;
    pthread_mutex_lock(&t->gate);
    if (r == PT_G5_OK) {
        o->context = context;
        o->live = true;
    } else
        fail_locked(t, r);
    t->ready++;
    pthread_cond_broadcast(&t->changed);
    if (r != PT_G5_OK) {
        pthread_mutex_unlock(&t->gate);
        return NULL;
    }
    for (;;) {
        if (t->closing)
            break;
        PtG5Job job;
        if (pt_g5_take(&t->fabric, o->cpu, &job)) {
            pthread_mutex_unlock(&t->gate);
            r = t->ops.apply(context, &job);
            pthread_mutex_lock(&t->gate);
            if (r != PT_G5_OK) {
                fail_locked(t, r);
                break;
            }
            if (job.flags & PT_G5_JOB_START)
                running = true;
            if (job.flags & PT_G5_JOB_STOP)
                running = false;
            r = pt_g5_complete(&t->fabric, o->cpu, &job);
            if (r != PT_G5_OK) {
                fail_locked(t, r);
                break;
            }
            pthread_cond_broadcast(&t->changed);
        } else if (running) {
            pthread_mutex_unlock(&t->gate);
            r = t->ops.run(context);
            pthread_mutex_lock(&t->gate);
            if (r != PT_G5_OK) {
                fail_locked(t, r);
                break;
            }
        } else
            pthread_cond_wait(&t->changed, &t->gate);
    }
    /* Unpublish before destroy: no producer/kicker can touch a dead context. */
    o->live = false;
    o->context = NULL;
    uint64_t epoch = t->fabric.epoch;
    pthread_mutex_unlock(&t->gate);
    PtG5Job stop = {.flags = PT_G5_JOB_STOP, .epoch = epoch};
    r = t->ops.apply(context, &stop);
    PtG5Result destroyed = t->ops.destroy(context);
    pthread_mutex_lock(&t->gate);
    if (r != PT_G5_OK && t->error == PT_G5_OK)
        t->error = failure_code(r);
    if (destroyed != PT_G5_OK && t->error == PT_G5_OK)
        t->error = failure_code(destroyed);
    pthread_cond_broadcast(&t->changed);
    pthread_mutex_unlock(&t->gate);
    return NULL;
}
PtG5Result pt_g5_owners_open(PtG5Owners **out, unsigned n, const PtG5OwnerOps *ops, void *user) {
    if (!out)
        return PT_G5_BAD_CONFIG;
    *out = NULL;
    if (!n || n > PT_G5_CPUS || !ops || !ops->create || !ops->apply || !ops->run || !ops->destroy || !ops->kick)
        return PT_G5_BAD_CONFIG;
    PtG5Owners *t = calloc(1, sizeof *t);
    if (!t)
        return PT_G5_BAD_CONFIG;
    if (pthread_mutex_init(&t->gate, NULL)) {
        free(t);
        return PT_G5_BAD_CONFIG;
    }
    if (pthread_cond_init(&t->changed, NULL)) {
        pthread_mutex_destroy(&t->gate);
        free(t);
        return PT_G5_BAD_CONFIG;
    }
    if (pt_g5_fabric_init(&t->fabric, n, 1, 0) != PT_G5_OK) {
        pthread_cond_destroy(&t->changed);
        pthread_mutex_destroy(&t->gate);
        free(t);
        return PT_G5_BAD_CONFIG;
    }
    t->ops = *ops;
    t->user = user;
    pthread_mutex_lock(&t->gate);
    for (unsigned c = 0; c < n; c++) {
        t->owner[c].team = t;
        t->owner[c].cpu = c;
        if (pthread_create(&t->owner[c].thread, NULL, owner_main, &t->owner[c])) {
            fail_locked(t, PT_G5_BAD_CONFIG);
            break;
        }
        t->created++;
    }
    while (t->ready < t->created)
        pthread_cond_wait(&t->changed, &t->gate);
    PtG5Result r = t->error;
    pthread_mutex_unlock(&t->gate);
    if (r != PT_G5_OK) {
        pt_g5_owners_close(t);
        return r;
    }
    *out = t;
    return PT_G5_OK;
}
static PtG5Result available(PtG5Owners *t) {
    return t->error != PT_G5_OK ? t->error : t->closing ? PT_G5_BAD_CONFIG : PT_G5_OK;
}
PtG5Result pt_g5_owners_start(PtG5Owners *t, unsigned cpu, uint64_t pc, uint64_t arg) {
    pthread_mutex_lock(&t->gate);
    PtG5Result r = available(t);
    if (r == PT_G5_OK)
        r = pt_g5_start(&t->fabric, cpu, pc, arg);
    if (r == PT_G5_OK)
        r = notify_locked(t, 1u << cpu);
    pthread_mutex_unlock(&t->gate);
    return r;
}
PtG5Result pt_g5_owners_stop(PtG5Owners *t, unsigned cpu) {
    pthread_mutex_lock(&t->gate);
    PtG5Result r = available(t);
    if (r == PT_G5_OK)
        r = pt_g5_stop(&t->fabric, cpu);
    if (r == PT_G5_OK)
        r = notify_locked(t, 1u << cpu);
    pthread_mutex_unlock(&t->gate);
    return r;
}
PtG5Result pt_g5_owners_ipi(PtG5Owners *t, unsigned mask, unsigned vector) {
    pthread_mutex_lock(&t->gate);
    PtG5Result r = available(t);
    if (r == PT_G5_OK)
        r = pt_g5_ipi(&t->fabric, mask, vector);
    if (r == PT_G5_OK)
        r = notify_locked(t, mask);
    pthread_mutex_unlock(&t->gate);
    return r;
}
PtG5Result pt_g5_owners_shootdown(PtG5Owners *t, PtG5Barrier *out) {
    if (!out)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&t->gate);
    PtG5Result r = available(t);
    if (r == PT_G5_OK) {
        *out = (PtG5Barrier){t->fabric.epoch, pt_g5_shootdown(&t->fabric)};
        if (!out->ticket)
            r = PT_G5_BAD_CONFIG;
        else
            r = notify_locked(t, ~0u);
    }
    pthread_mutex_unlock(&t->gate);
    return r;
}
static PtG5Result synced_locked(PtG5Owners *t, PtG5Barrier b) {
    PtG5Result r = available(t);
    if (r == PT_G5_OK) {
        if (b.epoch != t->fabric.epoch || !b.ticket || b.ticket > t->fabric.issued)
            r = PT_G5_BAD_CONFIG;
        else if (!pt_g5_synced(&t->fabric, b.ticket))
            r = PT_G5_RETRY;
    }
    return r;
}
PtG5Result pt_g5_owners_synced(PtG5Owners *t, PtG5Barrier b) {
    pthread_mutex_lock(&t->gate);
    PtG5Result r = synced_locked(t, b);
    pthread_mutex_unlock(&t->gate);
    return r;
}
static uint64_t now_ns(void) {
    struct timespec v;
    clock_gettime(CLOCK_MONOTONIC, &v);
    return (uint64_t)v.tv_sec * 1000000000 + v.tv_nsec;
}
PtG5Result pt_g5_owners_wait(PtG5Owners *t, PtG5Barrier b, unsigned milliseconds) {
    if (milliseconds > 60000)
        return PT_G5_BAD_CONFIG;
    uint64_t end = now_ns() + (uint64_t)milliseconds * 1000000;
    pthread_mutex_lock(&t->gate);
    PtG5Result r;
    while ((r = synced_locked(t, b)) == PT_G5_RETRY) {
        uint64_t now = now_ns();
        if (now >= end)
            break;
        uint64_t left = end - now;
        struct timespec relative = {(time_t)(left / 1000000000), (long)(left % 1000000000)};
        int e = pthread_cond_timedwait_relative_np(&t->changed, &t->gate, &relative);
        if (e && e != ETIMEDOUT) {
            r = PT_G5_BAD_CONFIG;
            break;
        }
    }
    pthread_mutex_unlock(&t->gate);
    return r;
}
PtG5Result pt_g5_owners_reset(PtG5Owners *t) {
    pthread_mutex_lock(&t->gate);
    PtG5Result r = available(t);
    if (r == PT_G5_OK)
        r = pt_g5_fabric_reset(&t->fabric, 0);
    pthread_mutex_unlock(&t->gate);
    return r;
}
PtG5Result pt_g5_owners_wait_stopped(PtG5Owners *t, unsigned cpu, unsigned milliseconds) {
    if (milliseconds > 60000)
        return PT_G5_BAD_CONFIG;
    uint64_t end = now_ns() + (uint64_t)milliseconds * 1000000;
    pthread_mutex_lock(&t->gate);
    PtG5Result r;
    while ((r = available(t)) == PT_G5_OK && (r = pt_g5_stopped(&t->fabric, cpu)) == PT_G5_RETRY) {
        uint64_t now = now_ns();
        if (now >= end)
            break;
        uint64_t left = end - now;
        struct timespec relative = {(time_t)(left / 1000000000), (long)(left % 1000000000)};
        int e = pthread_cond_timedwait_relative_np(&t->changed, &t->gate, &relative);
        if (e && e != ETIMEDOUT) {
            r = PT_G5_BAD_CONFIG;
            break;
        }
    }
    pthread_mutex_unlock(&t->gate);
    return r;
}
PtG5Result pt_g5_owners_close(PtG5Owners *t) {
    if (!t)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&t->gate);
    t->closing = true;
    notify_locked(t, ~0u);
    pthread_mutex_unlock(&t->gate);
    for (unsigned c = 0; c < t->created; c++)
        pthread_join(t->owner[c].thread, NULL);
    PtG5Result r = t->error;
    pt_g5_fabric_destroy(&t->fabric);
    pthread_cond_destroy(&t->changed);
    pthread_mutex_destroy(&t->gate);
    free(t);
    return r;
}
