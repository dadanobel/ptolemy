/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5.h"
#include <string.h>

static bool range_ok(PtG5Range r, uint64_t limit) {
    return r.size && r.base < limit && r.size <= limit - r.base;
}
static bool overlaps(PtG5Range a, PtG5Range b) {
    return a.base < b.base + b.size && b.base < a.base + a.size;
}
PtG5Result pt_g5_layout(PtG5Layout *out, const PtG5Range *ram, unsigned nr, const PtG5Range *io, unsigned ni,
                        uint64_t engine_size, unsigned ipa_bits) {
    if (!out || !ram || !nr || nr > 8 || ni > 64 || (ni && !io) || ipa_bits < 32 || ipa_bits > 48 || !engine_size ||
        (engine_size & 0x3fff))
        return PT_G5_BAD_CONFIG;
    uint64_t limit = UINT64_C(1) << ipa_bits, high = 0;
    for (unsigned i = 0; i < nr + ni; i++) {
        PtG5Range a = i < nr ? ram[i] : io[i - nr];
        if (!range_ok(a, UINT64_C(1) << PT_G5_PA_BITS) || !range_ok(a, limit) || ((a.base | a.size) & 0x3fff))
            return PT_G5_BAD_CONFIG;
        for (unsigned j = 0; j < i; j++)
            if (overlaps(a, j < nr ? ram[j] : io[j - nr]))
                return PT_G5_BAD_CONFIG;
        if (a.base + a.size > high)
            high = a.base + a.size;
    }
    /* The private engine follows all board-visible regions, including MMIO. */
    if (high > limit || engine_size > limit - high)
        return PT_G5_BAD_CONFIG;
    PtG5Layout result = {.count = nr, .engine = {high, engine_size}, .ipa_bits = ipa_bits};
    memcpy(result.ram, ram, nr * sizeof *ram);
    *out = result;
    return PT_G5_OK;
}
bool pt_g5_dma32(uint64_t pa, uint64_t n, uint32_t *out) {
    if (!out || !n || pa > UINT32_MAX || n > (UINT64_C(1) << 32) - pa)
        return false;
    *out = (uint32_t)pa;
    return true;
}
static uint8_t *resolve(PtG5Memory *m, uint64_t pa, size_t n, bool store) {
    for (unsigned i = 0; i < m->count; i++) {
        PtG5Bank *b = &m->bank[i];
        if (pa < b->range.base)
            continue;
        uint64_t off = pa - b->range.base;
        if (off <= b->range.size && n <= b->range.size - off)
            return store && b->readonly ? NULL : b->host + (size_t)off;
    }
    return NULL;
}
static void invalidate(PtG5Memory *m, uint64_t pa, size_t n) {
    if (!n)
        return;
    uint64_t first = pa / PT_G5_RESERVATION, last = (pa + n - 1) / PT_G5_RESERVATION;
    for (unsigned c = 0; c < PT_G5_CPUS; c++) {
        PtG5Reservation *r = &m->reservation[c];
        if (r->valid && r->address / PT_G5_RESERVATION >= first && r->address / PT_G5_RESERVATION <= last)
            r->valid = false;
    }
}
static uint64_t get_be(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v = v << 8 | p[i];
    return v;
}
static void put_be(uint8_t *p, unsigned n, uint64_t v) {
    for (unsigned i = 0; i < n; i++) {
        p[n - 1 - i] = (uint8_t)v;
        v >>= 8;
    }
}
PtG5Result pt_g5_memory_init(PtG5Memory *m, const PtG5Bank *banks, unsigned n) {
    if (!m || !banks || !n || n > 8)
        return PT_G5_BAD_CONFIG;
    for (unsigned i = 0; i < n; i++) {
        if (!banks[i].host || !range_ok(banks[i].range, UINT64_C(1) << PT_G5_PA_BITS) ||
            banks[i].range.size > SIZE_MAX || banks[i].range.size > UINTPTR_MAX - (uintptr_t)banks[i].host)
            return PT_G5_BAD_CONFIG;
        for (unsigned j = 0; j < i; j++) {
            if (overlaps(banks[i].range, banks[j].range))
                return PT_G5_BAD_CONFIG;
            /* Board aliases must resolve to one canonical physical bank before this bus. */
            PtG5Range a = {(uintptr_t)banks[i].host, banks[i].range.size};
            PtG5Range b = {(uintptr_t)banks[j].host, banks[j].range.size};
            if (overlaps(a, b))
                return PT_G5_BAD_CONFIG;
        }
    }
    memset(m, 0, sizeof *m);
    if (pthread_mutex_init(&m->lock, NULL))
        return PT_G5_BAD_CONFIG;
    memcpy(m->bank, banks, n * sizeof *banks);
    m->count = n;
    return PT_G5_OK;
}
void pt_g5_memory_destroy(PtG5Memory *m) {
    pthread_mutex_destroy(&m->lock);
}
PtG5Result pt_g5_read(PtG5Memory *m, uint64_t pa, void *dst, size_t n) {
    if (!dst || !n)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&m->lock);
    uint8_t *p = resolve(m, pa, n, false);
    if (p)
        memcpy(dst, p, n);
    pthread_mutex_unlock(&m->lock);
    return p ? PT_G5_OK : PT_G5_BUS;
}
PtG5Result pt_g5_write(PtG5Memory *m, uint64_t pa, const void *src, size_t n) {
    if (!src || !n)
        return PT_G5_BAD_CONFIG;
    pthread_mutex_lock(&m->lock);
    uint8_t *p = resolve(m, pa, n, true);
    if (p) {
        invalidate(m, pa, n);
        memcpy(p, src, n);
    }
    pthread_mutex_unlock(&m->lock);
    return p ? PT_G5_OK : PT_G5_BUS;
}
PtG5Result pt_g5_reserve(PtG5Memory *m, unsigned cpu, uint64_t pa, unsigned n, uint64_t *value) {
    if (cpu >= PT_G5_CPUS || (n != 4 && n != 8) || !value)
        return PT_G5_BAD_CONFIG;
    if (pa & (n - 1))
        return PT_G5_ALIGNMENT;
    pthread_mutex_lock(&m->lock);
    uint8_t *p = resolve(m, pa, n, false);
    if (p) {
        *value = get_be(p, n);
        m->reservation[cpu] = (PtG5Reservation){pa, n, true};
    }
    pthread_mutex_unlock(&m->lock);
    return p ? PT_G5_OK : PT_G5_BUS;
}
PtG5Result pt_g5_conditional(PtG5Memory *m, unsigned cpu, uint64_t pa, unsigned n, uint64_t value, bool *stored) {
    if (cpu >= PT_G5_CPUS || (n != 4 && n != 8) || !stored)
        return PT_G5_BAD_CONFIG;
    if (pa & (n - 1))
        return PT_G5_ALIGNMENT;
    pthread_mutex_lock(&m->lock);
    uint8_t *p = resolve(m, pa, n, true);
    PtG5Reservation old = m->reservation[cpu];
    if (p) {
        *stored = old.valid && old.address == pa && old.bytes == n;
        m->reservation[cpu].valid = false;
        if (*stored) {
            invalidate(m, pa, n);
            put_be(p, n, value);
        }
    }
    pthread_mutex_unlock(&m->lock);
    return p ? PT_G5_OK : PT_G5_BUS;
}
void pt_g5_reservation_clear(PtG5Memory *m, unsigned cpu) {
    if (cpu >= PT_G5_CPUS)
        return;
    pthread_mutex_lock(&m->lock);
    m->reservation[cpu].valid = false;
    pthread_mutex_unlock(&m->lock);
}
PtG5Result pt_g5_pte_read(PtG5Memory *m, uint64_t pa, uint64_t pte[2]) {
    uint8_t bytes[16];
    PtG5Result r = pt_g5_read(m, pa, bytes, 16);
    if (r == PT_G5_OK) {
        pte[0] = get_be(bytes, 8);
        pte[1] = get_be(bytes + 8, 8);
    }
    return r;
}
PtG5Result pt_g5_pte_commit(PtG5Memory *m, uint64_t pa, const uint64_t old[2], uint64_t replacement) {
    pthread_mutex_lock(&m->lock);
    uint8_t *p = resolve(m, pa, 16, true);
    PtG5Result r = PT_G5_BUS;
    if (p) {
        r = PT_G5_RETRY;
        if (get_be(p, 8) == old[0] && get_be(p + 8, 8) == old[1]) {
            invalidate(m, pa + 8, 8);
            put_be(p + 8, 8, replacement);
            r = PT_G5_OK;
        }
    }
    pthread_mutex_unlock(&m->lock);
    return r;
}
