/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Optional microbenchmark, not an OS performance or correctness verdict. */
#define main contracts_main
#include "g5-core.c"
#undef main
#include <time.h>
static double seconds(void) {
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int main(void) {
    uint8_t *lo = calloc(1, 1 << 20), *ht = calloc(1, 1 << 18);
    assert(lo && ht);
    Pt970Region r[] = {{0, 1 << 20, lo, 0}, {HT, 1 << 18, ht, 0}};
    Pt970Bus *bus = ppc970_bus_create(r, 2);
    assert(bus);
    CPU *c = calloc(1, sizeof *c);
    assert(c);
    c->env_ticks970 = ticks;
    assert(!ppc970_attach(c, bus, 0, 0));
    ready(c, 1);
    uint64_t ea = UINT64_C(0x1234567800001000);
    pte(c, bus, ea, 0x1000, 2);
    c->msr |= MSR_IR;
    /* Exercise a late SLB hit, one HPT hit, and the actual instruction fetch. */
    c->slb[63] = c->slb[0];
    c->slb[0].esid = 0;
    for (unsigned i = 0; i < 1024; i++)
        wbe32(lo + 0x1000 + 4 * i, 0x60000000u + i);
    uint64_t sum = 0;
    uint32_t insn;
    const unsigned n = 2000000;
    double start = seconds();
    for (unsigned i = 0; i < n; i++) {
        assert(!ppc970_ifetch(c, ea + 4 * (i & 1023), &insn));
        sum += insn;
    }
    double elapsed = seconds() - start;
    uint64_t expected = (uint64_t)n * 0x60000000u;
    for (unsigned i = 0; i < n; i++)
        expected += i & 1023;
    assert(sum == expected);
    printf("{\"fetches\":%u,\"seconds\":%.9f,\"ns_per_fetch\":%.3f,\"checksum\":%llu}\n", n, elapsed, elapsed * 1e9 / n,
           (unsigned long long)sum);
    ppc970_bus_destroy(bus);
    free(c);
    free(lo);
    free(ht);
    return 0;
}
