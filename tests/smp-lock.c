/* SPDX-License-Identifier: GPL-2.0-or-later */
/* smp.c under two host threads that play CPUs: the lock excludes, and a stop of the world leaves the other CPU at a
 * block boundary. Arguments: [serial] */
#include "../epicycle/smp.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int to_boundary[2], running[2], in_block[2], done;
static int host_owner = -1, serial; /* guarded by the engine lock */
static long world_stops, blocks[2];

static void boundary(int n) {
    atomic_store(&to_boundary[n], 1);
}
static void host_work(int n) { /* lock held */
    assert(host_owner == -1);
    host_owner = n;
    for (volatile int i = 0; i < 20; i++)
        ;
    assert(host_owner == n);
    host_owner = -1;
}
/* One block of guest code: leaves in the middle on its own "MMIO", at the end otherwise. */
static int run(int n, unsigned *seed) { /* 1 exit in the middle, 2 the block's end */
    atomic_store(&running[n], 1);
    atomic_store(&in_block[n], 1);
    int r = 2;
    if (!n && rand_r(seed) % 499 == 0) /* a guest loop that waits for the other CPU: it leaves only at an interrupt check */
        while (!atomic_load(&to_boundary[n]) && !atomic_load(&done))
            ;
    for (int i = 0; i < 200; i++) {
        if (atomic_load(&to_boundary[n]) && i > 3)
            break;
        if (rand_r(seed) % 997 == 0) {
            r = 1;
            break;
        }
    }
    if (r == 2) {
        atomic_store(&to_boundary[n], 0);
        atomic_store(&in_block[n], 0);
    }
    atomic_store(&running[n], 0);
    return r;
}
static void *cpu(void *arg) {
    int n = (int)(long)arg;
    unsigned seed = 77 + n;
    pt_smp_attach(n);
    assert(pt_smp_self() == n);
    while (!atomic_load(&done)) {
        host_work(n);
        if (rand_r(&seed) % 4001 == 0) {
            pt_smp_stop_world();
            assert(!atomic_load(&running[!n]) && !atomic_load(&in_block[!n]));
            host_work(n);
            world_stops++;
            pt_smp_start_world();
        }
        if (rand_r(&seed) % 3001 == 0) {
            pt_smp_sleep_begin();
            pt_smp_sleep_end();
        }
        pt_smp_checkpoint();
        pt_smp_run_begin();
        for (;;) {
            int r = run(n, &seed);
            pt_smp_run_end(r == 2);
            if (r == 2)
                break;
            host_work(n); /* an exit in the middle of the block, then on with the block */
            pt_smp_run_begin();
        }
        blocks[n]++;
    }
    pt_smp_sleep_begin(); /* a finished CPU must not hold the other */
    return NULL;
}
int main(int argc, char **argv) {
    serial = argc > 1 && !strcmp(argv[1], "serial");
    pt_smp_init(2, serial, boundary);
    pthread_t t[2];
    for (long i = 0; i < 2; i++)
        pthread_create(&t[i], NULL, cpu, (void *)i);
    struct timespec d = {2, 0};
    nanosleep(&d, NULL);
    atomic_store(&done, 1);
    for (int i = 0; i < 2; i++)
        pthread_join(t[i], NULL);
    printf("smp-lock %s: blocks %ld + %ld, stops of the world %ld\n", serial ? "serial" : "parallel", blocks[0], blocks[1],
           world_stops);
    assert(blocks[0] > 100 && blocks[1] > 100 && world_stops > 2); /* both made progress */
    /* One CPU: every call is a no-op. */
    pt_smp_init(1, 0, NULL);
    pt_smp_attach(0);
    pt_smp_run_begin();
    pt_smp_run_end(1);
    pt_smp_stop_world();
    pt_smp_start_world();
    pt_smp_checkpoint();
    puts("smp-lock: ok");
    return 0;
}
