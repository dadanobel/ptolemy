/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_of.h"
static void cell(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
PtG5Result pt_g5_of_memory(const PtG5Range *r, unsigned n, unsigned sizes, uint8_t *out, size_t cap, size_t *used) {
    if (!r || !n || n > 8 || (sizes != 1 && sizes != 2) || !used)
        return PT_G5_BAD_CONFIG;
    size_t bytes = 0;
    const uint64_t chunk = UINT64_C(0xffffc000);
    for (unsigned i = 0; i < n; i++) {
        if (!r[i].size || ((r[i].base | r[i].size) & 0x3fff) || r[i].base >= (UINT64_C(1) << 42) ||
            r[i].size > (UINT64_C(1) << 42) - r[i].base)
            return PT_G5_BAD_CONFIG;
        for (unsigned j = 0; j < i; j++)
            if (r[i].base < r[j].base + r[j].size && r[j].base < r[i].base + r[i].size)
                return PT_G5_BAD_CONFIG;
        uint64_t entries = sizes == 2 ? 1 : (r[i].size + chunk - 1) / chunk;
        if (entries > (SIZE_MAX - bytes) / ((2 + sizes) * 4))
            return PT_G5_BAD_CONFIG;
        bytes += (size_t)entries * (2 + sizes) * 4;
    }
    if (!out) {
        *used = bytes;
        return PT_G5_OK;
    }
    if (cap < bytes)
        return PT_G5_BAD_CONFIG;
    size_t at = 0;
    for (unsigned i = 0; i < n; i++)
        for (uint64_t off = 0; off < r[i].size;) {
            uint64_t size = r[i].size - off;
            if (sizes == 1 && size > chunk)
                size = chunk;
            uint64_t addr = r[i].base + off;
            cell(out + at, (uint32_t)(addr >> 32));
            cell(out + at + 4, (uint32_t)addr);
            if (sizes == 2) {
                cell(out + at + 8, (uint32_t)(size >> 32));
                cell(out + at + 12, (uint32_t)size);
            } else
                cell(out + at + 8, (uint32_t)size);
            at += (2 + sizes) * 4;
            off += size;
        }
    *used = bytes;
    return PT_G5_OK;
}
