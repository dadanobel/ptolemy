/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <OpenGL/OpenGL.h>
int ant_view_open(const char *title, int width, int height);
void ant_view_note(const char *text);
void ant_view_frame(const unsigned char *rgba, int w, int h);
int ant_view_gl(int depth, CGLContextObj *ctx, CGLPixelFormatObj *pf);
void ant_view_present(void);
void ant_view_close(const char *verdict);
