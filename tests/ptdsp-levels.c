/* SPDX-License-Identifier: GPL-2.0-or-later
 * The DrawSprocket shim's window levels (guest/dsp/levels.h) against a stand-in window server: a shield is found, the
 * process's windows go above it and stay there, the display is released, every level comes back; a process without
 * DrawSprocket is left alone. */
#include "../guest/dsp/levels.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { N = 80, SHIELD = 7, SAVER = 1000 };
static struct {
    int level, on_screen;
} win[N];
static unsigned sets, shield_asks, lists;
static int set_level(void *o, int w, int level) {
    (void)o;
    sets++;
    if (w < 0 || w >= N)
        return 1;
    win[w].level = level;
    return 0;
}
static int get_level(void *o, int w, int *level) {
    (void)o;
    if (w < 0 || w >= N)
        return 1;
    *level = win[w].level;
    return 0;
}
static int on_screen(void *o, int *list, int max, int *count) {
    (void)o;
    lists++;
    *count = 0;
    for (int w = 0; w < N && *count < max; w++)
        if (win[w].on_screen)
            list[(*count)++] = w;
    return 0;
}
static int is_shield(void *o, int w) {
    (void)o;
    shield_asks++;
    return w == SHIELD;
}
int main(void) {
    const PtDspServer v = {set_level, get_level, on_screen, is_shield, 0};
    const int front = SAVER + 10 + PT_DSP_ABOVE;
    PtDsp s;
    /* 1. A process without DrawSprocket: every level goes through as asked, nothing is asked of the server besides. */
    memset(&s, 0, sizeof s);
    memset(win, 0, sizeof win);
    win[1].on_screen = win[SHIELD].on_screen = 1;
    assert(!pt_dsp_set_level(&s, &v, 1, 3) && !pt_dsp_set_level(&s, &v, SHIELD, SAVER + 10) && win[1].level == 3);
    pt_dsp_ordered(&s, &v);
    assert(!pt_dsp_released(&s, &v) && sets == 2 && !shield_asks && !lists && !s.shield && !s.count);
    /* 2. With DrawSprocket, before any shield: still as asked. */
    s.armed = 1;
    win[2].on_screen = 1;
    win[2].level = 0;
    win[4].on_screen = 1;
    win[4].level = front + 20; /* already above: not ours to move */
    win[5].level = 8;          /* off screen */
    assert(!pt_dsp_set_level(&s, &v, 1, 4) && win[1].level == 4 && !s.shield);
    pt_dsp_ordered(&s, &v);
    assert(win[1].level == 4 && win[2].level == 0);
    /* 3. The shield's level is set: the windows on screen below its front go above it; the shield, the window already
     *    above and the one off screen stay. */
    assert(!pt_dsp_set_level(&s, &v, SHIELD, SAVER + 10) && s.shield == SHIELD);
    assert(win[SHIELD].level == SAVER + 10 && win[1].level == front && win[2].level == front);
    assert(win[4].level == front + 20 && win[5].level == 8);
    /* 4. Under the shield: a level the game asks for is kept for later, the window stays in front; a window that comes
     *    on screen is raised at the next ordering; a level above the front is given as asked. */
    assert(!pt_dsp_set_level(&s, &v, 1, 0) && win[1].level == front);
    assert(!pt_dsp_set_level(&s, &v, 6, 2) && win[6].level == front); /* not on screen yet, its level set first */
    win[5].on_screen = 1;
    pt_dsp_ordered(&s, &v);
    assert(win[5].level == front);
    assert(!pt_dsp_set_level(&s, &v, 2, front + 3) && win[2].level == front + 3);
    assert(!pt_dsp_set_level(&s, &v, SHIELD, SAVER + 12) && win[1].level == front + 2 && win[2].level == front + 3);
    /* 5. The display is released: each window has the level it would have had - the last one asked for under the
     *    shield, else the one it had before. Afterwards the shim is out of the way again. */
    assert(pt_dsp_released(&s, &v) == 4 && !s.shield && !s.count);
    assert(win[1].level == 0 && win[6].level == 2 && win[5].level == 8);
    assert(win[2].level == front + 3 && win[4].level == front + 20); /* 2: its own later choice stands */
    assert(!pt_dsp_set_level(&s, &v, 1, 1) && win[1].level == 1);
    pt_dsp_ordered(&s, &v);
    assert(win[1].level == 1 && !pt_dsp_released(&s, &v));
    /* 6. More windows than the shim remembers: those it cannot give back it does not raise. */
    memset(win, 0, sizeof win);
    for (int w = 8; w < N; w++)
        win[w].on_screen = 1;
    assert(!pt_dsp_set_level(&s, &v, SHIELD, SAVER + 10));
    int up = 0;
    for (int w = 8; w < N; w++)
        up += win[w].level == front;
    assert(up == PT_DSP_WINDOWS && pt_dsp_released(&s, &v) == PT_DSP_WINDOWS);
    for (int w = 8; w < N; w++)
        assert(win[w].level == 0);
    puts("ptdsp-levels: ok");
    return 0;
}
