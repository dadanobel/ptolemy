/* SPDX-License-Identifier: GPL-2.0-or-later */
/* US inputs enter the fragment ALU as fp24, truncated like the driver's constants: a pass-through program returns
 * 38.2f as 0x4218cc80 (fp24 0x443199, what Leopard's driver wrote for the uniform 38.2, u200 #199), in the
 * interpreter and in the emitted MSL. */
#include "shader_ir.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static float seen[4];
static bool sample(void *context, unsigned unit, unsigned op, const float c[4], float out[4]) {
    (void)context, (void)unit, (void)op;
    memcpy(seen, c, sizeof seen);
    memset(out, 0, 4 * sizeof *out);
    return true;
}
int main(void) {
    const uint32_t fs[] = {
            0, 0, 0, 0, 0, 0, 0x400000, 0, 7u << 26, 1u << 24, (21u << 7) | (20u << 14), 9u | (17u << 7) | (16u << 14)};
    static PtR2ShaderIR us;
    static PtR2ShaderIO io;
    PtR2ShaderError e;
    assert(pt_r2_shader_decode(PT_R2_SHADER_US, PT_R2_SHADER_FP32_SEPARATE, fs, 12, 0, &us, &e) == PT_R2_SHADER_OK);
    PtR2ShaderInterface fi = {0};
    fi.abi = PT_R2_SHADER_ABI;
    fi.input_count = 1;
    fi.output_count = 4;
    fi.constant_count = 32;
    const float in[4] = {38.2f, 0.25f, -1.5f, 1.0f};
    memcpy(io.input[0], in, sizeof in);
    assert(pt_r2_shader_run(&us, &fi, &io, NULL, NULL, &e) == PT_R2_SHADER_OK);
    uint32_t b;
    memcpy(&b, &io.output[0][0], 4);
    assert(b == 0x4218cc80u);
    assert(io.output[0][1] == 0.25f && io.output[0][2] == -1.5f && io.output[0][3] == 1.0f);
    static char text[1 << 16];
    size_t need = 0;
    assert(pt_r2_shader_msl_body(&us, &fi, text, sizeof text, &need, &e) == PT_R2_SHADER_OK);
    assert(strstr(text, "as_type<uint4>(io.input[0]) & 0xffffff80u"));
    /* A TEX on an input no ALU step wrote reads it whole (c47): a texture coordinate keeps fp32. */
    const uint32_t ft[] = {8, 0, 0, 0, 0, 0, 0x400000, (1u << 15) | (1u << 6), (7u << 26) | 1u, (1u << 24) | 1u,
                           (21u << 7) | (20u << 14), 9u | (17u << 7) | (16u << 14)}; /* as optics-lines-blend */
    static PtR2ShaderIR tex;
    assert(pt_r2_shader_decode(PT_R2_SHADER_US, PT_R2_SHADER_FP32_SEPARATE, ft, 12, 0, &tex, &e) == PT_R2_SHADER_OK);
    fi.texture_mask = 1;
    memcpy(io.input[0], in, sizeof in);
    assert(pt_r2_shader_run(&tex, &fi, &io, sample, NULL, &e) == PT_R2_SHADER_OK);
    memcpy(&b, &seen[0], 4);
    assert(b == 0x4218cccdu); /* 38.2f, not its fp24 0x4218cc80 */
    assert(pt_r2_shader_msl_body(&tex, &fi, text, sizeof text, &need, &e) == PT_R2_SHADER_OK);
    assert(strstr(text, "pt_in(io.input[0])"));
    puts("optics-us-fp24: ok");
    return 0;
}
