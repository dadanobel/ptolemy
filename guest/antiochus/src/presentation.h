/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANTIOCHUS_PRESENTATION_H
#define ANTIOCHUS_PRESENTATION_H
enum { ANT_SCREEN_W=512, ANT_SCREEN_H=256, ANT_SCREEN_FRAMES=60 };
unsigned ant_tile(unsigned x, unsigned y, unsigned frame);
void ant_screen_reference(unsigned char *rgba, unsigned frame);
#endif
