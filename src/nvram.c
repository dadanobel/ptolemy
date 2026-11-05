/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "nvram.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
/* The first 8 KiB is the firmware's interleaved 4 KiB OF view. The second
 * bank is the guest CHRP view; updates to one never reconstruct the other. */
static uint8_t nd_nv_checksum(const uint8_t *h) {
    unsigned sum = h[0];
    for (unsigned k = 2; k < 16; k++)
        sum += h[k];
    while (sum > 255)
        sum = (sum & 255) + (sum >> 8);
    return (uint8_t)sum;
}
static void nd_nv_partition(uint8_t *p, unsigned n, unsigned sig, const char *name) {
    memset(p, 0, n);
    p[0] = (uint8_t)sig;
    pt_write_be(p + 2, 2, n / 16);
    memcpy(p + 4, name, strlen(name));
    p[1] = nd_nv_checksum(p);
}
static void nd_nv_of_defaults(PtNvram *n) {
    uint8_t of[4096];
    nd_nv_partition(of, 0xc10, 0x70, "common");
    nd_nv_partition(of + 0xc10, sizeof of - 0xc10, 0x7f, "wwwwwwwwwwww");
    for (unsigned k = 0; k < sizeof of; k++)
        n->data[2 * k] = of[k];
}
/* Start of the OF view's common data, 0 when the view is damaged; *end is its limit. */
static unsigned nd_nv_common(const uint8_t *of, unsigned *end) {
    for (unsigned at = 0; at + 16 <= 4096;) {
        unsigned size = (unsigned)pt_read_be(of + at + 2, 2) * 16;
        if (size < 16 || size > 4096 - at || of[at + 1] != nd_nv_checksum(of + at))
            return 0;
        if (of[at] == 0x70 && !memcmp(of + at + 4, "common", 7)) {
            *end = at + size;
            return at + 16;
        }
        at += size;
    }
    return 0;
}
/* A new 16 KiB NVRAM: the firmware's default OF view (even bytes of the first 8 KiB) and a legacy CHRP guest bank
 * (common, APL,MacOS75, free) in the second 8 KiB. */
void pt_nvram_init(PtNvram *n) {
    memset(n, 0, sizeof *n);
    n->generation = 1;
    nd_nv_of_defaults(n);
    uint8_t *bank = n->data + 8192;
    nd_nv_partition(bank, 0x800, 0x70, "common");
    nd_nv_partition(bank + 0x800, 0x100, 0xa0, "APL,MacOS75");
    uint8_t *free = bank + 0x900;
    nd_nv_partition(free, 8192 - 0x900, 0x7f, "wwwwwwwwwwww");
    pt_write_be(free + 20, 4, 2);
    unsigned a = 0, b = 0;
    for (unsigned k = 20; k < 8192 - 0x900; k++) {
        a = (a + free[k]) % 65521;
        b = (b + a) % 65521;
    }
    pt_write_be(free + 16, 4, (b << 16) | a);
}
static uint32_t nd_nv_adler(const uint8_t *p, unsigned n) {
    uint32_t a = 1, b = 0;
    for (unsigned i = 0; i < n; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}
static int nd_nv_valid(const uint8_t *b) {
    return b[0] == 0x5a && b[1] == nd_nv_checksum(b) && pt_read_be(b + 16, 4) == nd_nv_adler(b + 20, 8192 - 20);
}
static void nd_nv_seal(uint8_t *b) {
    b[1] = nd_nv_checksum(b);
    pt_write_be(b + 16, 4, nd_nv_adler(b + 20, 8192 - 20));
}
/* Hands the aperture to the emulated Core99 flash. A legacy guest bank first gets a 32-byte Core99 header
 * (generation 1, checksums) cut from its free partition; the flash then works on a private copy. */
int pt_nvram_flash_enable(PtNvram *n) {
    if (n->flash_on)
        return 0;
    uint8_t b[8192];
    memcpy(b, n->data + 8192, sizeof b);
    if (!nd_nv_valid(b)) {
        if (b[0] == 0x5a)
            return EINVAL; /* A damaged committed bank is not a legacy format. */
        /* Wrap the existing CHRP partitions, using 32 bytes of the final free partition. */
        unsigned at = 0, last = 0;
        while (at < 8192) {
            unsigned size = (unsigned)pt_read_be(b + at + 2, 2) * 16;
            if (size < 16 || size > 8192 - at || b[at + 1] != nd_nv_checksum(b + at))
                return EINVAL;
            last = at;
            at += size;
        }
        unsigned size = 8192 - last;
        if (b[last] != 0x7f || size < 48)
            return ENOSPC;
        pt_write_be(b + last + 2, 2, (size - 32) / 16);
        b[last + 1] = nd_nv_checksum(b + last);
        memmove(b + 32, b, 8192 - 32);
        nd_nv_partition(b, 32, 0x5a, "nvram");
        pt_write_be(b + 20, 4, 1);
        nd_nv_seal(b);
        memcpy(n->data + 8192, b, sizeof b);
        n->generation++;
    }
    memcpy(n->flash, n->data, 16384);
    n->flash_on = 1;
    n->flash_status = 0x80;
    return 0;
}
/* Core99's byte command protocol. The first flash command takes ownership of the
 * aperture; firmware's interleaved view stays private and cannot be erased by OS X.
 * Persist only a complete, checksummed bank, never an erase or a partial program. */
static void nd_nv_flash_byte(PtNvram *n, unsigned at, uint8_t v) {
    if (n->flash_pending == 0x40) {
        if (at != n->flash_address || (n->flash[at] & v) != v)
            n->flash_status = 0x90;
        else {
            n->flash[at] &= v;
            n->flash_status = 0x80;
        }
        n->flash_pending = 0;
        n->flash_mode = 0x70;
        return;
    }
    if (n->flash_pending == 0x20) {
        if (v == 0xd0 && (at >> 13) == (n->flash_address >> 13)) {
            memset(n->flash + (at & ~8191u), 255, 8192);
            n->flash_status = 0x80;
        } else
            n->flash_status = 0xb0;
        n->flash_pending = 0;
        n->flash_mode = 0x70;
        return;
    }
    switch (v) {
    case 0xff:
        n->flash_mode = 0xff;
        {
            uint8_t *b = n->flash + (at & ~8191u), *old = n->data + 8192;
            if (nd_nv_valid(b) && (!nd_nv_valid(old) || pt_read_be(b + 20, 4) > pt_read_be(old + 20, 4))) {
                memcpy(old, b, 8192);
                n->generation++;
            }
        }
        break;
    case 0x40:
    case 0x20:
        n->flash_pending = v;
        n->flash_address = at;
        break;
    case 0x50:
        n->flash_status = 0x80;
        break;
    case 0x70:
        n->flash_mode = 0x70;
        break;
    default:
        n->flash_status = 0xb0;
        n->flash_mode = 0x70;
        break;
    }
}
/* Guest access to the NVRAM aperture: once the flash owns it (or a bank-start write of 0xff/0x20 claims it), bytes
 * are flash commands and status reads; before that, plain bytes. */
int pt_nvram_access(PtNvram *n, unsigned at, uint8_t *bytes, unsigned count, int write) {
    if (!bytes || at > sizeof n->data || count > sizeof n->data - at)
        return EINVAL;
    if (n->flash_on &&
        (n->flash_mode || (write && count == 1 && !(at & 8191) && (bytes[0] == 0xff || bytes[0] == 0x20)))) {
        if (write) {
            if (!n->flash_mode)
                n->flash_mode = 0xff;
            for (unsigned i = 0; i < count; i++)
                nd_nv_flash_byte(n, at + i, bytes[i]);
        } else if (n->flash_mode == 0x70)
            memset(bytes, n->flash_status, count);
        else
            memcpy(bytes, n->flash + at, count);
    } else if (write) {
        if (memcmp(n->data + at, bytes, count)) {
            memcpy(n->data + at, bytes, count);
            n->generation++;
        }
    } else
        memcpy(bytes, n->data + at, count);
    return 0;
}
/* Sets one Open Firmware variable in the firmware's interleaved OF view (the common partition), keeping the others;
 * ENOSPC when it does not fit. */
int pt_nvram_set_of(PtNvram *n, const char *name, const char *value) {
    if (!name || !value || !*name || strchr(name, '=') || strchr(name, '\n'))
        return EINVAL;
    uint8_t of[4096], out[4096];
    for (unsigned k = 0; k < 4096; k++)
        of[k] = n->data[2 * k];
    memcpy(out, of, sizeof out);
    unsigned at, end = 0, base = nd_nv_common(of, &end);
    if (!base)
        return EINVAL;
    memset(out + base, 0, end - base);
    unsigned used = base;
    size_t key = strlen(name), val = strlen(value);
    for (at = base; at < end && of[at];) {
        unsigned left = end - at;
        const uint8_t *zero = memchr(of + at, 0, left);
        if (!zero)
            return EINVAL;
        unsigned len = (unsigned)(zero - (of + at));
        if (!(len > key && of[at + key] == '=' && !memcmp(of + at, name, key))) {
            if (len + 1 > end - used)
                return ENOSPC;
            memcpy(out + used, of + at, len + 1);
            used += len + 1;
        }
        at += len + 1;
    }
    if (key > end - used || val > end - used - key || key + val + 3 > end - used)
        return ENOSPC;
    memcpy(out + used, name, key);
    out[used + key] = '=';
    memcpy(out + used + key + 1, value, val);
    int changed = memcmp(of, out, sizeof of) != 0;
    if (changed) {
        for (unsigned k = 0; k < 4096; k++)
            n->data[2 * k] = out[k];
        n->generation++;
    }
    return 0;
}
/* Tiger's flash driver can leave 0xff (the flash read-array command) in the first byte of the firmware's OF view, and
 * the firmware then refuses that NVRAM and stops: a damaged OF view gets default partitions again. The engine sets its
 * own OF variables after this. The guest's CHRP bank is left as it is. */
int pt_nvram_repair_of(PtNvram *n) {
    uint8_t of[4096];
    unsigned end;
    for (unsigned k = 0; k < 4096; k++)
        of[k] = n->data[2 * k];
    if (nd_nv_common(of, &end))
        return 0;
    nd_nv_of_defaults(n);
    n->generation++;
    return 1;
}
/* Leopard's kernel publishes IOPlatformUUID from the NVRAM variable platform-uuid (16 bytes); configd would derive one
 * from a built-in Ethernet port, which the RTL8139 in a PCI slot is not, so without it gethostuuid() fails and every
 * process logs "_CFGetHostUUIDString ... Error: 35". The engine seeds one UUID when absent,
 * before handing the bank to the Core99 flash driver; subsequent guest writes use the command transport.
 * No 0x00 or 0xff byte: IODTNVRAM escapes those. 1: added, 0: present, errno otherwise. tu-turu-tutu */
int pt_nvram_platform_uuid(PtNvram *n) {
    uint8_t *bank = n->data + 8192;
    unsigned at = 0, size = 0;
    while (at < 8192) {
        unsigned length = (unsigned)pt_read_be(bank + at + 2, 2) * 16;
        if (length < 16 || length > 8192 - at || bank[at + 1] != nd_nv_checksum(bank + at))
            return EINVAL;
        if (bank[at] == 0x70 && !memcmp(bank + at + 4, "common", 7)) {
            size = at + length;
            at += 16;
            break;
        }
        at += length;
    }
    if (!size)
        return EINVAL;
    for (; at < size && bank[at]; at++) {
        const uint8_t *z = memchr(bank + at, 0, size - at);
        if (!z)
            return EINVAL;
        if ((size_t)(z - (bank + at)) >= 14 && !memcmp(bank + at, "platform-uuid=", 14))
            return 0;
        at = (unsigned)(z - bank);
    }
    if (size - at < 14 + 16 + 2)
        return ENOSPC;
    uint8_t u[16];
    arc4random_buf(u, sizeof u);
    u[6] = (u[6] & 0x0f) | 0x40;
    u[8] = (u[8] & 0x3f) | 0x80;
    for (unsigned k = 0; k < 16; k++)
        if (!u[k] || u[k] == 0xff)
            u[k] = 0x5a;
    memcpy(bank + at, "platform-uuid=", 14);
    memcpy(bank + at + 14, u, 16);
    bank[at + 30] = 0;
    if (bank[0] == 0x5a)
        nd_nv_seal(bank);
    n->generation++;
    return 1;
}
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_t thread;
    int fd, busy, pending, stop, error, loaded;
    uint64_t saved, generation;
    char *path;
    uint8_t bytes[PT_NVRAM_SIZE];
} PtNativeNvramStore;
/* fsync the directory holding path, so a rename or link into it is durable. */
static int nd_nv_sync_parent(const char *path) {
    char dirpath[PATH_MAX];
    if (!dirname_r(path, dirpath))
        return errno;
    int fd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return errno;
    int error = fsync(fd) ? errno : 0;
    if (close(fd) && !error)
        error = errno;
    return error;
}
/* Write all bytes (retrying short writes and EINTR), then fsync. */
static int nd_nv_write_all(int fd, const uint8_t *bytes, size_t count) {
    for (size_t at = 0; at < count;) {
        ssize_t n = write(fd, bytes + at, count - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return n < 0 ? errno : EIO;
        at += (size_t)n;
    }
    return fsync(fd) ? errno : 0;
}
/* 0 when path is a regular 16 KiB file equal to bytes; EEXIST when it differs, errno on failure. */
static int nd_nv_same_backup(const char *path, const uint8_t *bytes) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0)
        return errno;
    struct stat st;
    uint8_t copy[PT_NVRAM_SIZE];
    int error = fstat(fd, &st) ? errno : 0;
    if (!error && (!S_ISREG(st.st_mode) || st.st_size != sizeof copy))
        error = EEXIST;
    for (size_t at = 0; !error && at < sizeof copy;) {
        ssize_t n = read(fd, copy + at, sizeof copy - at);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            error = n < 0 ? errno : EIO;
        else
            at += (size_t)n;
    }
    if (close(fd) && !error)
        error = errno;
    if (!error && memcmp(copy, bytes, sizeof copy))
        error = EEXIST;
    return error;
}
/* Keep bytes as <path>.legacy: written to a temporary file, then linked in, so a reader never sees a partial copy and
 * an existing backup is never replaced (an identical one is accepted). */
static int nd_nv_backup(PtNativeNvramStore *s, const uint8_t *bytes) {
    char *path = NULL, *tmp = NULL;
    int error = 0, fd = -1;
    if (asprintf(&path, "%s.legacy", s->path) < 0 || asprintf(&tmp, "%s.legacy.tmp.XXXXXX", s->path) < 0) {
        error = ENOMEM;
        goto done;
    }
    fd = mkstemp(tmp);
    if (fd < 0) {
        error = errno;
        goto done;
    }
    error = nd_nv_write_all(fd, bytes, PT_NVRAM_SIZE);
    if (close(fd) && !error)
        error = errno;
    fd = -1;
    /* Publish a complete snapshot without replacing a previous backup. */
    if (!error && link(tmp, path))
        error = errno == EEXIST ? nd_nv_same_backup(path, bytes) : errno;
    if (unlink(tmp) && !error)
        error = errno;
    if (!error)
        error = nd_nv_sync_parent(path);
done:
    if (fd >= 0)
        close(fd);
    free(tmp);
    free(path);
    return error;
}
/* Before a session: refuses while a save is pending, migrates to the flash layout when asked, repairs the image and
 * adds the platform UUID; *changes reports what was done (repaired, UUID added, legacy file backed up). */
int pt_nvram_prepare(PtNvram *n, int flash_on, unsigned *changes) {
    PtNativeNvramStore *s = (void *)n->store;
    unsigned flags = 0;
    int error = 0;
    if (changes)
        *changes = 0;
    if (s) {
        pthread_mutex_lock(&s->lock);
        if (s->busy || s->pending || s->error || (s->loaded && s->saved != n->generation))
            error = EBUSY;
        pthread_mutex_unlock(&s->lock);
        if (error)
            return error;
    }
    if (n->flash_mode)
        return EBUSY;
    PtNvram next = *n;
    int migrated = flash_on && n->data[8192] != 0x5a;
    /* Validate the committed bank before UUID insertion could reseal it. */
    if (flash_on && (error = pt_nvram_flash_enable(&next)))
        return error;
    if (pt_nvram_repair_of(&next))
        flags |= PT_NVRAM_REPAIRED;
    int uuid = pt_nvram_platform_uuid(&next);
    if (uuid > 1)
        return uuid;
    if (uuid == 1)
        flags |= PT_NVRAM_UUID_ADDED;
    /* Only the one-way layout migration needs the file kept: a repair alone (after a session in the legacy view)
     * must not conflict with the .legacy kept at migration, or the engine could no longer start. */
    if (s && s->loaded && migrated) {
        if ((error = nd_nv_backup(s, n->data)))
            return error;
        flags |= PT_NVRAM_BACKUP;
    }
    if (next.flash_on)
        memcpy(next.flash, next.data, sizeof next.data);
    *n = next;
    if (changes)
        *changes = flags;
    return 0;
}
/* Writes the snapshot to a temporary file and renames it over the NVRAM file, so a crash leaves the old or the new
 * image, never a mix. */
static int nd_nv_commit(PtNativeNvramStore *s) {
    char *tmp;
    if (asprintf(&tmp, "%s.tmp.XXXXXX", s->path) < 0)
        return ENOMEM;
    int fd = mkstemp(tmp), error = fd < 0 ? errno : 0;
    if (!error)
        error = nd_nv_write_all(fd, s->bytes, sizeof s->bytes);
    if (fd >= 0 && close(fd) && !error)
        error = errno;
    if (!error && rename(tmp, s->path))
        error = errno;
    if (error && fd >= 0)
        unlink(tmp);
    if (!error)
        error = nd_nv_sync_parent(s->path);
    /* A directory-sync failure is a durability error after publication. */
    free(tmp);
    return error;
}
/* Writer thread: commits each pending snapshot, so the CPU thread never waits for the disk. */
static void *nd_nv_worker(void *opaque) {
    PtNativeNvramStore *s = opaque;
    pthread_mutex_lock(&s->lock);
    for (;;) {
        while (!s->pending && !s->stop)
            pthread_cond_wait(&s->wake, &s->lock);
        if (!s->pending && s->stop)
            break;
        s->pending = 0;
        s->busy = 1;
        pthread_mutex_unlock(&s->lock);
        int error = nd_nv_commit(s);
        pthread_mutex_lock(&s->lock);
        s->error = error;
        if (!error)
            s->saved = s->generation;
        s->busy = 0;
        pthread_cond_broadcast(&s->wake);
    }
    pthread_mutex_unlock(&s->lock);
    return NULL;
}
/* Opens or creates the NVRAM file under an exclusive <path>.lock, loads a valid 16 KiB image (or starts from
 * defaults) and starts the writer thread. */
int pt_nvram_open(PtNvram *n, const char *path) {
    if (!path || !*path || n->store)
        return EINVAL;
    PtNativeNvramStore *s = calloc(1, sizeof *s);
    if (!s)
        return ENOMEM;
    s->fd = -1;
    char *lock;
    int error = 0, exists = 0;
    if (asprintf(&lock, "%s.lock", path) < 0) {
        free(s);
        return ENOMEM;
    }
    s->path = strdup(path);
    if (!s->path)
        error = ENOMEM;
    if (!error) {
        s->fd = open(lock, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
        if (s->fd < 0)
            error = errno;
        else if (flock(s->fd, LOCK_EX | LOCK_NB))
            error = errno;
    }
    free(lock);
    uint8_t loaded[PT_NVRAM_SIZE];
    if (!error) {
        int fd = open(path, O_RDONLY | O_NOFOLLOW);
        if (fd < 0) {
            if (errno != ENOENT)
                error = errno;
        } else {
            struct stat st;
            if (fstat(fd, &st))
                error = errno;
            else if (!S_ISREG(st.st_mode) || st.st_size != PT_NVRAM_SIZE)
                error = EINVAL;
            size_t at = 0;
            while (!error && at < sizeof loaded) {
                ssize_t got = read(fd, loaded + at, sizeof loaded - at);
                if (got < 0 && errno == EINTR)
                    continue;
                if (got <= 0)
                    error = got < 0 ? errno : EIO;
                else
                    at += (size_t)got;
            }
            exists = !error;
            close(fd);
        }
    }
    int mutex = 0, cond = 0;
    if (!error) {
        error = pthread_mutex_init(&s->lock, NULL);
        mutex = !error;
    }
    if (!error) {
        error = pthread_cond_init(&s->wake, NULL);
        cond = !error;
    }
    if (!error) {
        s->loaded = exists;
        s->saved = exists ? n->generation : 0;
        error = pthread_create(&s->thread, NULL, nd_nv_worker, s);
    }
    if (error) {
        if (cond)
            pthread_cond_destroy(&s->wake);
        if (mutex)
            pthread_mutex_destroy(&s->lock);
        if (s->fd >= 0)
            close(s->fd);
        free(s->path);
        free(s);
        return error;
    }
    if (exists)
        memcpy(n->data, loaded, sizeof loaded);
    n->store = (PtNvramStore *)s;
    return 0;
}
/* Called by the CPU thread: hands a changed image to the writer without waiting (EAGAIN while a write is in
 * flight). */
int pt_nvram_service(PtNvram *n) {
    PtNativeNvramStore *s = (void *)n->store;
    if (!s)
        return 0;
    int error = pthread_mutex_trylock(&s->lock);
    if (error)
        return error == EBUSY ? EAGAIN : error;
    if (s->error)
        error = s->error;
    else if (s->busy || s->pending)
        error = EAGAIN;
    else if (s->saved != n->generation) {
        memcpy(s->bytes, n->data, sizeof n->data);
        s->generation = n->generation;
        s->pending = 1;
        pthread_cond_signal(&s->wake);
    }
    pthread_mutex_unlock(&s->lock);
    return error;
}
/* Waits for writes in flight, commits the final image if it changed, then stops the writer and releases the lock. */
int pt_nvram_close(PtNvram *n) {
    PtNativeNvramStore *s = (void *)n->store;
    if (!s)
        return 0;
    pthread_mutex_lock(&s->lock);
    while (s->busy || s->pending)
        pthread_cond_wait(&s->wake, &s->lock);
    if (!s->error && s->saved != n->generation) {
        memcpy(s->bytes, n->data, sizeof n->data);
        s->generation = n->generation;
        s->pending = 1;
        pthread_cond_signal(&s->wake);
        while (s->busy || s->pending)
            pthread_cond_wait(&s->wake, &s->lock);
    }
    int error = s->error;
    s->stop = 1;
    pthread_cond_signal(&s->wake);
    pthread_mutex_unlock(&s->lock);
    pthread_join(s->thread, NULL);
    close(s->fd);
    pthread_cond_destroy(&s->wake);
    pthread_mutex_destroy(&s->lock);
    free(s->path);
    free(s);
    n->store = NULL;
    return error;
}
