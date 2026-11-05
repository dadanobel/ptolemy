/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_dart.h"
static PtG5Result fault(PtG5DartFault *f, uint64_t io, bool write, bool ht, unsigned code, PtG5Result r) {
    *f = (PtG5DartFault){true,
                         (uint32_t)ht << 31 | (uint32_t)((io >> 12) & 0x7ffffff) << 4 | (uint32_t)write << 3 | code};
    return r;
}
PtG5Result pt_g5_dart_translate(const PtG5DartConfig *d, PtG5Memory *m, uint64_t io, bool write, bool ht,
                                PtG5DmaMapping *out, PtG5DartFault *f) {
    if (!d || !m || !out || !f || d->base_page > 0xffffff || d->pages > 0x1ffff)
        return PT_G5_BAD_CONFIG;
    f->valid = false;
    if (io >= (UINT64_C(1) << 40))
        return fault(f, io, write, ht, 4, PT_G5_BUS);
    uint64_t pa = io;
    if (io >= UINT64_C(0x8000000000)) {
        if (io >= UINT64_C(0x9000000000))
            return fault(f, io, write, ht, 4, PT_G5_BUS);
        pa = io & UINT64_C(0xfffffffff);
    } else if (io >= UINT64_C(0x80000000) && io < UINT64_C(0x100000000)) {
        /* Fixed direct-access window even when DART is enabled. */
    } else if (!d->enabled) {
        if (io >= UINT64_C(0x1000000000))
            return fault(f, io, write, ht, 4, PT_G5_BUS);
    } else {
        uint32_t page = (uint32_t)(io >> 22), index = (uint32_t)((io >> 12) & 1023);
        if (d->pages && page >= d->pages)
            return fault(f, io, write, ht, 0, PT_G5_BUS);
        uint64_t table_page = (uint64_t)d->base_page + page;
        /* Do not silently wrap the 36-bit table address for malformed setup. */
        if (table_page > 0xffffff)
            return PT_G5_BAD_CONFIG;
        uint8_t bytes[4];
        PtG5Result r = pt_g5_read(m, (table_page << 12) | (index << 2), bytes, 4);
        if (r != PT_G5_OK)
            return fault(f, io, write, ht, 4, r);
        uint32_t pte = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
        if (!(pte & 0x80000000u))
            return fault(f, io, write, ht, 1, PT_G5_PAGE);
        if (pte & (write ? 0x20000000u : 0x40000000u))
            return fault(f, io, write, ht, write ? 3 : 2, PT_G5_PROTECTION);
        pa = ((uint64_t)(pte & 0xffffff) << 12) | (io & 4095);
    }
    for (unsigned i = 0; i < m->count; i++) {
        const PtG5Range *b = &m->bank[i].range;
        if (pa >= b->base && pa - b->base < b->size) {
            uint64_t left = b->size - (pa - b->base), page_left = 4096 - (pa & 4095);
            *out = (PtG5DmaMapping){pa, (size_t)(left < page_left ? left : page_left)};
            return PT_G5_OK;
        }
    }
    return fault(f, io, write, ht, 4, PT_G5_BUS);
}
