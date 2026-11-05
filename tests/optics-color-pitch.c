/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Success: retain every documented COLORPITCH bit and keep row addresses exact. */
#include "r300-test.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    OpticsR300State *s = optics_r300_state_create(1);
    assert(s);
    triangle_state(s);
    passthrough(s);
    const unsigned pitches[] = {64, 130, 132, 134, 258, 8194, 16382};
    for (unsigned k = 0; k < sizeof pitches / sizeof *pitches; k++) {
        unsigned pitch = pitches[k];
        wr(s, 0x4e38, 6u << 21 | pitch);
        OpticsDraw d;
        const char *why = NULL;
        int ok = optics_draw_build(&d, regs, s, 0x35, TRIANGLE, 13, NULL, NULL, NULL, &why);
        if (!ok) {
            fprintf(stderr, "pitch %u refused: %s\n", pitch, why ? why : "?");
            assert(ok);
        }
        if (d.cb_pitch != pitch)
            fprintf(stderr, "pitch %u decoded as %u\n", pitch, d.cb_pitch);
        assert(d.cb_pitch == pitch && d.width == pitch);
        for (unsigned y = 0; y < 64; y++)
            assert((uint64_t)y * d.width * 4 == (uint64_t)y * pitch * 4);
        optics_draw_free(&d);
    }
    optics_r300_state_destroy(s);
    puts("PASS: COLORPITCH 13:1, two-pixel units, exact row addresses");
}
