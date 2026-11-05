/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
typedef struct {
    int unused;
} CPU;
static void *radeon = (void *)1;
static uint64_t guest_ram_bytes = 1u << 20;
static unsigned checks, clocks, services, sleeps;
static int pending, denied;
static uint64_t now_ns(void) {
    return (uint64_t)clocks++ * 1000000000;
}
static int optics_frontend_ram_before(void *r, uint64_t pa, unsigned n, int write) {
    assert(r == radeon && pa == 4096 && n == 4 && write == 1);
    checks++;
    if (pending) {
        if (pending > 0)
            pending--;
        return EAGAIN;
    }
    return denied ? EIO : 0;
}
static void pt_radeon_service(void *r, uint64_t now) {
    (void)now;
    assert(r == radeon && ++services < 10);
}
static int fake_sleep(const struct timespec *t, struct timespec *left) {
    assert(t->tv_nsec == 100000 && !left);
    sleeps++;
    return 0;
}
#define nanosleep fake_sleep
#include "g5-gpu-wait.inc"
static void reset(int waits, int error) {
    checks = clocks = services = sleeps = 0;
    pending = waits;
    denied = error;
}
int main(void) {
    CPU c = {0};
    reset(0, 0);
    assert(!radeon_ram970(&c, 4096, 4, 1) && checks == 1 && !clocks && !services && !sleeps);
    reset(0, 1);
    assert(radeon_ram970(&c, 4096, 4, 1) == EIO && checks == 1 && !clocks);
    reset(2, 0);
    assert(!radeon_ram970(&c, 4096, 4, 1) && checks == 3 && services == 2 && sleeps == 2);
    reset(1, 1);
    assert(radeon_ram970(&c, 4096, 4, 1) == EIO && checks == 2 && services == 1);
    reset(-1, 0);
    assert(radeon_ram970(&c, 4096, 4, 1) == EIO && services == 3 && sleeps == 2);
    reset(0, 0);
    assert(!radeon_ram970(&c, guest_ram_bytes, 4, 1) && !checks && !clocks);
    radeon = NULL;
    assert(!radeon_ram970(&c, 4096, 4, 1) && !checks && !clocks);
    puts("PASS 970 GPU ownership: clock-free uncontended path, retry, errors, timeout and bypass bounds");
}
