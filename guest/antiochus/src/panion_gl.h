/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANT_PANION_GL_H
#define ANT_PANION_GL_H
#include "panion.h"
#include <OpenGL/gl.h>
typedef struct { GLuint buffer[4],texture[2]; } PanGL;
int pan_gl_create(PanGL *g,const PanScene *s);
int pan_gl_draw(PanGL *g,const PanScene *s,unsigned frame,int width,int height);
void pan_gl_destroy(PanGL *g);
#endif
