/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "g5_dec.h"
void pt_g5_dec_write(PtG5Decrementer *d, uint64_t ticks, uint32_t value) {
    *d = (PtG5Decrementer){ticks, value, true};
}
PtG5Result pt_g5_dec_read(const PtG5Decrementer *d, uint64_t ticks, uint32_t *out) {
    if (!d || !out || !d->initialized || ticks < d->at)
        return PT_G5_BAD_CONFIG;
    *out = d->value - (uint32_t)(ticks - d->at);
    return PT_G5_OK;
}
PtG5Result pt_g5_dec_pending(const PtG5Decrementer *d, uint64_t ticks, bool ee, bool *out) {
    if (!out)
        return PT_G5_BAD_CONFIG;
    uint32_t value;
    PtG5Result r = pt_g5_dec_read(d, ticks, &value);
    if (r == PT_G5_OK)
        *out = ee && (value >> 31);
    return r;
}
PtG5Result pt_g5_dec_next(const PtG5Decrementer *d, uint64_t ticks, uint64_t *distance) {
    if (!distance)
        return PT_G5_BAD_CONFIG;
    uint32_t value;
    PtG5Result r = pt_g5_dec_read(d, ticks, &value);
    if (r == PT_G5_OK)
        *distance = value >> 31 ? 0 : (uint64_t)value + 1;
    return r;
}
