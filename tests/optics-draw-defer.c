/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A draw whose shader compiles in the background (OPTICS_EXEC_BUSY, the executor's compiling op): the packet stays in the
 * ring and the CP thread waits without the device lock, so the CPU's register accesses go on meanwhile (a first shader
 * compile no longer stops the guest); the compile's completion wakes the CP, the draw runs once, then the packets after
 * it (a scratch write). RBBM_STATUS shows the GPU busy until then. A ring set up again meanwhile drops the waiting draw. */
#include "optics-test.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

enum {
    COMPILE_MS = 150
};
static atomic_int compiling, busy, drawn;
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static int draw_(void *o, PtRadeon *dv, const OpticsDraw *dr, const char **why) {
    (void)o;
    (void)dv;
    (void)why;
    if (dr->defer && atomic_load(&compiling)) {
        atomic_fetch_add(&busy, 1);
        return OPTICS_EXEC_BUSY;
    }
    atomic_fetch_add(&drawn, 1);
    return OPTICS_EXEC_DONE;
}
static int compiling_(void *o) {
    (void)o;
    return atomic_load(&compiling);
}
static void *compiler(void *o) {
    (void)o;
    usleep(COMPILE_MS * 1000);
    atomic_store(&compiling, 0);
    optics_frontend_wake(d);
    return NULL;
}
static void submit_draw(
        uint32_t
                scratch) { /* the state of tests/optics-draw-cleanup.c, a 3D_DRAW_IMMD_2 of one triangle, a scratch write */
    static const uint32_t a[] = {0x43e0, 0x43e4, 0x2140, 0x20b4, 0x2150, 0x21e0, 0x2090, 0x4600, 0x4604, 0x4608,
                                 0x4610, 0x4614, 0x4618, 0x461c, 0x4620, 0x46c0, 0x47c0, 0x48c0, 0x49c0, 0x4e38};
    const uint32_t v[] = {1440 | 1440u << 13,
                          1503 | 1503u << 13,
                          1u << 8,
                          4,
                          3u | 1u << 13,
                          0xf000 | 1u << 3 | 2u << 6 | 3u << 9,
                          1,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0,
                          0x400000,
                          0,
                          7u << 26,
                          1u << 24,
                          (21u << 7) | (20u << 14),
                          9u | (17u << 7) | (16u << 14),
                          6u << 21 | 64};
    for (unsigned i = 0; i < sizeof a / sizeof *a; i++)
        reg(a[i], v[i]);
    const uint32_t payload[13] = {
            4u | 3u << 4 | 3u << 16, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000};
    ring(0xc0000000u | 12u << 16 | 0x35u << 8);
    for (unsigned i = 0; i < 13; i++)
        ring(payload[i]);
    reg(SCRATCH_REG0, scratch);
    mmio(CP_RB_WPTR, wptr);
}
int main(void) {
    static const OpticsExecutor ex = {.draw = draw_, .compiling = compiling_};
    optics_frontend_set_executor(&ex);
    uint8_t *ram = calloc(1, 1 << 20);
    assert(ram);
    PtRadeonConfig c = {.ram = {ram, 1 << 20, NULL}, .cp_thread = 1};
    assert(!pt_radeon_create(&d, &c));
    mmio(CP_RB_BASE, RB);
    mmio(CP_RB_CNTL, 9);
    mmio(SCRATCH_ADDR, SCRATCH);
    mmio(SCRATCH_UMSK, 1);
    wptr = 0;
    /* 1. The draw waits for its shader; the CPU's accesses meanwhile never wait for the CP. */
    atomic_store(&compiling, 1);
    pthread_t t;
    assert(!pthread_create(&t, NULL, compiler, NULL));
    uint64_t start = now_ns(), worst = 0;
    unsigned accesses = 0, busy_seen = 0;
    submit_draw(1);
    while (vr(SCRATCH) != 1) {
        uint64_t a = now_ns();
        uint32_t s = rd(RBBM_STATUS);
        (void)rd(CP_RB_RPTR);
        uint64_t b = now_ns() - a;
        if (b > worst)
            worst = b;
        accesses++;
        if (atomic_load(&compiling))
            busy_seen += s >> 31;
        assert(now_ns() - start < 5000000000ull);
    }
    uint64_t waited = now_ns() - start;
    pthread_join(t, NULL);
    while (!optics_frontend_idle(d)) {
    }
    assert(atomic_load(&drawn) == 1 && atomic_load(&busy) >= 1 && rd(CP_RB_RPTR) == wptr && !(rd(RBBM_STATUS) >> 31) &&
           rd(SCRATCH_REG0) == 1);
    assert(waited >= COMPILE_MS * 1000000ull && busy_seen && worst < 20000000u); /* no access waited for the compile */
    unsigned retries = (unsigned)atomic_load(&busy);
    /* 2. Without a compile the draw runs at once. */
    submit_draw(2);
    start = now_ns();
    while (vr(SCRATCH) != 2)
        assert(now_ns() - start < 5000000000ull);
    while (!optics_frontend_idle(d)) {
    }
    assert(atomic_load(&drawn) == 2 && (unsigned)atomic_load(&busy) == retries);
    /* 3. A ring set up again while the draw waits drops it: the GPU goes idle, the next submission runs. */
    atomic_store(&compiling, 1);
    submit_draw(3);
    start = now_ns();
    while (!atomic_load(&busy) || (unsigned)atomic_load(&busy) == retries)
        assert(now_ns() - start < 5000000000ull);
    mmio(CP_RB_CNTL, 9);
    wptr = 0;
    atomic_store(&compiling, 0);
    start = now_ns();
    while (!optics_frontend_idle(d))
        assert(now_ns() - start < 5000000000ull);
    assert(!(rd(RBBM_STATUS) >> 31) && atomic_load(&drawn) == 2);
    submit_draw(4);
    start = now_ns();
    while (vr(SCRATCH) != 4)
        assert(now_ns() - start < 5000000000ull);
    assert(atomic_load(&drawn) == 3);
    OpticsFrontendStats s;
    optics_frontend_stats(d, &s);
    assert(!s.failed && s.draws == 3 && !s.draws_refused);
    pt_radeon_close(d);
    free(ram);
    printf("optics-draw-defer: the draw waited %.0f ms for its shader (%u wakes), %u CPU accesses meanwhile, the longest %.2f ms; "
           "no wait without a compile; a ring reset drops the waiting draw\n",
           waited / 1e6, retries, accesses, worst / 1e6);
    return 0;
}
