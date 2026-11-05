/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nvram.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void wr(PtNvram *n, unsigned a, uint8_t v) {
    assert(!pt_nvram_access(n, a, &v, 1, 1));
}
static uint8_t rd(PtNvram *n, unsigned a) {
    uint8_t v;
    assert(!pt_nvram_access(n, a, &v, 1, 0));
    return v;
}
static void seal(uint8_t *b, unsigned gen) {
    pt_write_be(b + 20, 4, gen);
    unsigned a = 1, c = 0;
    for (unsigned i = 20; i < 8192; i++) {
        a = (a + b[i]) % 65521;
        c = (c + a) % 65521;
    }
    pt_write_be(b + 16, 4, c << 16 | a);
}
static void program(PtNvram *n, unsigned at, uint8_t *b) {
    wr(n, at, 0x20);
    wr(n, at, 0xd0);
    assert(rd(n, at) == 0x80);
    wr(n, at, 0xff);
    for (unsigned i = 0; i < 8192; i++)
        assert(rd(n, at + i) == 255);
    for (unsigned i = 0; i < 8192; i++) {
        wr(n, at + i, 0x40);
        wr(n, at + i, b[i]);
        assert(rd(n, at) == 0x80);
    }
    wr(n, at, 0xff);
    for (unsigned i = 0; i < 8192; i++)
        assert(rd(n, at + i) == b[i]);
}
int main(void) {
    PtNvram n;
    pt_nvram_init(&n);
    assert(pt_nvram_platform_uuid(&n) == 1);
    assert(!pt_nvram_flash_enable(&n));
    assert(!pt_nvram_platform_uuid(&n));
    uint8_t of[8192], b[8192], old[8192];
    memcpy(of, n.data, 8192);
    memcpy(b, n.data + 8192, 8192);
    assert(b[0] == 0x5a && pt_read_be(b + 20, 4) == 1);
    uint64_t generation = n.generation;
    wr(&n, 0, 0xff);
    assert(n.generation == generation && !memcmp(n.data, of, 8192));
    b[300] = 'A';
    seal(b, 2);
    program(&n, 0, b);
    assert(!memcmp(n.data + 8192, b, 8192));
    assert(!memcmp(n.data, of, 8192));
    b[300] = 'B';
    seal(b, 3);
    program(&n, 8192, b);
    assert(!memcmp(n.data + 8192, b, 8192));
    /* Invalid NOR transition and a failed erase sequence have finite error status. */
    wr(&n, 8192, 0x40);
    wr(&n, 8192, 0xff);
    assert(rd(&n, 8192) == 0x90);
    wr(&n, 8192, 0x50);
    assert(rd(&n, 8192) == 0x80);
    wr(&n, 8192, 0xff);
    wr(&n, 0, 0x20);
    wr(&n, 0, 0);
    assert(rd(&n, 0) == 0xb0);
    wr(&n, 0, 0xff);
    /* Partial bank is never published, even at orderly shutdown. */
    memcpy(old, n.data + 8192, 8192);
    wr(&n, 0, 0x20);
    wr(&n, 0, 0xd0);
    wr(&n, 0, 0xff);
    wr(&n, 0, 0x40);
    wr(&n, 0, 0x5a);
    wr(&n, 0, 0xff);
    assert(!memcmp(n.data + 8192, old, 8192));
    char dir[] = "/tmp/pt-nv-flash-XXXXXX", path[256], lock[270];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/nvram", dir);
    snprintf(lock, sizeof lock, "%s.lock", path);
    assert(!pt_nvram_open(&n, path));
    assert(!pt_nvram_close(&n));
    PtNvram loaded;
    pt_nvram_init(&loaded);
    assert(!pt_nvram_open(&loaded, path));
    assert(!pt_nvram_flash_enable(&loaded));
    assert(!memcmp(loaded.data + 8192, old, 8192));
    assert(!memcmp(loaded.data, of, 8192));
    assert(!pt_nvram_close(&loaded));
    unlink(path);
    unlink(lock);
    rmdir(dir);
    /* UUID parsing rejects a missing terminator, without reading past the bank. */
    pt_nvram_init(&loaded);
    memset(loaded.data + 8192 + 16, 'x', 0x800 - 16);
    assert(pt_nvram_platform_uuid(&loaded) != 0);
    puts("PASS Core99 flash: alternating banks, status, no OF damage, partial-write recovery, persistence, UUID bounds");
}
