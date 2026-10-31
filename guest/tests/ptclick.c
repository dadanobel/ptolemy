/* SPDX-License-Identifier: GPL-2.0-or-later
 * Test tool for Tiger (built with the guest devkit, run through Ptolemy's agent): posts mouse and
 * keyboard events into the console session, so a test can drive a graphical installer step by step.
 *   ptclick move X Y | click X Y | dclick X Y | drag X1 Y1 X2 Y2   (points, top-left origin)
 *   ptclick key CODE [cmd] [shift] [opt] [ctrl]                    (virtual key code, e.g. 36 Return, 53 Escape)
 *   ptclick type TEXT                                              (US layout: letters, digits, space, . - _ /) */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void button(CGPoint p, int down) { CGPostMouseEvent(p, 1, 1, (boolean_t)down); usleep(60000); }
static void key(CGKeyCode k, int down) { CGPostKeyboardEvent(0, k, (boolean_t)down); usleep(25000); }
static int code(char c, int *shift) {   /* US layout virtual key codes */
    static const char *const row = "asdfhgzxcv bqweryt123465=97-80]ou[ip lj'k;\\,/nm.";
    const char *at; *shift = 0;
    if (c >= 'A' && c <= 'Z') { *shift = 1; c = (char)(c - 'A' + 'a'); }
    if (c == '_') { *shift = 1; c = '-'; }
    if (c == ' ') return 49;
    if (c && (at = strchr(row, c)) && *at != ' ') return (int)(at - row);
    return -1;
}
int main(int argc, char **argv) {
    if (argc == 4 && (!strcmp(argv[1], "move") || !strcmp(argv[1], "click") || !strcmp(argv[1], "dclick"))) {
        CGPoint p = CGPointMake(atof(argv[2]), atof(argv[3]));
        CGPostMouseEvent(p, 1, 1, 0); usleep(100000);
        for (int n = !strcmp(argv[1], "move") ? 0 : !strcmp(argv[1], "click") ? 1 : 2; n > 0; n--) { button(p, 1); button(p, 0); }
        return 0;
    }
    if (argc == 6 && !strcmp(argv[1], "drag")) {
        CGPoint a = CGPointMake(atof(argv[2]), atof(argv[3])), b = CGPointMake(atof(argv[4]), atof(argv[5]));
        CGPostMouseEvent(a, 1, 1, 0); usleep(100000); button(a, 1);
        for (int i = 1; i <= 10; i++) { CGPostMouseEvent(CGPointMake(a.x + (b.x - a.x) * i / 10, a.y + (b.y - a.y) * i / 10), 1, 1, 1); usleep(30000); }
        button(b, 0); return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "key")) {
        static const struct { const char *name; CGKeyCode code; } mods[] = {{"cmd", 55}, {"shift", 56}, {"opt", 58}, {"ctrl", 59}};
        for (int i = 3; i < argc; i++) for (unsigned m = 0; m < 4; m++) if (!strcmp(argv[i], mods[m].name)) key(mods[m].code, 1);
        key((CGKeyCode)atoi(argv[2]), 1); key((CGKeyCode)atoi(argv[2]), 0);
        for (int i = argc - 1; i >= 3; i--) for (unsigned m = 0; m < 4; m++) if (!strcmp(argv[i], mods[m].name)) key(mods[m].code, 0);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "type")) {
        for (const char *s = argv[2]; *s; s++) {
            int shift, k = code(*s, &shift);
            if (k < 0) { fprintf(stderr, "ptclick: no key for '%c'\n", *s); return 1; }
            if (shift) key(56, 1);
            key((CGKeyCode)k, 1); key((CGKeyCode)k, 0);
            if (shift) key(56, 0);
        }
        return 0;
    }
    fprintf(stderr, "usage: ptclick move|click|dclick X Y | drag X1 Y1 X2 Y2 | key CODE [cmd shift opt ctrl] | type TEXT\n");
    return 2;
}
