/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An executor call that waits for the GPU on the CP thread (optics_frontend_gpu_wait_begin/end, as the Metal executor's
 * drain and backpressure waits do) leaves the device lock free. The executor here owns one "surface" of VRAM whose
 * export waits until the test opens a gate; a 2D fill into it makes the CP thread materialize it. While the CP waits:
 * register reads, the kick (CP_RB_WPTR), an interrupt acknowledge and the service tick return at once and the tick still
 * latches the vertical blank; RBBM_STATUS shows the GPU busy; a CPU read of the surface waits for the export and gets its
 * bytes. A reset and a close that arrive during such a wait run after the packet; many short waits run beside two
 * threads of accesses of both kinds. A frame that comes late because of such a wait is reported as a stall of the CP's
 * GPU wait (the session log's stall attribution). With the argument `held` the lock stays
 * held (OPTICS_GPU_WAIT_UNLOCK=0, the behaviour before) and the test shows that a register read then waits: the negative
 * control. Run under ThreadSanitizer. */
#include "optics-test.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum {
    SURF = 0x400000,
    SURF_WORDS = 0x4000,
    PITCH = 256,
    MINE = 0x200000,
    HOLD_MS = 120,
    ROUNDS = 100,
    GEN_INT_STATUS = 0x44,
    DST_PITCH_OFFSET = 0x142c,
    DST_Y_X = 0x1438,
    DST_HEIGHT_WIDTH = 0x143c,
    DP_GUI_MASTER_CNTL = 0x146c,
    DP_BRUSH_FRGD_CLR = 0x147c
};
static atomic_int dirty, waiting, gate, exported, returned, done; /* gate: 0 closed, 1 open, 2 one short wait */
static uint32_t rows[SURF_WORDS];
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static uint32_t pattern(uint32_t i) {
    return i * 2654435761u ^ 0x600dcafeu;
}
/* The surface is GPU-owned (dirty) and its export in flight: wait in slices, as drain() does, then write it back. */
static int materialize_(void *o, PtRadeon *dv, uint64_t start, uint64_t end, int write) {
    (void)o;
    (void)write;
    if (end <= SURF || start >= SURF + 4 * SURF_WORDS || !atomic_load(&dirty))
        return 0;
    for (int g; (g = atomic_load(&gate)) != 1;) {
        int released = optics_frontend_gpu_wait_begin(dv);
        atomic_store(&waiting, 1 + released);
        usleep(g ? 200 : 10000);
        optics_frontend_gpu_wait_end(dv, released);
        if (g)
            break;
    }
    atomic_store(&dirty, 0); /* the write-back below comes through materialize again */
    if (!optics_frontend_gpu_write(dv, SURF, rows, sizeof rows))
        return -1;
    atomic_fetch_add(&exported, 1);
    return 0;
}
static void setup(void) {
    mmio(0x0b00, 0); /* SURFACE_CNTL: no aperture swap (the boot mode's is for 32-bit pixels), ring words as written */
    mmio(CP_RB_BASE, RB);
    mmio(CP_RB_CNTL, 9);
    mmio(SCRATCH_ADDR, SCRATCH);
    mmio(SCRATCH_UMSK, 1);
    wptr = 0;
}
/* One row of 64 pixels filled at the top of the GPU-owned surface, then a scratch write. */
static void fill(uint32_t tag) {
    atomic_store(&dirty, 1);
    reg(DP_GUI_MASTER_CNTL, 0x00f006d2u); /* solid fill (ROP 0xF0, brush 13), 32 bpp, DST_PITCH_OFFSET */
    reg(DST_PITCH_OFFSET, (PITCH / 64) << 22 | SURF >> 10);
    reg(DP_BRUSH_FRGD_CLR, tag);
    reg(DST_Y_X, 0);
    reg(DST_HEIGHT_WIDTH, 1u << 16 | 64);
    reg(SCRATCH_REG0, tag);
    mmio(CP_RB_WPTR, wptr);
}
static int submit_fill(uint32_t tag) { /* returns once the CP waits for the GPU: 1 with the lock released */
    atomic_store(&gate, 0);
    atomic_store(&waiting, 0);
    fill(tag);
    uint64_t limit = now_ns() + 5000000000ull;
    while (!atomic_load(&waiting))
        assert(now_ns() < limit);
    return atomic_load(&waiting) - 1;
}
static void *reader(void *o) { /* the CPU reads a pixel of the surface the GPU still owns */
    *(uint32_t *)o = vr(SURF + 4 * 1000);
    atomic_store(&returned, 1);
    return NULL;
}
static void *register_reader(void *o) {
    *(uint32_t *)o = rd(CP_RB_RPTR);
    atomic_store(&returned, 1);
    return NULL;
}
static void *other(void *o) { /* agent status, another device's DMA, idle and register reads from a third thread */
    (void)o;
    for (unsigned n = 0; !atomic_load(&done); n++) {
        OpticsStatus s;
        optics_frontend_status(d, &s);
        assert(!optics_frontend_ram_before(d, 4096 * (n % 16), 64, n & 1));
        (void)optics_frontend_idle(d);
        (void)rd(CP_RB_RPTR);
    }
    return NULL;
}
static void *opener(void *o) {
    (void)o;
    usleep(30000);
    atomic_store(&gate, 1);
    return NULL;
}
static void finish(uint32_t tag) {
    uint64_t limit = now_ns() + 5000000000ull;
    while (vr(SCRATCH) != tag || !optics_frontend_idle(d))
        assert(now_ns() < limit);
    assert(rd(CP_RB_RPTR) == wptr && !(rd(RBBM_STATUS) >> 31));
}
int main(int argc, char **argv) {
    int held = argc > 1 && !strcmp(argv[1], "held");
    if (held)
        setenv("OPTICS_GPU_WAIT_UNLOCK", "0", 1);
    static const OpticsExecutor ex = {.materialize = materialize_};
    optics_frontend_set_executor(&ex);
    for (uint32_t i = 0; i < SURF_WORDS; i++)
        rows[i] = pattern(i);
    uint8_t *ram = calloc(1, 1 << 20);
    assert(ram);
    PtRadeonConfig c = {.ram = {ram, 1 << 20, NULL}, .cp_thread = 1, .width = 64, .height = 16};
    assert(!pt_radeon_create(&d, &c));
    setup();
    pthread_t t;
    uint32_t value = 0;
    if (held) { /* negative control: with the lock held through the wait, a register read waits for the GPU */
        assert(submit_fill(0x11) == 0);
        assert(!pthread_create(&t, NULL, register_reader, &value));
        usleep(HOLD_MS * 1000);
        assert(!atomic_load(&returned));
        atomic_store(&gate, 1);
        pthread_join(t, NULL);
        finish(0x11);
        pt_radeon_close(d);
        free(ram);
        printf("optics-gpu-wait held: a register read waited %d ms behind the CP's GPU wait (the behaviour before)\n",
               HOLD_MS);
        return 0;
    }
    /* 1. Light accesses beside the CP's GPU wait; the surface's reader waits for the export. */
    assert(submit_fill(0x11) == 1);
    assert(!pthread_create(&t, NULL, reader, &value));
    uint64_t start = now_ns(), worst = 0, a, b;
    unsigned accesses = 0, busy = 0, vblank = 0;
    while ((a = now_ns()) - start < HOLD_MS * 1000000ull) {
        busy += rd(RBBM_STATUS) >> 31;
        (void)rd(CP_RB_RPTR);
        mmio(CP_RB_WPTR, wptr);
        vblank += rd(GEN_INT_STATUS) & 1;
        mmio(GEN_INT_STATUS, 1);
        pt_radeon_service(d, a);
        b = now_ns() - a;
        if (b > worst)
            worst = b;
        accesses++;
    }
    assert(busy == accesses && vblank >= 2); /* GUI_ACTIVE all along; the tick ran: vertical blanks were latched */
    assert(!atomic_load(&returned) && !atomic_load(&exported)); /* the reader is still waiting for the GPU's bytes */
    atomic_store(&gate, 1);
    pthread_join(t, NULL);
    finish(0x11);
    assert(value == pattern(1000) && atomic_load(&exported) == 1);
    assert(vr(SURF) == 0x11000000u && vr(SURF + 4 * 64) == pattern(64)); /* the fill landed on the exported bytes */
    const char *slack = getenv("OPTICS_TEST_SLACK_MS"); /* a loaded host: the limit for one round of five accesses */
    assert(worst <= (slack ? (uint64_t)atoi(slack) : 1) * 1000000ull);
    /* 2. Frames stream (the CPU writes VRAM, the tick publishes); the next one comes after a GPU wait of the CP: one
     * stall, attributed to that wait. */
    for (uint32_t i = 0; i < 6; i++) {
        vw(MINE, i);
        usleep(20000);
        pt_radeon_service(d, now_ns());
    }
    assert(submit_fill(0x55) == 1);
    usleep(HOLD_MS * 1000);
    atomic_store(&gate, 1);
    finish(0x55);
    pt_radeon_service(d, now_ns() + 20000000u);
    FILE *log = tmpfile();
    assert(log);
    pt_radeon_stats(d, log);
    rewind(log);
    static char text[8192];
    text[fread(text, 1, sizeof text - 1, log)] = 0;
    fclose(log);
    assert(strstr(text, "stalls of 100 ms or more while streaming: 1,") &&
           strstr(text, "largest share: executing 0, GPU wait 1, shader 0, fence 0, idle 0 "));
    /* 3. A reset that arrives during the wait runs after the packet; the device works again. */
    assert(submit_fill(0x22) == 1);
    assert(!pthread_create(&t, NULL, opener, NULL));
    assert(!pt_radeon_reset(d));
    assert(atomic_load(&exported) == 3);
    pthread_join(t, NULL);
    setup();
    reg(SCRATCH_REG0, 0x33);
    mmio(CP_RB_WPTR, wptr);
    finish(0x33);
    assert(vr(SURF + 4 * 64) == pattern(64));
    /* 4. Short waits, one per round, beside this thread's VRAM, register and tick accesses and a third thread's. */
    atomic_store(&gate, 2);
    assert(!pthread_create(&t, NULL, other, NULL));
    for (uint32_t r = 1; r <= ROUNDS; r++) {
        fill(0x1000 + r);
        uint64_t limit = now_ns() + 5000000000ull;
        while (vr(SCRATCH) != 0x1000 + r) {
            vw(MINE + 4 * (r % 1024), r);
            assert(vr(MINE + 4 * (r % 1024)) == r);
            pt_radeon_service(d, now_ns());
            (void)rd(RBBM_STATUS);
            assert(now_ns() < limit);
        }
        assert(vr(SURF) == __builtin_bswap32(0x1000 + r) && vr(SURF + 4 * 64) == pattern(64));
    }
    atomic_store(&done, 1);
    pthread_join(t, NULL);
    assert(atomic_load(&exported) == 3 + ROUNDS);
    finish(0x1000 + ROUNDS);
    /* 5. A close during a wait runs after the packet too. */
    assert(submit_fill(0x44) == 1);
    assert(!pthread_create(&t, NULL, opener, NULL));
    pt_radeon_close(d);
    assert(atomic_load(&exported) == 4 + ROUNDS);
    pthread_join(t, NULL);
    free(ram);
    printf("optics-gpu-wait: %u rounds of register reads, kick, interrupt acknowledge and service tick beside a %d ms GPU "
           "wait of the CP, the longest round %.3f ms; the surface's reader waited and read the export; a late frame "
           "counted as a stall of that wait; reset and close in flight; %d rounds of short waits beside two threads\n",
           accesses, HOLD_MS, worst / 1e6, ROUNDS);
    return 0;
}
