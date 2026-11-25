/* SPDX-License-Identifier: GPL-2.0-or-later
 * pt-smp: a guest torture test for two CPUs (Ptolemy Dual G4). Concurrently, for SECONDS (default 60):
 *   atomics   - 4 threads add to one counter with OSAtomicAdd32Barrier (lwarx/stwcx.) and, under an OSSpinLock, to a
 *               plain int: both sums must be exact (a lost update = broken atomicity or mutual exclusion);
 *   condvar   - two threads hand a token back and forth with a pthread mutex + condition variable;
 *   semaphore - two threads hand a token with Mach semaphores;
 *   sleep     - one thread sleeps 2 ms at a time and measures how long it really slept;
 *   time      - every thread checks that mach_absolute_time never goes back (threads move between CPUs).
 * Prints the counts, the longest hand-over and sleep, hand-overs of 100 ms or more, and lost updates. Exit 1 on a lost
 * update or time going back, 2 on a hand-over or sleep of a second or more. */
#include <libkern/OSAtomic.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/semaphore.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile int stop;
static double ns_per_tick;
static volatile int32_t atomic_sum;
static int plain_sum;
static OSSpinLock spin = OS_SPINLOCK_INIT;
static volatile int32_t backwards;
static uint64_t now(void) { return mach_absolute_time(); }
static double ms(uint64_t ticks) { return ticks * ns_per_tick / 1e6; }
static void tick_check(uint64_t *last) {
    uint64_t t = now();
    if (t < *last) OSAtomicIncrement32Barrier(&backwards);
    *last = t;
}
struct stat_ { uint64_t n, slow, worst; };
static void note(struct stat_ *s, uint64_t d) {
    s->n++;
    if (d > s->worst) s->worst = d;
    if (ms(d) >= 100) s->slow++;
}
static int64_t adds[4];
static void *adder(void *p) {
    int64_t *mine = p;
    uint64_t last = 0;
    while (!stop) {
        for (int k = 0; k < 1000; k++) {
            OSAtomicAdd32Barrier(1, &atomic_sum);
            OSSpinLockLock(&spin);
            plain_sum++;
            OSSpinLockUnlock(&spin);
        }
        *mine += 1000;
        tick_check(&last);
    }
    return 0;
}
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int token;
static struct stat_ cv_stat[2];
static void *cv_side(void *p) {
    int me = (int)(intptr_t)p;
    uint64_t last = 0;
    pthread_mutex_lock(&m);
    while (!stop) {
        uint64_t t = now();
        while (token != me && !stop) pthread_cond_wait(&cv, &m);
        note(&cv_stat[me], now() - t);
        token = !me;
        pthread_cond_signal(&cv);
        tick_check(&last);
    }
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&m);
    return 0;
}
static semaphore_t sem[2];
static struct stat_ sem_stat[2];
static void *sem_side(void *p) {
    int me = (int)(intptr_t)p;
    uint64_t last = 0;
    while (!stop) {
        uint64_t t = now();
        mach_timespec_t to = {1, 0};
        while (semaphore_timedwait(sem[me], to) == KERN_OPERATION_TIMED_OUT && !stop) {}
        note(&sem_stat[me], now() - t);
        semaphore_signal(sem[!me]);
        tick_check(&last);
    }
    semaphore_signal(sem[!me]);
    return 0;
}
static struct stat_ sleep_stat;
static void *sleeper(void *p) {
    (void)p;
    uint64_t last = 0;
    while (!stop) {
        uint64_t t = now();
        usleep(2000);
        uint64_t d = now() - t, want = (uint64_t)(2e6 / ns_per_tick);
        note(&sleep_stat, d > want ? d - want : 0);
        tick_check(&last);
    }
    return 0;
}
static void line(const char *what, struct stat_ *s) {
    printf("%-10s %10llu hand-overs, longest %8.1f ms, 100 ms or more %llu\n", what, (unsigned long long)s->n, ms(s->worst),
           (unsigned long long)s->slow);
}
int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 60;
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    ns_per_tick = (double)tb.numer / tb.denom;
    for (int i = 0; i < 2; i++) semaphore_create(mach_task_self(), &sem[i], SYNC_POLICY_FIFO, 0);
    pthread_t th[9];
    int n = 0;
    for (int i = 0; i < 4; i++) pthread_create(&th[n++], 0, adder, &adds[i]);
    for (int i = 0; i < 2; i++) pthread_create(&th[n++], 0, cv_side, (void *)(intptr_t)i);
    for (int i = 0; i < 2; i++) pthread_create(&th[n++], 0, sem_side, (void *)(intptr_t)i);
    pthread_create(&th[n++], 0, sleeper, 0);
    semaphore_signal(sem[0]);
    sleep(seconds);
    stop = 1;
    for (int i = 0; i < n; i++) pthread_join(th[i], 0);
    int64_t want = adds[0] + adds[1] + adds[2] + adds[3];
    int32_t lost_a = (int32_t)((uint32_t)want - (uint32_t)atomic_sum), lost_p = (int32_t)((uint32_t)want - (uint32_t)plain_sum);
    printf("atomics    %lld adds: lost by the atomic add %d, lost under the spin lock %d\n", (long long)want, (int)lost_a, (int)lost_p);
    struct stat_ c = cv_stat[0], s = sem_stat[0];
    c.n += cv_stat[1].n, c.slow += cv_stat[1].slow, c.worst = c.worst > cv_stat[1].worst ? c.worst : cv_stat[1].worst;
    s.n += sem_stat[1].n, s.slow += sem_stat[1].slow, s.worst = s.worst > sem_stat[1].worst ? s.worst : sem_stat[1].worst;
    line("condvar", &c);
    line("semaphore", &s);
    printf("sleep      %10llu sleeps of 2 ms, worst oversleep %.1f ms, oversleeps of 100 ms or more %llu\n",
           (unsigned long long)sleep_stat.n, ms(sleep_stat.worst), (unsigned long long)sleep_stat.slow);
    printf("time       went back %d times\n", (int)backwards);
    if (lost_a || lost_p || backwards) return 1;
    if (ms(c.worst) >= 1000 || ms(s.worst) >= 1000 || ms(sleep_stat.worst) >= 1000) return 2;
    return 0;
}
