/* SPDX-License-Identifier: GPL-2.0-or-later
 * A chain of AltiVec operations through the block translator in the window's configuration, as an FFT butterfly has
 * it: lvx lvx vmaddfp vnmsubfp vperm stvx in a loop. Against the interpreter (registers and memory), with lvx
 * storing its register in one piece and in two, and with the VR cache and without. The second loop is for the
 * cache: an accumulator carried by the loop, a result that is its own operand, a cached register written by
 * another operation (vxor), VSCR.NJ switched off and on inside the block. `bench`: nanoseconds an iteration on
 * this Mac - the stand for what a value costs on its way from one VMX operation to the next. */
#include "jit-test.h"
#include <time.h>
enum { GUEST = 1 << 20, END = 0xf000, A = 0x20000, B = 0x40000, OUT = 0x60000, OUT2 = 0x80000, OUT3 = 0xa0000, OUT4 = 0xc0000, N = 4000 };
#define VA(xo, d, a, b, c) (4u << 26 | (d) << 21 | (a) << 16 | (b) << 11 | (c) << 6 | (xo))
#define VX(xo, d, a, b) VA(xo, d, a, b, 0)
#define MEM(xo, v, a, b) (31u << 26 | (v) << 21 | (a) << 16 | (b) << 11 | (xo) << 1)
static uint8_t *memory, *reference;
static const uint32_t loop[] = {0x7c0348ce, 0x7c2448ce, 0x1040186e, 0x1080186f, 0x10a221ab, 0x7ca549ce, 0x39290010, 0x4200ffe4,
                                0x48000000u | (END - 0x1020)}; /* lvx v0,r3,r9; lvx v1,r4,r9; vmaddfp v2,v0,v1,v3;
                                vnmsubfp v4,v0,v1,v3; vperm v5,v2,v4,v6; stvx v5,r5,r9; addi r9,r9,16; bdnz; b END */
static uint32_t chain[] = {
    MEM(103, 0, 3, 9), MEM(103, 1, 4, 9), /* lvx v0; lvx v1 */
    VA(46, 13, 15, 9, 1), /* vmaddfp v13 = v15*v1 + 0: v15 has denormals, a product with 1e30 tells a flushed one */
    VA(46, 7, 0, 7, 1), /* vmaddfp v7 = v0*v1 + v7: carried by the loop, its own operand */
    VX(10, 8, 7, 0), /* vaddfp v8 = v7 + v0 */
    VX(1220, 0, 0, 6), /* vxor v0 ^= v6: a cached register written outside vmx_fp */
    VX(74, 2, 8, 0), /* vsubfp v2 = v8 - v0 */
    VX(1604, 0, 0, 9), /* mtvscr v9: NJ off - denormals count from here */
    VA(46, 14, 15, 9, 1), /* vmaddfp v14 = v15*v1 + 0: now with the denormals */
    VX(1034, 11, 2, 1), /* vmaxfp v11 = max(v2, v1) */
    VA(47, 2, 11, 0, 1), /* vnmsubfp v2 = -(v11*v1 - v0) */
    VX(1604, 0, 0, 10), /* mtvscr v10: NJ on */
    VX(1098, 12, 2, 11), /* vminfp v12 = min(v2, v11) */
    VX(1220, 13, 13, 14), /* vxor v13 ^= v14: both products bit for bit */
    0xfc21102a, 0xfc21102a, /* fadd f1,f1,f2 twice: the FPR cache takes its registers first */
    MEM(231, 7, 5, 9), MEM(231, 2, 6, 9), MEM(231, 12, 7, 9), MEM(231, 13, 8, 9), /* stvx v7, v2, v12, v13 */
    0x39290010, 0, 0}; /* addi r9,r9,16; bdnz; b END (main) */
enum { CHAIN = sizeof chain / 4 };
/* Geekbench's Dot Product (vector) in small: one vmaddfp a pass, its sum carried by the loop. */
static const uint32_t dot[] = {MEM(103, 0, 3, 9), MEM(103, 1, 4, 9), VA(46, 7, 0, 7, 1), 0x39290010, 0x4200fff0,
                               MEM(231, 7, 5, 9), 0x48000000u | (END - 0x1018)}; /* ...; stvx v7; b END */
static const struct {
    const char *name;
    const uint32_t *code;
    unsigned n, gain; /* gain: A64 instructions the VR cache takes off a pass of the loop, at least */
} programs[] = {{"the butterfly", loop, sizeof loop / 4, 20}, {"the chain", chain, CHAIN, 20}, {"the dot product", dot, sizeof dot / 4, 10}};
enum { PROGRAMS = sizeof programs / sizeof *programs };
static CPU initial(uint8_t *mem) {
    CPU c = {0};
    c.pc = 0x1000;
    c.mem = c.vbase = mem;
    c.msr = MSR_FP | MSR_VEC;
    c.spr[287] = 0x80030000u;
    c.jit_jc = jc;
    c.npreg = 1;
    c.preg[0] = (PRegion){0, GUEST, mem, 0};
    c.vscr = 0x10000; /* NJ, as Mac OS X runs */
    for (unsigned i = 0; i < 16; i++)
        c.crtab[i] = cr_make(i >> 3 & 1, i >> 2 & 1, i >> 1 & 1, i & 1);
    c.gpr[3] = A;
    c.gpr[4] = B;
    c.gpr[5] = OUT;
    c.gpr[6] = OUT2;
    c.gpr[7] = OUT3;
    c.gpr[8] = OUT4;
    c.vr[10][13] = 1; /* VSCR.NJ for mtvscr; v9 = 0 */
    static const uint32_t small[4] = {0x00000123, 0x80000456, 0x00800000, 0x007fffff}; /* denormals; the least
                                                         ordinary number: half of it is a denormal result */
    for (unsigned i = 0; i < 4; i++)
        wbe32(c.vr[15] + 4 * i, small[i]);
    c.ctr = N;
    for (unsigned i = 0; i < 4; i++) {
        float f = 0.25f + i;
        uint32_t w;
        memcpy(&w, &f, 4);
        wbe32(c.vr[3] + 4 * i, w);
    }
    for (unsigned i = 0; i < 16; i++)
        c.vr[6][i] = (uint8_t)(i * 7 + 3);
    c.fpr[1] = 1.0;
    c.fpr[2] = 0x1p-10;
    return c;
}
static unsigned steps, words; /* instructions left to the interpreter; a pass of the loop in A64 instructions */
static void run(CPU *c) { /* as jit_run: translate, link, fill the jump cache */
    uint32_t *t = NULL;
    for (int fill = 0; c->pc != END;) {
        pthread_jit_write_protect_np(0);
        if (!t) {
            int first = c->pc == 0x1000;
            t = block(c);
            if (first)
                words = (unsigned)(cp - loop_start); /* the block without its prologue */
        }
        assert(t);
        if (fill)
            jc_fill(c, t, 0);
        pthread_jit_write_protect_np(1);
        int why = enter(c, t);
        t = NULL;
        fill = why == X_LOOKUP;
        if (why == X_LINK) {
            uint32_t *site = (uint32_t *)c->jit_link;
            pthread_jit_write_protect_np(0);
            t = block(c);
            assert(t);
            b_at(site, t);
            __builtin___clear_cache((char *)site, (char *)(site + 1));
            pthread_jit_write_protect_np(1);
        } else if (why != X_LOOKUP && c->pc != END) {
            steps++;
            assert(interp_step(c) == 0);
        }
    }
}
static double once(int q, int cache) {
    vmx_lvx_q = q;
    vrc_enabled = cache;
    pthread_jit_write_protect_np(0);
    flush_all();
    pthread_jit_write_protect_np(1);
    CPU c = initial(memory);
    uint64_t t0 = jit_now();
    run(&c);
    return (double)(jit_now() - t0) / N;
}
int main(int argc, char **argv) {
    uint32_t *code = mmap(NULL, 1 << 22, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    memory = mmap(NULL, GUEST, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    reference = malloc(GUEST);
    assert(code != MAP_FAILED && memory != MAP_FAILED && reference);
    trampoline(code);
    buf = code;
    bufend = code + (1 << 20);
    jmsr = MSR_750 | MSR_FP | MSR_VEC;
    vm_vec = MSR_VEC;
    zbase = 0;
    maxblk = 128;
    fcache_enabled = 1;
    vmx_neon_enabled = vmx_mem_enabled = vmx_vscr_enabled = vmx_fp_enabled = vmx_fp2_enabled = vmx_int2_enabled = vmx_mhadd_enabled = 1;
    flush_all();
    __builtin___clear_cache((char *)code, (char *)cp);
    pthread_jit_write_protect_np(1);
    chain[CHAIN - 2] = 0x42000000u | (uint16_t)(-4 * (CHAIN - 2));
    chain[CHAIN - 1] = 0x48000000u | (END - 0x1000 - 4 * (CHAIN - 1));
    for (unsigned i = 0; i < 4 * N; i++) { /* ordinary values, then zeros, a denormal, infinities and NaNs */
        float a = 1.0f + (i % 977) * 0.001f, b = 0.5f - (i % 499) * 0.0003f;
        uint32_t wa, wb;
        memcpy(&wa, &a, 4);
        memcpy(&wb, &b, 4);
        if (i % 64 == 63 || i % 64 == 4)
            wb = 0x7149f2ca; /* 1e30: times a denormal it is an ordinary number */
        if (i % 64 == 63)
            wa = (uint32_t[]){0, 0x80000000u, 0x00000123, 0x7f800000, 0xff800000, 0x7fc01234, 0x7f812345, 0x00800000}[i / 64 % 8];
        wbe32(memory + A + 4 * i, wa);
        wbe32(memory + B + 4 * i, wb);
    }
    int bench = argc > 1 && !strcmp(argv[1], "bench");
    unsigned size[PROGRAMS][2];
    for (int p = 0; p < PROGRAMS; p++) {
        memset(memory + 0x1000, 0, 0x100);
        for (unsigned i = 0; i < programs[p].n; i++)
            wbe32(memory + 0x1000 + 4 * i, programs[p].code[i]);
        if (bench) {
            double best[2][2] = {{1e9, 1e9}, {1e9, 1e9}};
            for (int r = 0; r < 120; r++) {
                double t = once(r & 1, r >> 1 & 1);
                best[r >> 1 & 1][r & 1] = t < best[r >> 1 & 1][r & 1] ? t : best[r >> 1 & 1][r & 1];
            }
            printf("vmx-chain bench, %s: %.2f ns an iteration with the VR cache, %.2f without (%+.0f%%); lvx storing "
                   "in two: %.2f and %.2f\n",
                   programs[p].name, best[1][1], best[0][1], (best[1][1] / best[0][1] - 1) * 100, best[1][0],
                   best[0][0]);
            continue;
        }
        memset(memory + OUT, 0, GUEST - OUT);
        memcpy(reference, memory, GUEST);
        CPU r = initial(reference);
        while (r.pc != END)
            assert(interp_step(&r) == 0);
        for (int k = 0; k < 8; k++) { /* with the FPR cache taking registers first, and without */
            memset(memory + OUT, 0, GUEST - OUT);
            CPU c = initial(memory);
            vmx_lvx_q = k & 1;
            vrc_enabled = k >> 1 & 1;
            fcache_enabled = k >> 2;
            run(&c);
            assert(!memcmp(c.vr, r.vr, sizeof c.vr) && c.vscr == r.vscr && !memcmp(memory, reference, GUEST));
            assert(c.fpr[1] == r.fpr[1] && c.fpscr == r.fpscr);
            assert(steps == 0); /* every operation of the loop is translated */
            size[p][k >> 1 & 1] = words;
            pthread_jit_write_protect_np(0);
            flush_all();
            pthread_jit_write_protect_np(1);
        }
        assert(size[p][0] >= size[p][1] + programs[p].gain); /* the cache is at work, on the registers worth it */
    }
    if (!bench)
        printf("vmx-chain: %u passes of 3 loops equal to the interpreter (registers, VSCR, memory), lvx storing in one piece "
               "and in two, the VR cache and the FPR cache on and off; a pass in A64 instructions, without the VR cache "
               "and with: %u -> %u, %u -> %u, %u -> %u PASS\n",
               (unsigned)N, size[0][0], size[0][1], size[1][0], size[1][1], size[2][0], size[2][1]);
    return 0;
}
