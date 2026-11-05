/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_owners.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define OK(x) assert((x) == PT_G5_OK)
typedef struct Test Test;
typedef struct {
    Test *test;
    unsigned cpu;
    pthread_t thread;
    bool kick;
    _Atomic unsigned starts, flushes, ipis, destroys;
} Context;
struct Test {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    Context c[4];
    int fail_create, fail_apply;
    PtG5Result fail_result;
    bool hold_flush, hold_enter;
    unsigned held_flush, held_enter;
};
static void same(Context *c) {
    assert(pthread_equal(c->thread, pthread_self()));
}
static PtG5Result create(void *arg, unsigned cpu, void **out) {
    Test *t = arg;
    if (t->fail_create == (int)cpu)
        return PT_G5_BUS;
    Context *c = &t->c[cpu];
    c->test = t;
    c->cpu = cpu;
    c->thread = pthread_self();
    *out = c;
    return PT_G5_OK;
}
static PtG5Result apply(void *ctx, const PtG5Job *j) {
    Context *c = ctx;
    Test *t = c->test;
    same(c);
    assert(j->epoch);
    if (j->flags & PT_G5_JOB_START) {
        assert(j->entry == 0x100 && j->argument == c->cpu);
        atomic_fetch_add(&c->starts, 1);
    }
    if (j->flags & PT_G5_JOB_TLB) {
        pthread_mutex_lock(&t->lock);
        if (c->cpu == 3 && t->hold_flush) {
            t->held_flush++;
            pthread_cond_broadcast(&t->changed);
            while (t->hold_flush)
                pthread_cond_wait(&t->changed, &t->lock);
        }
        pthread_mutex_unlock(&t->lock);
        if (t->fail_apply == (int)c->cpu)
            return t->fail_result;
        atomic_fetch_add(&c->flushes, 1);
    }
    if (j->flags & PT_G5_JOB_IPI)
        atomic_fetch_or(&c->ipis, j->ipi);
    return PT_G5_OK;
}
static PtG5Result run(void *ctx) {
    Context *c = ctx;
    Test *t = c->test;
    same(c);
    pthread_mutex_lock(&t->lock);
    if (t->hold_enter) {
        t->held_enter++;
        pthread_cond_broadcast(&t->changed);
        while (t->hold_enter)
            pthread_cond_wait(&t->changed, &t->lock);
    }
    while (!c->kick)
        pthread_cond_wait(&t->changed, &t->lock);
    c->kick = false;
    pthread_mutex_unlock(&t->lock);
    return PT_G5_OK;
}
static PtG5Result kick(void *ctx) {
    Context *c = ctx;
    Test *t = c->test;
    pthread_mutex_lock(&t->lock);
    c->kick = true;
    pthread_cond_broadcast(&t->changed);
    pthread_mutex_unlock(&t->lock);
    return PT_G5_OK;
}
static PtG5Result destroy(void *ctx) {
    Context *c = ctx;
    same(c);
    atomic_fetch_add(&c->destroys, 1);
    return PT_G5_OK;
}
static const PtG5OwnerOps ops = {create, apply, run, destroy, kick};
static void init(Test *t) {
    memset(t, 0, sizeof *t);
    assert(!pthread_mutex_init(&t->lock, NULL));
    assert(!pthread_cond_init(&t->changed, NULL));
    t->fail_create = t->fail_apply = -1;
    t->fail_result = PT_G5_BUS;
}
static void done(Test *t) {
    pthread_cond_destroy(&t->changed);
    pthread_mutex_destroy(&t->lock);
}
static void wait_held(Test *t, bool enter) {
    pthread_mutex_lock(&t->lock);
    while (!(enter ? t->held_enter : t->held_flush))
        pthread_cond_wait(&t->changed, &t->lock);
    pthread_mutex_unlock(&t->lock);
}
static void release(Test *t, bool enter) {
    pthread_mutex_lock(&t->lock);
    if (enter)
        t->hold_enter = false;
    else
        t->hold_flush = false;
    pthread_cond_broadcast(&t->changed);
    pthread_mutex_unlock(&t->lock);
}
static PtG5Result sync_wait(PtG5Owners *p, PtG5Barrier b) {
    return pt_g5_owners_wait(p, b, 2000);
}
int main(void) {
    alarm(15);
    Test t;
    init(&t);
    t.hold_enter = true;
    PtG5Owners *p;
    OK(pt_g5_owners_open(&p, 4, &ops, &t));
    for (unsigned c = 0; c < 4; c++)
        OK(pt_g5_owners_start(p, c, 0x100, c));
    wait_held(&t, true);
    PtG5Barrier first;
    OK(pt_g5_owners_shootdown(p, &first));
    release(&t, true);
    OK(sync_wait(p, first));
    for (unsigned c = 0; c < 4; c++)
        assert(atomic_load(&t.c[c].starts) == 1 && atomic_load(&t.c[c].flushes));
    pthread_mutex_lock(&t.lock);
    t.hold_flush = true;
    pthread_mutex_unlock(&t.lock);
    OK(pt_g5_owners_shootdown(p, &first));
    wait_held(&t, false);
    assert(pt_g5_owners_synced(p, first) == PT_G5_RETRY);
    assert(pt_g5_owners_wait(p, first, 1) == PT_G5_RETRY);
    PtG5Barrier last;
    for (unsigned i = 0; i < 200; i++)
        OK(pt_g5_owners_shootdown(p, &last));
    OK(pt_g5_owners_ipi(p, 10, 2));
    assert(pt_g5_owners_synced(p, last) == PT_G5_RETRY);
    release(&t, false);
    OK(sync_wait(p, last));
    assert(atomic_load(&t.c[1].ipis) == 4 && atomic_load(&t.c[3].ipis) == 4 && !atomic_load(&t.c[0].ipis));
    assert(pt_g5_owners_reset(p) == PT_G5_BAD_CONFIG);
    for (unsigned c = 0; c < 4; c++)
        OK(pt_g5_owners_stop(p, c));
    while (pt_g5_owners_reset(p) != PT_G5_OK)
        usleep(100);
    assert(pt_g5_owners_synced(p, last) == PT_G5_BAD_CONFIG);
    for (unsigned c = 0; c < 4; c++)
        OK(pt_g5_owners_start(p, c, 0x100, c));
    OK(pt_g5_owners_shootdown(p, &last));
    OK(sync_wait(p, last));
    OK(pt_g5_owners_close(p));
    for (unsigned c = 0; c < 4; c++)
        assert(atomic_load(&t.c[c].starts) == 2 && atomic_load(&t.c[c].destroys) == 1);
    done(&t);
    init(&t);
    t.fail_create = 2;
    assert(pt_g5_owners_open(&p, 4, &ops, &t) == PT_G5_BUS && p == NULL);
    for (unsigned c = 0; c < 4; c++)
        assert(atomic_load(&t.c[c].destroys) == (c != 2));
    done(&t);
    init(&t);
    t.fail_apply = 3;
    OK(pt_g5_owners_open(&p, 4, &ops, &t));
    for (unsigned c = 0; c < 4; c++)
        OK(pt_g5_owners_start(p, c, 0x100, c));
    OK(pt_g5_owners_shootdown(p, &last));
    assert(sync_wait(p, last) == PT_G5_BUS);
    assert(pt_g5_owners_close(p) == PT_G5_BUS);
    for (unsigned c = 0; c < 4; c++)
        assert(atomic_load(&t.c[c].destroys) == 1);
    done(&t);
    init(&t);
    t.fail_apply = 0;
    t.fail_result = PT_G5_RETRY;
    OK(pt_g5_owners_open(&p, 1, &ops, &t));
    OK(pt_g5_owners_start(p, 0, 0x100, 0));
    OK(pt_g5_owners_shootdown(p, &last));
    assert(sync_wait(p, last) == PT_G5_BAD_CONFIG);
    assert(pt_g5_owners_close(p) == PT_G5_BAD_CONFIG);
    done(&t);
    puts("PASS persistent owners: sticky pre-entry kick, delayed/coalesced flush, IPI, reset, restart, creation/apply failure and join");
    alarm(0);
}
