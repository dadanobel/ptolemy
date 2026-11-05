/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nvram.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void put(const char *path, const void *bytes, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    assert(write(fd, bytes, n) == (ssize_t)n);
    assert(!close(fd));
}
static void equal(const char *path, const uint8_t *bytes) {
    uint8_t copy[PT_NVRAM_SIZE];
    int fd = open(path, O_RDONLY);
    assert(fd >= 0);
    assert(read(fd, copy, sizeof copy) == sizeof copy);
    assert(!memcmp(copy, bytes, sizeof copy));
    assert(!close(fd));
}
static void load(PtNvram *n, const char *path) {
    pt_nvram_init(n);
    assert(!pt_nvram_open(n, path));
}
int main(void) {
    char dir[] = "./pt-nv-default-XXXXXX", path[256], backup[280], lock[280];
    assert(mkdtemp(dir));
    snprintf(path, sizeof path, "%s/nvram", dir);
    snprintf(backup, sizeof backup, "%s.legacy", path);
    snprintf(lock, sizeof lock, "%s.lock", path);
    PtNvram n;
    uint8_t original[PT_NVRAM_SIZE], prepared[PT_NVRAM_SIZE];
    unsigned flags;
    /* A fresh file needs no misleading backup of bytes that never existed. */
    load(&n, path);
    assert(!pt_nvram_prepare(&n, 1, &flags));
    assert(n.flash_on && !(flags & PT_NVRAM_BACKUP));
    assert(access(backup, F_OK));
    assert(!pt_nvram_close(&n));
    unlink(path);
    pt_nvram_init(&n);
    assert(!pt_nvram_set_of(&n, "user-setting", "keep-me"));
    memcpy(original, n.data, sizeof original);
    put(path, original, sizeof original);
    load(&n, path);
    assert(!pt_nvram_prepare(&n, 1, &flags));
    assert((flags & (PT_NVRAM_BACKUP | PT_NVRAM_UUID_ADDED)) == (PT_NVRAM_BACKUP | PT_NVRAM_UUID_ADDED));
    equal(backup, original);
    equal(path, original);
    assert(!memcmp(n.data, original, 8192));
    assert(!memcmp(n.flash, n.data, sizeof n.data));
    memcpy(prepared, n.data, sizeof prepared);
    assert(!pt_nvram_close(&n));
    equal(path, prepared);
    load(&n, path);
    assert(!pt_nvram_prepare(&n, 1, &flags) && !flags);
    assert(!memcmp(n.data, prepared, sizeof prepared));
    assert(!pt_nvram_close(&n));
    equal(backup, original);
    /* Retry after a crash between backup and migration accepts an exact backup. */
    put(path, original, sizeof original);
    load(&n, path);
    assert(!pt_nvram_prepare(&n, 1, &flags));
    assert(flags & PT_NVRAM_BACKUP);
    assert(!pt_nvram_close(&n));
    equal(backup, original);
    /* A stale, truncated or redirected backup must not authorize mutation. */
    for (unsigned mode = 0; mode < 3; mode++) {
        put(path, original, sizeof original);
        assert(!unlink(backup));
        if (mode == 0)
            put(backup, prepared, sizeof prepared);
        else if (mode == 1)
            put(backup, original, 1);
        else
            assert(!symlink(path, backup));
        load(&n, path);
        uint64_t generation = n.generation;
        assert(pt_nvram_prepare(&n, 1, &flags) != 0);
        assert(!flags && !n.flash_on && n.generation == generation && !memcmp(n.data, original, sizeof original));
        assert(!pt_nvram_close(&n));
        equal(path, original);
    }
    assert(!unlink(backup));
    /* Corrupt committed data must not be resealed by UUID insertion or reset. */
    uint8_t bad[PT_NVRAM_SIZE];
    memcpy(bad, prepared, sizeof bad);
    bad[8192 + 300] ^= 1;
    put(path, bad, sizeof bad);
    load(&n, path);
    assert(pt_nvram_prepare(&n, 1, &flags) == EINVAL);
    assert(!memcmp(n.data, bad, sizeof bad));
    assert(!pt_nvram_close(&n));
    equal(path, bad);
    assert(access(backup, F_OK));
    /* A repair of an already migrated file (OF view damaged in a legacy-view session) needs no backup and must not
     * collide with the one kept at migration. */
    put(backup, original, sizeof original);
    memcpy(bad, prepared, sizeof bad);
    bad[0] = 255;
    put(path, bad, sizeof bad);
    load(&n, path);
    assert(!pt_nvram_prepare(&n, 1, &flags));
    assert(flags == PT_NVRAM_REPAIRED);
    equal(backup, original);
    assert(!pt_nvram_close(&n));
    assert(!unlink(backup));
    put(path, original, sizeof original);
    load(&n, path);
    assert(!pt_nvram_prepare(&n, 0, &flags));
    assert(!n.flash_on && !(flags & PT_NVRAM_BACKUP));
    assert(!pt_nvram_close(&n));
    unlink(path);
    unlink(lock);
    assert(!rmdir(dir));
    puts("PASS NVRAM: fresh/default, exact backup, UUID/OF persistence, retry, stale/truncated/symlink backup rejection, corrupt-bank refusal, repair without a second backup, explicit legacy opt-out");
}
