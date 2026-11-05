/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Allocation accounting catches ownership escape on every injected failure. */
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned outstanding, attempt, fail_at;
static void *checked_malloc(size_t n) {
    if (fail_at && ++attempt == fail_at)
        return NULL;
    void *p = malloc(n);
    if (p)
        outstanding++;
    return p;
}
static void *checked_calloc(size_t n, size_t z) {
    if (fail_at && ++attempt == fail_at)
        return NULL;
    void *p = calloc(n, z);
    if (p)
        outstanding++;
    return p;
}
static void checked_free(void *p) {
    if (p) {
        assert(outstanding);
        outstanding--;
    }
    free(p);
}
#define malloc checked_malloc
#define calloc checked_calloc
#define free checked_free
#include "../src/optics/draw.c"
#undef malloc
#undef calloc
#undef free
#include "r300-test.h"
int main(void) {
    OpticsR300State *s = optics_r300_state_create(1);
    assert(s);
    triangle_state(s);
    passthrough(s);
    uint32_t payload[13];
    memcpy(payload, TRIANGLE, sizeof payload);

    wr(s, 0x4e38, 6u << 21 | 64);
    OpticsDraw d;
    const char *why = NULL;
    assert(optics_draw_build(&d, regs, s, 0x35, payload, 13, NULL, NULL, NULL, &why));
    optics_draw_free(&d);
    unsigned cached = outstanding;
    /* Failure after both vertex and index allocation: missing position output. */
    wr(s, 0x2090, 0);
    assert(!optics_draw_build(&d, regs, s, 0x35, payload, 13, NULL, NULL, NULL, &why));
    assert(!d.vertex && !d.index && !d.corner && outstanding == cached);
    optics_draw_free(&d);
    wr(s, 0x2090, 1);
    for (unsigned n = 1; n <= 2; n++) {
        attempt = 0;
        fail_at = n;
        assert(!optics_draw_build(&d, regs, s, 0x35, payload, 13, NULL, NULL, NULL, &why));
        assert(!d.vertex && !d.index && !d.corner && outstanding == cached);
        optics_draw_free(&d);
    }
    fail_at = 0;
    /* The same failure with point-sprite corner and expanded vertex allocations. */
    payload[0] = 1u | 3u << 4 | 3u << 16;
    wr(s, 0x2090, 0);
    assert(!optics_draw_build(&d, regs, s, 0x35, payload, 13, NULL, NULL, NULL, &why));
    assert(!d.vertex && !d.index && !d.corner && outstanding == cached);
    optics_draw_free(&d);
    optics_r300_state_destroy(s);
    puts("PASS: one cleanup owner, post-allocation refusal and injected OOM, repeated free safe");
    return 0;
}
