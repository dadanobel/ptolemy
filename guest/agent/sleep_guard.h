/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_SLEEP_GUARD_H
#define PT_SLEEP_GUARD_H
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
typedef struct {
    int (*create)(CFStringRef, uint32_t, uint32_t *);
    int (*release)(uint32_t);
    uint32_t id;
    int active;
} PtSleepGuard;
/* Runtime assertion, not a persistent Energy Saver preference or a host assertion. */
static int pt_sleep_guard_set(PtSleepGuard *g, int enabled) {
    if (!!enabled == g->active)
        return 0;
    if (enabled) {
        if (!g->create || !g->release)
            return -1;
        uint32_t id = 0;
        int e = g->create(CFSTR("NoIdleSleepAssertion"), 255, &id);
        if (e)
            return e;
        g->id = id;
        g->active = 1;
    } else {
        int e = g->release(g->id);
        if (e)
            return e;
        g->active = 0;
    }
    return 0;
}
#endif
