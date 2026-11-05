/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../guest/agent/sleep_guard.h"
#include <assert.h>
#include <stdio.h>
static int creates, releases, fail;
static int create(CFStringRef type, uint32_t level, uint32_t *id) {
    assert(CFEqual(type, CFSTR("NoIdleSleepAssertion")) && level == 255);
    creates++;
    *id = 37;
    return fail;
}
static int release(uint32_t id) {
    assert(id == 37);
    releases++;
    return fail;
}
int main(void) {
    PtSleepGuard g = {0};
    assert(pt_sleep_guard_set(&g, 1));
    assert(!g.active);
    assert(!pt_sleep_guard_set(&g, 0));
    g.create = create;
    g.release = release;
    fail = 5;
    assert(pt_sleep_guard_set(&g, 1) == 5 && !g.active);
    fail = 0;
    assert(!pt_sleep_guard_set(&g, 1) && g.active);
    assert(!pt_sleep_guard_set(&g, 1) && creates == 2);
    fail = 7;
    assert(pt_sleep_guard_set(&g, 0) == 7 && g.active);
    fail = 0;
    assert(!pt_sleep_guard_set(&g, 0) && !g.active);
    assert(!pt_sleep_guard_set(&g, 0) && releases == 2);
    puts("PASS guest sleep assertion: idempotence, failure, release; no preference edits");
}
