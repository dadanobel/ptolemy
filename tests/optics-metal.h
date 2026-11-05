/* SPDX-License-Identifier: GPL-2.0-or-later
 * Shared by the Metal executor tests: the product executor m and one triangle draw dr over an n x n target. */
#include "frontend.h"
#include <assert.h>
#include <unistd.h>
static const OpticsExecutor *m;
static PtR2ShaderIR pvs, us;
static OpticsDraw dr;
static float vert[3][OPTICS_DRAW_INPUTS][4];
static uint32_t idx[3] = {0, 1, 2};
/* dr: a triangle over the n x n target with the 12-word US program fs (pvs_id 1: CPU vertex stage, window coordinates
 * as given, no divide, no viewport). m: the product executor; the device gets no instance, each test owns one. */
static inline void triangle(unsigned n, const uint32_t *fs, uint32_t us_id) {
    PtR2ShaderError e;
    assert(optics_draw_passthrough(2, &pvs) &&
           pt_r2_shader_decode(PT_R2_SHADER_US, PT_R2_SHADER_FP32_FTZ0, fs, 12, 0, &us, &e) == PT_R2_SHADER_OK);
    dr.pvs = &pvs;
    dr.us = &us;
    dr.pvs_id = 1;
    dr.us_id = us_id;
    dr.vi.abi = dr.fi.abi = PT_R2_SHADER_ABI;
    dr.vi.input_count = OPTICS_DRAW_INPUTS;
    dr.vi.output_count = 2;
    dr.vi.constant_count = 256;
    dr.fi.input_count = 1;
    dr.fi.output_count = 4;
    dr.fi.constant_count = 32;
    dr.width = dr.height = n;
    dr.cb_pitch = n;
    dr.scissor[2] = dr.scissor[3] = n;
    dr.vte = 0x300;
    dr.inputs = OPTICS_DRAW_INPUTS;
    dr.vertices = 3;
    dr.vertex = vert;
    dr.indices = 3;
    dr.index = idx;
    const float pos[3][2] = {{0, 0}, {2 * n, 0}, {0, 2 * n}};
    for (int v = 0; v < 3; v++) {
        vert[v][0][0] = pos[v][0];
        vert[v][0][1] = pos[v][1];
        vert[v][0][3] = 1;
        vert[v][1][3] = 1;
    }
    dr.position_output = 0;
    dr.color_output = 0;
    dr.varyings = 1;
    dr.pvs_output[0] = 1;
    dr.us_input[0] = 0;
    for (int c = 0; c < 4; c++)
        dr.component[0][c] = (uint8_t)c;
    m = optics_metal_executor();
    optics_frontend_set_executor(NULL);
}
/* Waits for the GPU, then [at, at+n) is written back and read into out. */
static inline void finish(void *x, PtRadeon *d, uint32_t at, uint8_t *out, unsigned n) {
    for (int i = 0; m->fence(x, d) != OPTICS_FENCE_DONE; i++) {
        assert(i < 5000);
        usleep(1000);
    }
    assert(!m->materialize(x, d, at, at + n, 0));
    assert(!pt_radeon_vram(d, at, out, n, 0));
}
