/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Directed test of pt_r2_io_ram_tracked, the lock-free map of tracked guest RAM pages that lets a device access skip the
 * device lock: watches, snapshots and GPU writes through the GART mark their RAM pages, release/abort/reset clear them,
 * VRAM pages never appear, GART aliases share one page. Then random watches/releases against a reference count, with a
 * second thread reading the map (run under ThreadSanitizer). */
#include "io.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

enum { GART = 0x40000000, GART_PAGES = 512, RAM_BASE = 0x200000, RAM_PAGES = 256 };
static uint8_t vram[1 << 20], ram[RAM_BASE + RAM_PAGES * 4096];
static int protect(void *o, int r, uint64_t a, PtR2IoGuard g) {
    (void)o, (void)r, (void)a, (void)g;
    return 1;
}
static void written(void *o, int r, uint64_t a, size_t n) {
    (void)o, (void)r, (void)a, (void)n;
}
static int map_page(void *o, uint64_t gpu_page, int write, PtR2Span *s) { /* GART pages 256-511 alias 0-255 */
    (void)o, (void)write;
    uint64_t physical = RAM_BASE + ((gpu_page - GART) / 4096 % RAM_PAGES) * 4096;
    *s = (PtR2Span){ram + physical, 4096, 1, 1, physical};
    return 1;
}
static PtR2Io *io;
static uint64_t page(unsigned i) {
    return RAM_BASE + (uint64_t)i * 4096;
}
static int tracked(unsigned i) {
    return pt_r2_io_ram_tracked(io, page(i), 4096);
}
static atomic_int done;
static void *reader(void *o) {
    (void)o;
    uintptr_t n = 0;
    while (!atomic_load(&done))
        n += (unsigned)pt_r2_io_ram_tracked(io, RAM_BASE, RAM_PAGES * 4096);
    return (void *)n;
}
int main(void) {
    PtR2IoConfig c = {.address = {.fb_base = 0, .fb_length = sizeof vram, .gart_base = GART,
                                  .gart_length = GART_PAGES * 4096, .generation = 1, .vram = vram,
                                  .vram_bytes = sizeof vram, .map_page = map_page},
                      .pages = 1024, .transfers = 256, .snapshot_bytes = 1 << 20, .protect = protect,
                      .written = written};
    io = pt_r2_io_create(&c);
    assert(io);
    assert(!pt_r2_io_ram_tracked(io, 0, sizeof ram) && !pt_r2_io_ram_tracked(io, page(3), 0));
    assert(pt_r2_io_ram_tracked(NULL, 0, 1) && pt_r2_io_ram_tracked(io, 0xfffff000u, 0x2000) &&
           pt_r2_io_ram_tracked(io, 1ull << 32, 1)); /* past 4 GiB: the locked path decides */
    /* a GPU write: tracked, and the locked path agrees (BUSY); abort clears */
    PtR2IoToken w, s, v, a, b;
    assert(pt_r2_io_write_begin(io, GART + 0x1000, 4096, &w) == PT_R2_IO_OK);
    assert(tracked(1) && !tracked(0) && !tracked(2));
    assert(pt_r2_io_ram_tracked(io, page(1) - 1, 2) && !pt_r2_io_ram_tracked(io, page(0), 4095));
    assert(pt_r2_io_before_physical(io, 1, page(1), 4, 0) == PT_R2_IO_BUSY);
    assert(pt_r2_io_write_abort(io, w) == PT_R2_IO_OK && !tracked(1));
    /* a watch over two pages; a snapshot; a device write still invalidates the snapshot through the locked path */
    assert(pt_r2_io_watch(io, GART + 0x3000, 8192, &v) == PT_R2_IO_OK && tracked(3) && tracked(4) && !tracked(5));
    assert(pt_r2_io_snapshot(io, GART + 0x5000, 16, &s) == PT_R2_IO_OK && tracked(5));
    assert(pt_r2_io_before_physical(io, 1, page(5), 4, 1) == PT_R2_IO_OK && !pt_r2_io_snapshot_valid(io, s));
    assert(tracked(5)); /* until released */
    assert(pt_r2_io_release(io, s) == PT_R2_IO_OK && !tracked(5) && tracked(3));
    assert(pt_r2_io_release(io, v) == PT_R2_IO_OK && !tracked(3) && !tracked(4));
    /* VRAM pages never enter the RAM map */
    assert(pt_r2_io_watch(io, 0x1000, 4096, &v) == PT_R2_IO_OK && !pt_r2_io_ram_tracked(io, 0, sizeof ram));
    assert(pt_r2_io_release(io, v) == PT_R2_IO_OK);
    /* GART aliases share one page: it stays tracked until both are released */
    assert(pt_r2_io_watch(io, GART + 7 * 4096, 4096, &a) == PT_R2_IO_OK);
    assert(pt_r2_io_watch(io, GART + (256 + 7) * 4096, 4096, &b) == PT_R2_IO_OK && tracked(7));
    assert(pt_r2_io_release(io, a) == PT_R2_IO_OK && tracked(7));
    assert(pt_r2_io_release(io, b) == PT_R2_IO_OK && !tracked(7));
    /* reset forgets everything */
    assert(pt_r2_io_watch(io, GART + 9 * 4096, 4096, &a) == PT_R2_IO_OK && tracked(9));
    assert(pt_r2_io_reset(io) == PT_R2_IO_OK && !pt_r2_io_ram_tracked(io, 0, sizeof ram));
    /* random watches and releases (hash back-shifts included) against a reference count, a reader beside */
    pthread_t t;
    assert(!pthread_create(&t, NULL, reader, NULL));
    static PtR2IoToken live[200];
    static unsigned first[200], count[200], refs[RAM_PAGES];
    unsigned n = 0, x = 12345;
    for (unsigned step = 0; step < 20000; step++) {
        x = x * 1103515245u + 12345u;
        if (n < 200 && (n == 0 || x >> 31)) {
            unsigned g = x >> 8 & (GART_PAGES - 1), k = 1 + (x >> 20) % 3;
            if (g + k > GART_PAGES)
                g = GART_PAGES - k;
            assert(pt_r2_io_watch(io, GART + (uint64_t)g * 4096, (size_t)k * 4096, &live[n]) == PT_R2_IO_OK);
            first[n] = g, count[n] = k;
            for (unsigned i = 0; i < k; i++)
                refs[(g + i) % RAM_PAGES]++;
            n++;
        } else {
            unsigned j = (x >> 8) % n;
            assert(pt_r2_io_release(io, live[j]) == PT_R2_IO_OK);
            for (unsigned i = 0; i < count[j]; i++)
                refs[(first[j] + i) % RAM_PAGES]--;
            n--;
            live[j] = live[n], first[j] = first[n], count[j] = count[n];
        }
        for (unsigned i = 0; i < RAM_PAGES; i++)
            assert(tracked(i) == (refs[i] != 0));
    }
    atomic_store(&done, 1);
    pthread_join(t, NULL);
    assert(pt_r2_io_destroy(io) == PT_R2_IO_OK);
    printf("optics-io-ram-map: watch/snapshot/GPU write/alias/reset, 20000 random steps, reader beside: PASS\n");
    return 0;
}
