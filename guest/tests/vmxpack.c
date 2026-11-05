/* SPDX-License-Identifier: GPL-2.0-or-later */
/* vmxpack: the AltiVec sequence of Tiger's libGLImage for RGBA8 -> ARGB1555 (vrlw by 24: RGBA -> ARGB; vrlb by 1 on the
 * alpha byte: its high bit to the low bit; vpkpx), checked texel for texel against plain C. One line:
 *   vmxpack: N texels, M differ (K in odd texels)
 * Build in Tiger: gcc -O2 -faltivec -o vmxpack vmxpack.c */
#include <stdio.h>

int main(void) {
    static unsigned char in[32 * 64] __attribute__((aligned(16)));
    static unsigned short out[8 * 64] __attribute__((aligned(16)));
    vector unsigned int w24 = (vector unsigned int)(24), a, b;
    vector unsigned char r1 = (vector unsigned char)(1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0);
    unsigned seed = 5, i, bad = 0, odd = 0;
    for (i = 0; i < sizeof in; i++) {
        seed = seed * 1664525u + 1013904223u;
        in[i] = (unsigned char)(seed >> 24);
    }
    for (i = 0; i < sizeof in / 32; i++) {
        a = vec_ld(32 * i, (unsigned int *)in);
        b = vec_ld(32 * i + 16, (unsigned int *)in);
        a = vec_rl(a, w24);
        b = vec_rl(b, w24);
        a = (vector unsigned int)vec_rl((vector unsigned char)a, r1);
        b = (vector unsigned int)vec_rl((vector unsigned char)b, r1);
        vec_st(vec_packpx(a, b), 16 * i, out);
    }
    for (i = 0; i < sizeof in / 4; i++) {
        const unsigned char *p = in + 4 * i;
        unsigned e = (unsigned)(p[3] >> 7) << 15 | (unsigned)(p[0] >> 3) << 10 | (unsigned)(p[1] >> 3) << 5 | p[2] >> 3;
        if (out[i] == e)
            continue;
        bad++;
        odd += i & 1;
        if (bad <= 4)
            printf("texel %u bytes %02x %02x %02x %02x: %04x, expected %04x\n", i, p[0], p[1], p[2], p[3], out[i], e);
    }
    printf("vmxpack: %u texels, %u differ (%u in odd texels)\n", (unsigned)(sizeof in / 4), bad, odd);
    return bad != 0;
}
