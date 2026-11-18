/* SPDX-License-Identifier: GPL-2.0-or-later */
/* See smp.h. The lock is first come, first served (a ticket), so a CPU with many short exits cannot starve the other
 * and a serial hand-over is unlock, lock. */
#include "smp.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

enum { SMP_HOST, SMP_RUN, SMP_IDLE };
static struct {
    _Atomic int state, boundary;
} smp_cpu[PT_SMP_MAX];
static int smp_n = 1, smp_serial;
static _Atomic int smp_stopper = -1, smp_waiting; /* the CPU that stopped the world; threads queued for the lock */
static _Thread_local int smp_me;
static void (*smp_boundary)(int);
static pthread_mutex_t smp_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t smp_turn = PTHREAD_COND_INITIALIZER;
static uint64_t smp_next, smp_look = 30000;
static _Atomic uint64_t smp_serving;
static uint64_t smp_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
void pt_smp_look(uint64_t ns) {
    smp_look = ns;
}

static void smp_lock(void) {
    pthread_mutex_lock(&smp_m);
    uint64_t t = smp_next++;
    atomic_fetch_add(&smp_waiting, 1);
    for (int looked = 0; t != smp_serving;) {
        if (!smp_serial && !looked && smp_look) { /* pt_smp_look: the turn is checked again under the mutex */
            looked = 1;
            pthread_mutex_unlock(&smp_m);
            for (uint64_t began = smp_now(); t != atomic_load(&smp_serving) && smp_now() - began < smp_look;) {
#if defined(__aarch64__)
                __asm__ volatile("isb");
#endif
            }
            pthread_mutex_lock(&smp_m);
            continue;
        }
        if (!smp_serial) {
            pthread_cond_wait(&smp_turn, &smp_m);
            continue;
        }
        /* serial: the holder runs guest code with the lock, and a guest loop may wait for this very CPU. Every
         * millisecond the holder is asked to its checkpoint, where it hands over. */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        if ((ts.tv_nsec += 1000000) >= 1000000000)
            ts.tv_nsec -= 1000000000, ts.tv_sec++;
        if (pthread_cond_timedwait(&smp_turn, &smp_m, &ts))
            for (int i = 0; i < smp_n; i++)
                if (i != smp_me)
                    smp_boundary(i);
    }
    atomic_fetch_sub(&smp_waiting, 1);
    pthread_mutex_unlock(&smp_m);
}
static void smp_unlock(void) {
    pthread_mutex_lock(&smp_m);
    smp_serving++;
    pthread_cond_broadcast(&smp_turn);
    pthread_mutex_unlock(&smp_m);
}
void pt_smp_init(int cpus, int serial, void (*boundary)(int)) {
    smp_n = cpus < 1 ? 1 : cpus > PT_SMP_MAX ? PT_SMP_MAX : cpus;
    smp_serial = serial;
    smp_boundary = boundary;
    for (int i = 0; i < PT_SMP_MAX; i++)
        atomic_store(&smp_cpu[i].state, SMP_IDLE); /* a CPU that has not started is as good as asleep */
}
int pt_smp_cpus(void) {
    return smp_n;
}
int pt_smp_self(void) {
    return smp_me;
}
void pt_smp_attach(int n) {
    smp_me = n;
    if (smp_n > 1)
        pt_smp_sleep_end();
}
void pt_smp_run_begin(void) {
    if (smp_n == 1)
        return;
    atomic_store(&smp_cpu[smp_me].state, SMP_RUN);
    if (!smp_serial)
        smp_unlock();
}
void pt_smp_run_end(int boundary) {
    if (smp_n == 1)
        return;
    atomic_store(&smp_cpu[smp_me].boundary, boundary);
    atomic_store(&smp_cpu[smp_me].state, SMP_HOST); /* before the lock: a stopping CPU holds it and watches for this */
    if (!smp_serial)
        smp_lock();
}
void pt_smp_sleep_begin(void) {
    if (smp_n == 1)
        return;
    atomic_store(&smp_cpu[smp_me].state, SMP_IDLE);
    smp_unlock();
}
void pt_smp_sleep_end(void) {
    if (smp_n == 1)
        return;
    smp_lock();
    atomic_store(&smp_cpu[smp_me].boundary, 1);
    atomic_store(&smp_cpu[smp_me].state, SMP_HOST);
}
/* Another CPU has stopped the world: this one, at its boundary, stays there (a stop is short, except the machine's pause). */
static void smp_wait_stop(void) {
    for (unsigned n = 0; atomic_load(&smp_stopper) >= 0 && atomic_load(&smp_stopper) != smp_me; n++) {
        smp_unlock();
        if (n < 200)
            sched_yield();
        else
            nanosleep(&(struct timespec){0, n < 400 ? 50000 : 2000000}, NULL);
        smp_lock();
    }
}
void pt_smp_checkpoint(void) {
    if (smp_n == 1)
        return;
    atomic_store(&smp_cpu[smp_me].boundary, 1);
    smp_wait_stop();
    if (smp_serial && atomic_load(&smp_waiting)) { /* the other CPU's turn */
        smp_unlock();
        smp_lock();
        smp_wait_stop();
    }
}
void pt_smp_stop_world(void) {
    if (smp_n == 1)
        return;
    atomic_store(&smp_cpu[smp_me].boundary, 1);
    smp_wait_stop();
    atomic_store(&smp_stopper, smp_me);
    for (;;) {
        int running = 0, held = 0; /* inside a block: running it, or held by an exit in its middle (it needs the lock) */
        for (int i = 0; i < smp_n; i++) {
            int s = atomic_load(&smp_cpu[i].state);
            if (i == smp_me || s == SMP_IDLE || (s == SMP_HOST && atomic_load(&smp_cpu[i].boundary)))
                continue;
            smp_boundary(i);
            if (s == SMP_RUN)
                running = 1;
            else
                held = 1;
        }
        if (!running && !held)
            return;
        if (held) {
            smp_unlock();
            sched_yield();
            smp_lock();
        } else
            sched_yield();
    }
}
void pt_smp_start_world(void) {
    if (smp_n > 1)
        atomic_store(&smp_stopper, -1);
}
