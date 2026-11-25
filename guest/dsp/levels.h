/* SPDX-License-Identifier: GPL-2.0-or-later
 * Window levels of the DrawSprocket shim (guest/dsp/ptdsp.c), apart from the window server so that they can be tested
 * without a Mac (tests/ptdsp-levels.c).
 *
 * DrawSprocket captures the display and puts a capture shield at screen saver level + 10. A game written before
 * DSpSetWindowToFront existed draws into its own windows under the shield: a black screen. While this process holds a
 * shield, its windows are kept at the level DSpSetWindowToFront uses (the shield's + PT_DSP_ABOVE); when the display is
 * released each one gets back the level it would have had. Nothing happens in a process that is not armed (one
 * without DrawSprocket), and nothing before a shield's level is set. */
#ifndef PT_DSP_LEVELS_H
#define PT_DSP_LEVELS_H

enum { PT_DSP_ABOVE = 5, PT_DSP_WINDOWS = 64 };

/* The window server as the shim reaches it; every call returns 0 on success. on_screen lists this process's own
 * windows that are on screen. */
typedef struct {
    int (*set_level)(void *o, int window, int level);
    int (*get_level)(void *o, int window, int *level);
    int (*on_screen)(void *o, int *list, int max, int *count);
    int (*is_shield)(void *o, int window);
    void *o;
} PtDspServer;

typedef struct {
    int armed; /* DrawSprocket is loaded in this process */
    int shield, shield_level, sweeping, count;
    struct {
        int window, level; /* the level to give back */
    } raised[PT_DSP_WINDOWS];
} PtDsp;

/* The level `window` gets back at release is `level`; -1: no room to remember it (then it is not raised either). */
static inline int pt_dsp_remember(PtDsp *s, int window, int level, int replace) {
    int k = 0;
    while (k < s->count && s->raised[k].window != window)
        k++;
    if (k == s->count) {
        if (s->count == PT_DSP_WINDOWS)
            return -1; /* ponytail: 64 windows of one game; a list if a game ever has more */
        s->raised[s->count++].window = window;
        s->raised[k].level = level;
    } else if (replace)
        s->raised[k].level = level;
    return k;
}
/* Every window of this process that is on screen and below the shield's front goes above the shield. */
static inline void pt_dsp_sweep(PtDsp *s, const PtDspServer *v) {
    int list[128], n = 0;
    if (!s->shield || s->sweeping || v->on_screen(v->o, list, 128, &n))
        return;
    s->sweeping = 1;
    for (int i = 0; i < n; i++) {
        int level;
        if (list[i] == s->shield || v->get_level(v->o, list[i], &level) || level >= s->shield_level + PT_DSP_ABOVE)
            continue;
        if (pt_dsp_remember(s, list[i], level, 0) >= 0)
            v->set_level(v->o, list[i], s->shield_level + PT_DSP_ABOVE);
    }
    s->sweeping = 0;
}
/* The process sets a window's level (CGSSetWindowLevel). Under a shield its own windows stay above it, and the level
 * asked for is the one they get back. A shield's own level, once set, starts all this. */
static inline int pt_dsp_set_level(PtDsp *s, const PtDspServer *v, int window, int level) {
    if (!s->armed)
        return v->set_level(v->o, window, level);
    if (s->shield && window != s->shield && pt_dsp_remember(s, window, level, 1) >= 0 &&
        level < s->shield_level + PT_DSP_ABOVE)
        level = s->shield_level + PT_DSP_ABOVE;
    int e = v->set_level(v->o, window, level);
    if (!e && (window == s->shield || v->is_shield(v->o, window))) {
        s->shield = window;
        s->shield_level = level;
        pt_dsp_sweep(s, v);
    }
    return e;
}
/* After the process ordered windows (a new one may have come on screen). */
static inline void pt_dsp_ordered(PtDsp *s, const PtDspServer *v) {
    if (s->armed)
        pt_dsp_sweep(s, v);
}
/* The display was released: the shield is gone, every raised window gets its level back. Returns how many. */
static inline int pt_dsp_released(PtDsp *s, const PtDspServer *v) {
    int n = s->count;
    if (!s->shield)
        return 0;
    s->shield = 0;
    s->count = 0;
    for (int k = 0; k < n; k++)
        v->set_level(v->o, s->raised[k].window, s->raised[k].level);
    return n;
}
#endif
