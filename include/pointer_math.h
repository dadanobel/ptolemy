/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_POINTER_MATH_H
#define PT_POINTER_MATH_H
#include <math.h>
#include <stdint.h>
#include <limits.h>
/* A scroll delta (AppKit: pixels when precise, else lines) in the guest mouse's scroll counts. */
static inline double pt_pointer_scroll(double delta, double counts_per_unit) {
    return isfinite(delta) ? delta * counts_per_unit : 0;
}
static inline int32_t pt_pointer_whole(double *value) {
    if (!isfinite(*value)) {
        *value = 0;
        return 0;
    }
    double n = trunc(*value);
    if (n > INT32_MAX) {
        *value = 0;
        return INT32_MAX;
    }
    if (n < INT32_MIN) {
        *value = 0;
        return INT32_MIN;
    }
    *value -= n;
    return (int32_t)n;
}
#endif
