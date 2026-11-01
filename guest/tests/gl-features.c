/* SPDX-License-Identifier: GPL-2.0-or-later */
/* gl-features: draws small scenes twice, with the accelerated renderer (Tiger's ATI driver on Ptolemy's Radeon model)
 * and with Apple's software renderer, reads both back and compares them. A pixel matches when the other image has a
 * pixel within the case's tolerance at the same place or one pixel away, so rasterisation rules that differ by a
 * pixel at an edge do not count. One line per case:
 *   <id> pass|FAIL|skip unmatched=N maxdiff=M lit=ATI/SW
 * Usage: gl-features [--out DIR] [case-prefix...]
 *   --out DIR writes DIR/<id>-ati.ppm and DIR/<id>-sw.ppm for every case run.
 * Build in Tiger: gcc -O2 -o gl-features gl-features.c -framework OpenGL */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_TEXTURE_FILTER_CONTROL_EXT
#define GL_TEXTURE_FILTER_CONTROL_EXT 0x8500
#define GL_TEXTURE_LOD_BIAS_EXT 0x8501
#endif
#ifndef GL_RGBA_FLOAT32_ATI
#define GL_RGBA_FLOAT32_ATI 0x8814
#define GL_RGBA_FLOAT16_ATI 0x881A
#endif
#ifndef kCGLRendererGenericFloatID
#define kCGLRendererGenericFloatID 0x00020400
#endif
#ifndef GL_POINT_SPRITE_ARB
#define GL_POINT_SPRITE_ARB 0x8861
#define GL_COORD_REPLACE_ARB 0x8862
#endif

enum { W = 128, H = 128 };
static CGLContextObj ctx[2];                  /* 0: accelerated, 1: software */
static unsigned char soft[W * H * 4], img[2][W * H * 4];
static float depth[2][W * H];
static int renderer;                          /* the context the scene is drawn into */

static void reset(void)
{
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DITHER); glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE); glDisable(GL_TEXTURE_2D); glDisable(GL_TEXTURE_3D);
    glDisable(GL_FOG); glDisable(GL_SCISSOR_TEST); glDisable(GL_COLOR_LOGIC_OP); glDisable(GL_LIGHTING);
    glDisable(GL_LINE_STIPPLE); glDisable(GL_POLYGON_STIPPLE); glDisable(GL_CLIP_PLANE0); glDisable(GL_CLIP_PLANE1);
    glShadeModel(GL_SMOOTH); glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); glLineWidth(1); glPointSize(1);
    glColorMask(1, 1, 1, 1); glDepthMask(1); glDepthFunc(GL_LESS);
    glClearColor(26 / 255.0f, 51 / 255.0f, 77 / 255.0f, 1); glClearDepth(1); glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

static void quad(float x0, float y0, float x1, float y1)
{
    glBegin(GL_QUADS); glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1); glEnd();
}

static void textured_quad(float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(s0, t0); glVertex2f(x0, y0); glTexCoord2f(s1, t0); glVertex2f(x1, y0);
    glTexCoord2f(s1, t1); glVertex2f(x1, y1); glTexCoord2f(s0, t1); glVertex2f(x0, y1);
    glEnd();
}

/* ---- Scenes ---- */

static void s_clear(void) { reset(); }
static void s_clear_alpha(void) { reset(); glClearColor(0.6f, 0.3f, 0.9f, 0.5f); glClear(GL_COLOR_BUFFER_BIT); }

static void s_smooth(void)
{
    reset();
    glBegin(GL_TRIANGLES);
    glColor3ub(250, 20, 40); glVertex2f(8, 8); glColor3ub(30, 240, 60); glVertex2f(120, 20); glColor3ub(40, 60, 230); glVertex2f(60, 120);
    glEnd();
}

static void s_depth(void)
{
    reset(); glEnable(GL_DEPTH_TEST);
    glBegin(GL_QUADS);
    glColor3ub(200, 60, 60); glVertex3f(10, 10, -0.5f); glVertex3f(100, 10, -0.5f); glVertex3f(100, 100, 0.5f); glVertex3f(10, 100, 0.5f);
    glColor3ub(60, 200, 60); glVertex3f(30, 30, 0.2f); glVertex3f(118, 30, 0.2f); glVertex3f(118, 118, 0.2f); glVertex3f(30, 118, 0.2f);
    glEnd();
}

static void receding_floor(GLenum mode, float a, float b)
{
    reset();
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-1, 1, -1, 1, 1, 60);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (mode) {   GLfloat red[4] = { 0.9f, 0.1f, 0.1f, 1 };
        glEnable(GL_FOG); glFogi(GL_FOG_MODE, mode); glFogfv(GL_FOG_COLOR, red); glHint(GL_FOG_HINT, GL_NICEST);
        if (mode == GL_LINEAR) { glFogf(GL_FOG_START, a); glFogf(GL_FOG_END, b); } else glFogf(GL_FOG_DENSITY, a); }
    {   /* a 24 x 24 grid: OpenGL lets fog be computed per vertex, and Tiger's driver does */
        int i, j; const int n = 24;
        for (j = 0; j < n; j++) {
            glBegin(GL_QUAD_STRIP);
            for (i = 0; i <= n; i++) {
                int k;
                for (k = 0; k < 2; k++) {
                    float v = (float)(j + k) / n, u = (float)i / n, w = 1 + 29 * v;
                    glColor3ub((GLubyte)(240 - 200 * v), (GLubyte)(240 - 20 * v), (GLubyte)(240 - 150 * v));
                    glVertex3f((2 * u - 1) * w, -1 + 1.5f * v, -1.2f - 53.8f * v);
                }
            }
            glEnd();
        }
    }
}
static void s_grid(void) { receding_floor(0, 0, 0); }
static void s_quad_strip(void)
{
    reset(); glBegin(GL_QUAD_STRIP);
    glColor3ub(240, 60, 60); glVertex2f(10, 20); glVertex2f(10, 100); glColor3ub(60, 240, 60); glVertex2f(60, 30); glVertex2f(60, 110);
    glColor3ub(60, 60, 240); glVertex2f(118, 10); glVertex2f(118, 90);
    glEnd();
}
static void s_fog_linear(void) { receding_floor(GL_LINEAR, 2, 40); }
static void s_fog_exp(void) { receding_floor(GL_EXP, 0.06f, 0); }
static void s_fog_exp2(void) { receding_floor(GL_EXP2, 0.05f, 0); }

static void s_clip_one(void)
{
    GLdouble plane[4] = { 1, 1, 0, -128 };    /* keeps x + y >= 128 */
    reset(); glClipPlane(GL_CLIP_PLANE0, plane); glEnable(GL_CLIP_PLANE0);
    glColor3ub(230, 200, 30); quad(4, 4, 124, 124);
}

static void s_clip_two(void)
{
    GLdouble p0[4] = { 1, 0, 0, -40 }, p1[4] = { 0, -1, 0, 90 };   /* x >= 40 and y <= 90 */
    reset(); glClipPlane(GL_CLIP_PLANE0, p0); glClipPlane(GL_CLIP_PLANE1, p1);
    glEnable(GL_CLIP_PLANE0); glEnable(GL_CLIP_PLANE1);
    glBegin(GL_TRIANGLES); glColor3ub(250, 80, 80); glVertex2f(4, 4); glColor3ub(80, 250, 80); glVertex2f(124, 30); glColor3ub(80, 80, 250); glVertex2f(50, 124); glEnd();
}

static void star(void)
{
    int i;
    glBegin(GL_LINES);
    for (i = 0; i < 12; i++) {
        float a = (float)i * 0.2618f;
        glColor3ub((GLubyte)(40 + 17 * i), (GLubyte)(250 - 15 * i), 120);
        glVertex2f(64 + 10 * cosf(a), 64 + 10 * sinf(a)); glVertex2f(64 + 56 * cosf(a), 64 + 56 * sinf(a));
    }
    glEnd();
}
static void s_line_1(void) { reset(); star(); }
static void s_line_3(void) { reset(); glLineWidth(3); star(); }
static void s_line_7(void) { reset(); glLineWidth(7); star(); }
static void s_line_strip(void)
{
    int i;
    reset(); glLineWidth(4); glColor3ub(240, 230, 60);
    glBegin(GL_LINE_STRIP); for (i = 0; i < 9; i++) glVertex2f(8 + 14 * i, i & 1 ? 110 : 20); glEnd();
}
static void s_line_stipple(void)
{
    reset(); glEnable(GL_LINE_STIPPLE); glLineStipple(2, 0x0F35); star();
}
static void s_line_stipple_wide(void)
{
    reset(); glEnable(GL_LINE_STIPPLE); glLineStipple(1, 0x00FF); glLineWidth(3); star();
}

static void dots(void)
{
    int i;
    glBegin(GL_POINTS);
    for (i = 0; i < 25; i++) { glColor3ub((GLubyte)(30 + 9 * i), 200, (GLubyte)(240 - 8 * i)); glVertex2f(12.5f + 25 * (i % 5), 12.5f + 25 * (i / 5)); }
    glEnd();
}
static void s_point_1(void) { reset(); dots(); }
static void s_point_6(void) { reset(); glPointSize(6); dots(); }
static void s_point_vertex_size(void)
{
    /* Per-vertex size from an ARB vertex program: 2 + the vertex's x / 10. */
    static const char vp[] =
        "!!ARBvp1.0\n"
        "PARAM mvp[4] = { state.matrix.mvp };\n"
        "DP4 result.position.x, mvp[0], vertex.position;\n"
        "DP4 result.position.y, mvp[1], vertex.position;\n"
        "DP4 result.position.z, mvp[2], vertex.position;\n"
        "DP4 result.position.w, mvp[3], vertex.position;\n"
        "MOV result.color, vertex.color;\n"
        "MAD result.pointsize.x, vertex.position.x, 0.1, 2;\n"
        "END\n";
    GLuint id;
    reset();
    glGenProgramsARB(1, &id); glBindProgramARB(GL_VERTEX_PROGRAM_ARB, id);
    glProgramStringARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, (GLsizei)strlen(vp), vp);
    glEnable(GL_VERTEX_PROGRAM_ARB); glEnable(GL_VERTEX_PROGRAM_POINT_SIZE_ARB);
    dots();
    glDisable(GL_VERTEX_PROGRAM_POINT_SIZE_ARB); glDisable(GL_VERTEX_PROGRAM_ARB); glDeleteProgramsARB(1, &id);
}

static GLuint checker_texture(int size)
{
    static unsigned char t[64 * 64 * 4];
    GLuint id; int x, y;
    for (y = 0; y < size; y++) for (x = 0; x < size; x++) {
        unsigned char *p = t + (y * size + x) * 4; int on = ((x / 4) ^ (y / 4)) & 1;
        p[0] = on ? 240 : 30; p[1] = (unsigned char)(x * 255 / size); p[2] = (unsigned char)(y * 255 / size); p[3] = 255;
    }
    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    return id;
}
static void s_point_sprite(void)
{
    GLuint id;
    reset(); id = checker_texture(16);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_POINT_SPRITE_ARB); glTexEnvi(GL_POINT_SPRITE_ARB, GL_COORD_REPLACE_ARB, GL_TRUE); glPointSize(16);
    dots();
    glDisable(GL_POINT_SPRITE_ARB); glDeleteTextures(1, &id);
}

static void s_flat(void)
{
    int i;
    reset(); glShadeModel(GL_FLAT);
    glBegin(GL_TRIANGLES);       /* provoking vertex: the last of each triangle */
    glColor3ub(250, 0, 0); glVertex2f(4, 4); glColor3ub(0, 250, 0); glVertex2f(60, 4); glColor3ub(0, 0, 250); glVertex2f(30, 40);
    glEnd();
    glBegin(GL_TRIANGLE_STRIP);  /* vertex i+2 */
    for (i = 0; i < 6; i++) { glColor3ub((GLubyte)(40 * i), (GLubyte)(250 - 40 * i), 128); glVertex2f(68 + 11 * i, i & 1 ? 40 : 4); }
    glEnd();
    glBegin(GL_TRIANGLE_FAN);    /* vertex i+2 */
    glColor3ub(255, 255, 255); glVertex2f(30, 84);
    for (i = 0; i < 5; i++) { glColor3ub((GLubyte)(50 * i), 120, (GLubyte)(250 - 50 * i)); glVertex2f(30 + 26 * cosf(i * 0.6f), 84 + 26 * sinf(i * 0.6f)); }
    glEnd();
    glBegin(GL_QUADS);           /* the last vertex of each quad */
    glColor3ub(250, 250, 0); glVertex2f(70, 60); glColor3ub(0, 250, 250); glVertex2f(100, 60); glColor3ub(250, 0, 250); glVertex2f(100, 90); glColor3ub(90, 160, 30); glVertex2f(70, 90);
    glEnd();
    glBegin(GL_POLYGON);         /* the first vertex */
    glColor3ub(200, 100, 30); glVertex2f(104, 96); glColor3ub(30, 100, 200); glVertex2f(124, 100); glColor3ub(100, 200, 30); glVertex2f(120, 124); glColor3ub(0, 0, 0); glVertex2f(100, 120);
    glEnd();
}

static void fan_of_triangles(void)
{
    glBegin(GL_TRIANGLES);
    glColor3ub(250, 60, 60); glVertex2f(10, 10); glVertex2f(60, 14); glVertex2f(30, 60);        /* counter-clockwise */
    glColor3ub(60, 250, 60); glVertex2f(70, 10); glVertex2f(90, 60); glVertex2f(120, 14);       /* clockwise */
    glColor3ub(60, 60, 250); glVertex2f(20, 70); glVertex2f(110, 72); glVertex2f(64, 120);
    glEnd();
}
static void s_polygon_line(void) { reset(); glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); fan_of_triangles(); }
static void s_polygon_point(void) { reset(); glPointSize(3); glPolygonMode(GL_FRONT_AND_BACK, GL_POINT); fan_of_triangles(); }
static void s_polygon_front_line(void)
{
    reset(); glPolygonMode(GL_FRONT, GL_LINE); glPolygonMode(GL_BACK, GL_FILL); fan_of_triangles();
}

static void s_polygon_stipple(void)
{
    GLubyte pattern[128]; int i;
    for (i = 0; i < 128; i++) pattern[i] = (GLubyte)((i / 4) & 1 ? 0xF0 : 0x3C) ^ (GLubyte)i;
    reset(); glPolygonStipple(pattern); glEnable(GL_POLYGON_STIPPLE);
    glColor3ub(250, 200, 50); quad(3, 5, 125, 122);
}

static void logic(GLenum op)
{
    reset();
    glColor3ub(0xC3, 0x5A, 0x0F); quad(10, 10, 90, 90);
    glEnable(GL_COLOR_LOGIC_OP); glLogicOp(op);
    glColor3ub(0x96, 0x3C, 0xF0); quad(40, 40, 118, 118);
}
static void s_logic_xor(void) { logic(GL_XOR); }
static void s_logic_and(void) { logic(GL_AND); }
static void s_logic_or(void) { logic(GL_OR); }
static void s_logic_invert(void) { logic(GL_INVERT); }
static void s_logic_copy_inverted(void) { logic(GL_COPY_INVERTED); }
static void s_logic_equiv(void) { logic(GL_EQUIV); }

static GLuint level_texture(void)
{
    /* Each mip level one colour, so the chosen level is visible. */
    static const unsigned char colour[7][4] = { {250,40,40,255}, {40,250,40,255}, {40,40,250,255}, {250,250,40,255},
                                                {40,250,250,255}, {250,40,250,255}, {250,250,250,255} };
    static unsigned char t[64 * 64 * 4];
    GLuint id; int level, size, i;
    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    for (level = 0, size = 64; size; level++, size /= 2) {
        for (i = 0; i < size * size; i++) memcpy(t + i * 4, colour[level], 4);
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}
static void lod(float bias)
{
    GLuint id;
    reset(); id = level_texture(); glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, bias);
    textured_quad(0, 0, 64, 64, 0, 0, 4, 4);         /* 4 texels per pixel: level 2 */
    textured_quad(64, 0, 128, 64, 0, 0, 1, 1);       /* level 0 */
    textured_quad(0, 64, 64, 128, 0, 0, 8, 8);       /* level 3 */
    textured_quad(64, 64, 128, 128, 0, 0, 2, 2);     /* level 1 */
    glTexEnvf(GL_TEXTURE_FILTER_CONTROL_EXT, GL_TEXTURE_LOD_BIAS_EXT, 0);
    glDeleteTextures(1, &id);
}
static void s_lod_bias_0(void) { lod(0); }
static void s_lod_bias_plus(void) { lod(1.0f); }
static void s_lod_bias_minus(void) { lod(-1.0f); }

static void s_border(void)
{
    GLfloat border[4] = { 0.2f, 0.8f, 0.4f, 0.6f };
    GLuint id;
    reset(); id = checker_texture(16);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    textured_quad(4, 4, 124, 124, -0.5f, -0.5f, 1.5f, 1.5f);
    glDeleteTextures(1, &id);
}

static void s_texture_3d(void)
{
    static unsigned char t[8 * 8 * 4 * 4];
    GLuint id; int i;
    for (i = 0; i < 8 * 8 * 4; i++) { int z = i / 64; t[i * 4] = (unsigned char)(60 * z + 40); t[i * 4 + 1] = (unsigned char)(i * 7); t[i * 4 + 2] = (unsigned char)(250 - 60 * z); t[i * 4 + 3] = 255; }
    reset();
    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_3D, id);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 8, 8, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    glEnable(GL_TEXTURE_3D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    for (i = 0; i < 4; i++) {
        float x0 = (float)(i % 2) * 64, y0 = (float)(i / 2) * 64, r = (i + 0.5f) / 4;
        glBegin(GL_QUADS);
        glTexCoord3f(0, 0, r); glVertex2f(x0, y0); glTexCoord3f(1, 0, r); glVertex2f(x0 + 64, y0);
        glTexCoord3f(1, 1, r); glVertex2f(x0 + 64, y0 + 64); glTexCoord3f(0, 1, r); glVertex2f(x0, y0 + 64);
        glEnd();
    }
    glDisable(GL_TEXTURE_3D); glDeleteTextures(1, &id);
}

static void float_texture(GLenum format)
{
    static float t[16 * 16 * 4];
    GLuint id; int i;
    for (i = 0; i < 16 * 16; i++) { t[i * 4] = (float)(i % 16) / 15.0f; t[i * 4 + 1] = (float)(i / 16) / 15.0f * 2.0f; t[i * 4 + 2] = 0.25f; t[i * 4 + 3] = 1; }
    reset();
    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, format, 16, 16, 0, GL_RGBA, GL_FLOAT, t);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    textured_quad(0, 0, 128, 128, 0, 0, 1, 1);
    glDeleteTextures(1, &id);
}
static void s_texture_float32(void) { float_texture(GL_RGBA_FLOAT32_ATI); }
static void s_texture_float16(void) { float_texture(GL_RGBA_FLOAT16_ATI); }

static void s_copy_texture(void)
{
    /* glCopyTexSubImage2D from the frame, then the copy drawn back over the other half. */
    GLuint id;
    s_smooth();
    glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 32, 32, 64, 64);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    textured_quad(64, 0, 128, 64, 0, 0, 1, 1);
    glDisable(GL_TEXTURE_2D); glDeleteTextures(1, &id);
}

/* ---- Cases ---- */

/* Expected images drawn on the CPU, for features Apple's software renderer does not have (per-vertex point size from a
 * vertex program, point sprites): squares of the rounded size centred on each point, as OpenGL rasterises points. */
static void expect_dots(unsigned char *out, int sprite)
{
    int i, x, y, k;
    for (i = 0; i < W * H; i++) { out[i * 4] = 26; out[i * 4 + 1] = 51; out[i * 4 + 2] = 77; out[i * 4 + 3] = 255; }
    for (i = 0; i < 25; i++) {
        float cx = 12.5f + 25 * (i % 5), cy = 12.5f + 25 * (i / 5), size = sprite ? 16 : (float)(int)(2 + 0.1f * cx + 0.5f);
        unsigned char c[4]; c[0] = (unsigned char)(30 + 9 * i); c[1] = 200; c[2] = (unsigned char)(240 - 8 * i); c[3] = 255;
        for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            if (px < cx - size / 2 || px >= cx + size / 2 || py < cy - size / 2 || py >= cy + size / 2) continue;
            if (sprite) {   /* the checker texture: s across, t up from the bottom. Unverified: ARB_point_sprite puts t = 0 at
                             * the top; Tiger's driver programs the R300's sprite coordinates with t = 0 at the bottom. */
                int tx = (int)(px - (cx - 8)), ty = (int)(py - (cy - 8)), on = ((tx / 4) ^ (ty / 4)) & 1;
                c[0] = on ? 240 : 30; c[1] = (unsigned char)(tx * 255 / 16); c[2] = (unsigned char)(ty * 255 / 16);
            }
            for (k = 0; k < 4; k++) out[(y * W + x) * 4 + k] = c[k];
        }
    }
}
static void expect_vertex_size(unsigned char *out) { expect_dots(out, 0); }
static void expect_border(unsigned char *out)
{
    int x, y;
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        unsigned char *p = out + (y * W + x) * 4;
        float s = -0.5f + 2 * (x + 0.5f - 4) / 120, t = -0.5f + 2 * (y + 0.5f - 4) / 120;
        if (x < 4 || x >= 124 || y < 4 || y >= 124) { p[0] = 26; p[1] = 51; p[2] = 77; p[3] = 255; continue; }
        if (s < 0 || s >= 1 || t < 0 || t >= 1) { p[0] = 51; p[1] = 204; p[2] = 102; p[3] = 153; continue; }
        {   int tx = (int)(s * 16), ty = (int)(t * 16), on = ((tx / 4) ^ (ty / 4)) & 1;
            p[0] = on ? 240 : 30; p[1] = (unsigned char)(tx * 255 / 16); p[2] = (unsigned char)(ty * 255 / 16); p[3] = 255; }
    }
}
static void expect_sprite(unsigned char *out) { expect_dots(out, 1); }

typedef struct { const char *id; void (*draw)(void); int tol; unsigned allow; int depth; const char *needs; void (*expect)(unsigned char *); } Case;
static const Case cases[] = {
    { "readback.clear", s_clear, 0, 0, 0, NULL },
    { "readback.clear-alpha", s_clear_alpha, 1, 0, 0, NULL },
    { "readback.smooth", s_smooth, 3, 0, 0, NULL },
    { "readback.depth", s_depth, 3, 0, 1, NULL },
    { "geometry.quad-strip", s_quad_strip, 3, 0, 0, NULL },
    { "geometry.grid", s_grid, 3, 0, 0, NULL },
    { "fog.linear", s_fog_linear, 6, 0, 0, NULL },
    { "fog.exp", s_fog_exp, 6, 0, 0, NULL },
    { "fog.exp2", s_fog_exp2, 6, 0, 0, NULL },
    { "clip.one-plane", s_clip_one, 2, 0, 0, NULL },
    { "clip.two-planes", s_clip_two, 3, 0, 0, NULL },
    { "line.width-1", s_line_1, 3, 0, 0, NULL },
    { "line.width-3", s_line_3, 3, 24, 0, NULL },      /* allow: the ends of wide lines (OpenGL leaves their shape open) */
    { "line.width-7", s_line_7, 3, 48, 0, NULL },
    { "line.strip-4", s_line_strip, 3, 0, 0, NULL },
    { "line.stipple", s_line_stipple, 3, 0, 0, NULL },
    { "line.stipple-wide", s_line_stipple_wide, 3, 24, 0, NULL },   /* pattern phase at the ends of the wide segments */
    { "point.size-1", s_point_1, 3, 0, 0, NULL },
    { "point.size-6", s_point_6, 3, 0, 0, NULL },
    { "point.vertex-size", s_point_vertex_size, 3, 0, 0, "GL_ARB_vertex_program", expect_vertex_size },
    { "point.sprite", s_point_sprite, 3, 0, 0, "GL_ARB_point_sprite", expect_sprite },
    { "shade.flat", s_flat, 0, 0, 0, NULL },
    { "polygon.mode-line", s_polygon_line, 3, 0, 0, NULL },
    { "polygon.mode-point", s_polygon_point, 3, 0, 0, NULL },
    { "polygon.mode-front-line", s_polygon_front_line, 3, 0, 0, NULL },
    { "polygon.stipple", s_polygon_stipple, 0, 0, 0, NULL },
    { "logic.xor", s_logic_xor, 0, 0, 0, NULL },
    { "logic.and", s_logic_and, 0, 0, 0, NULL },
    { "logic.or", s_logic_or, 0, 0, 0, NULL },
    { "logic.invert", s_logic_invert, 0, 0, 0, NULL },
    { "logic.copy-inverted", s_logic_copy_inverted, 0, 0, 0, NULL },
    { "logic.equiv", s_logic_equiv, 0, 0, 0, NULL },
    { "texture.lod-bias-0", s_lod_bias_0, 0, 0, 0, "GL_EXT_texture_lod_bias" },
    { "texture.lod-bias-plus", s_lod_bias_plus, 0, 0, 0, "GL_EXT_texture_lod_bias" },
    { "texture.lod-bias-minus", s_lod_bias_minus, 0, 0, 0, "GL_EXT_texture_lod_bias" },
    { "texture.border-colour", s_border, 1, 0, 0, "GL_ARB_texture_border_clamp", expect_border },
    { "texture.3d", s_texture_3d, 0, 0, 0, NULL },
    { "texture.float32", s_texture_float32, 2, 0, 0, "GL_ATI_texture_float" },
    { "texture.float16", s_texture_float16, 2, 0, 0, "GL_ATI_texture_float" },
    { "texture.copy-subimage", s_copy_texture, 3, 0, 0, NULL },
};

static int has_extension(const char *name)
{
    const char *all = (const char *)glGetString(GL_EXTENSIONS), *p = all; size_t n = strlen(name);
    while (all && (p = strstr(p, name))) { if ((p == all || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return 1; p += n; }
    return 0;
}

/* Pixels of a with no pixel of b within tol at the same place or one pixel away; *maxd: the largest difference at the
 * same place. */
static unsigned unmatched(const unsigned char *a, const unsigned char *b, int tol, int *maxd)
{
    unsigned bad = 0; int x, y, dx, dy, c;
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        const unsigned char *p = a + (y * W + x) * 4; int best = 1000;
        for (dy = -1; dy <= 1 && best > tol; dy++) for (dx = -1; dx <= 1 && best > tol; dx++) {
            int d = 0; const unsigned char *q;
            if (x + dx < 0 || x + dx >= W || y + dy < 0 || y + dy >= H) continue;
            q = b + ((y + dy) * W + x + dx) * 4;
            for (c = 0; c < 4; c++) { int e = abs((int)p[c] - (int)q[c]); if (e > d) d = e; }
            if (!dx && !dy && d > *maxd) *maxd = d;
            if (d < best) best = d;
        }
        bad += best > tol;
    }
    return bad;
}
static unsigned depth_unmatched(const float *a, const float *b)
{
    unsigned bad = 0; int x, y, dx, dy;
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        int ok = 0;
        for (dy = -1; dy <= 1 && !ok; dy++) for (dx = -1; dx <= 1 && !ok; dx++)
            if (x + dx >= 0 && x + dx < W && y + dy >= 0 && y + dy < H) ok = fabsf(a[y * W + x] - b[(y + dy) * W + x + dx]) <= 0.002f;
        bad += !ok;
    }
    return bad;
}
static unsigned lit(const unsigned char *a)
{
    unsigned n = 0, i;
    for (i = 0; i < W * H; i++) n += abs(a[i * 4] - 26) > 3 || abs(a[i * 4 + 1] - 51) > 3 || abs(a[i * 4 + 2] - 77) > 3;
    return n;
}
static void ppm(const char *dir, const char *id, const char *who, const unsigned char *a)
{
    char path[512]; FILE *f; int x, y;
    snprintf(path, sizeof path, "%s/%s-%s.ppm", dir, id, who);
    if (!(f = fopen(path, "wb"))) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (y = H - 1; y >= 0; y--) for (x = 0; x < W; x++) fwrite(a + (y * W + x) * 4, 1, 3, f);
    fclose(f);
}

static int make_contexts(void)
{
    CGLPixelFormatAttribute accelerated[] = { kCGLPFAAccelerated, kCGLPFAPBuffer, kCGLPFAColorSize, 24, kCGLPFAAlphaSize, 8,
                                              kCGLPFADepthSize, 24, kCGLPFAStencilSize, 8, 0 };
    CGLPixelFormatAttribute software[] = { kCGLPFARendererID, kCGLRendererGenericFloatID, kCGLPFAOffScreen, kCGLPFAColorSize, 32, kCGLPFAAlphaSize, 8,
                                           kCGLPFADepthSize, 24, kCGLPFAStencilSize, 8, 0 };
    CGLPixelFormatObj pf; CGLPBufferObj pbuffer; long n, screen = 0;   /* long: the Mac OS X 10.4 CGL signatures */
    if (CGLChoosePixelFormat(accelerated, &pf, &n) || !pf) { puts("setup: no accelerated pbuffer format"); return 0; }
    if (CGLCreateContext(pf, NULL, &ctx[0]) || CGLCreatePBuffer(W, H, GL_TEXTURE_2D, GL_RGBA, 0, &pbuffer)) { puts("setup: accelerated context"); return 0; }
    CGLDestroyPixelFormat(pf); CGLGetVirtualScreen(ctx[0], &screen);
    if (CGLSetPBuffer(ctx[0], pbuffer, 0, 0, screen)) { puts("setup: pbuffer"); return 0; }
    if (CGLChoosePixelFormat(software, &pf, &n) || !pf) { puts("setup: no software format"); return 0; }
    if (CGLCreateContext(pf, NULL, &ctx[1]) || CGLSetOffScreen(ctx[1], W, H, W * 4, soft)) { puts("setup: software context"); return 0; }
    CGLDestroyPixelFormat(pf);
    return 1;
}

int main(int argc, char **argv)
{
    const char *out = NULL; int first = 1, failures = 0, i, k, r;
    if (argc > 2 && !strcmp(argv[1], "--out")) { out = argv[2]; first = 3; }
    if (!make_contexts()) return 2;
    for (r = 0; r < 2; r++) { CGLSetCurrentContext(ctx[r]); printf("renderer %d: %s | %s\n", r, (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION)); }
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        const Case *c = &cases[i]; int selected = first >= argc, missing = 0, maxd = 0; unsigned bad; GLenum error[2];
        for (k = first; k < argc; k++) if (!strncmp(c->id, argv[k], strlen(argv[k]))) selected = 1;
        if (!selected) continue;
        for (r = 0; r < (c->expect ? 1 : 2); r++) { CGLSetCurrentContext(ctx[r]); if (c->needs && !has_extension(c->needs)) missing |= 1 << r; }
        if (missing) { printf("%s skip %s missing in%s%s\n", c->id, c->needs, missing & 1 ? " accelerated" : "", missing & 2 ? " software" : ""); continue; }
        error[1] = 0; if (c->expect) c->expect(img[1]);
        for (r = 0; r < (c->expect ? 1 : 2); r++) {
            renderer = r; CGLSetCurrentContext(ctx[r]);
            c->draw(); glFinish();
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, img[r]);
            if (c->depth) glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, depth[r]);
            error[r] = glGetError();
        }
        bad = unmatched(img[0], img[1], c->tol, &maxd) + unmatched(img[1], img[0], c->tol, &maxd);
        if (c->depth) bad += depth_unmatched(depth[0], depth[1]) + depth_unmatched(depth[1], depth[0]);
        if (c->depth && bad) printf("%s depth ATI/SW at (2,2) %g/%g (64,64) %g/%g (110,110) %g/%g\n", c->id, depth[0][2 * W + 2], depth[1][2 * W + 2],
            depth[0][64 * W + 64], depth[1][64 * W + 64], depth[0][110 * W + 110], depth[1][110 * W + 110]);
        k = bad > c->allow || error[0] || error[1];
        failures += k;
        printf("%s %s unmatched=%u maxdiff=%d lit=%u/%u", c->id, k ? "FAIL" : "pass", bad, maxd, lit(img[0]), lit(img[1]));
        if (error[0] || error[1]) printf(" glerror=0x%x/0x%x", (unsigned)error[0], (unsigned)error[1]);
        putchar('\n'); fflush(stdout);
        if (out) { ppm(out, c->id, "ati", img[0]); ppm(out, c->id, "sw", img[1]); }
    }
    printf("failures %d\n", failures);
    return failures > 100 ? 100 : failures;
}
