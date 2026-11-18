/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "jit-test.h"
#include <fenv.h>
#include <math.h>
static uint64_t bits(double x) {
    uint64_t u;
    memcpy(&u, &x, 8);
    return u;
}
static double value(unsigned n) {
    const uint64_t edge[] = {0,
                             0x8000000000000000ull,
                             1,
                             0x8000000000000001ull,
                             0x0010000000000000ull,
                             0x3ff0000000000000ull,
                             0xbff0000000000000ull,
                             0x3ff0000010000000ull,
                             0x3ff0000000000001ull,
                             0x7fefffffffffffffull,
                             0xffefffffffffffffull,
                             0x7ff0000000000000ull,
                             0x7ff8000000000001ull,
                             0x7ff0000000000001ull};
    uint64_t u = n % 4 ? ((uint64_t)rnd() << 32 | rnd()) : edge[rnd() % (sizeof edge / sizeof *edge)];
    double d;
    memcpy(&d, &u, 8);
    return d;
}
/* Whole blocks in the window's FP configuration (hoisted guard, lazy FPSCR, FPR cache, links): the directed form sits
 * on the guard's slow path, leaves its block and the next block starts with the cache and the status from the CPU. */
enum { GUEST = 1 << 16, END = 0xf000 };
static uint8_t *memory;
static unsigned to_interp; /* exits for the interpreter's instruction */
#define AFP(sgl, xo, d, a, b, c) (((sgl) ? 59u : 63u) << 26 | (d) << 21 | (a) << 16 | (b) << 11 | (c) << 6 | (xo) << 1)
#define XFP(xo, d, b) (63u << 26 | (d) << 21 | (b) << 11 | (xo) << 1)
#define MTFSFI7(rn) (63u << 26 | 7u << 23 | (uint32_t)(rn) << 12 | 134u << 1)
#define STFD(s, a, i) (54u << 26 | (s) << 21 | (a) << 16 | (i))
#define LFD(d, a, i) (50u << 26 | (d) << 21 | (a) << 16 | (i))
#define BDNZ(to, at) (16u << 26 | 16u << 21 | (((to) - (at)) & 0xfffc))
#define B(to, at) (18u << 26 | (((to) - (at)) & 0x3fffffc))
static void run(CPU *c) { /* as jit_run: translate, link, fill the jump cache, step the interpreter where asked */
    uint32_t *t = NULL;
    int fill = 0;
    while (c->pc != END) {
        pthread_jit_write_protect_np(0);
        if (!t)
            t = block(c);
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
        } else if (why != X_LOOKUP && c->pc != END && !(why == X_FSF && jit_fsf_fail(c))) {
            to_interp += why == X_INTERP; /* not X_FSF: the single-format cache learning its entry values */
            assert(interp_step(c) == 0);
        }
    }
}
static unsigned program(const char *name, const uint32_t *ins, unsigned n, unsigned loops, uint32_t fpscr) {
    memset(memory, 0, GUEST);
    for (unsigned i = 0; i < n; i++)
        wbe32(memory + 0x1000 + 4 * i, ins[i]);
    CPU c = {0};
    c.pc = 0x1000;
    c.msr = MSR_FP;
    c.mem = c.vbase = memory;
    c.jit_jc = jc;
    c.npreg = 1;
    c.preg[0] = (PRegion){0, GUEST, memory, 0};
    for (unsigned i = 0; i < 16; i++)
        c.crtab[i] = cr_make(i >> 3 & 1, i >> 2 & 1, i >> 1 & 1, i & 1);
    c.ctr = loops;
    c.gpr[1] = 0x8000;
    c.fpscr = fpscr;
    const double v[] = {0x1.123456789abcdp+4, 1.0, 0x1p-54, 3.0, 0x1.0000001p+0, 1e6, -0x1.fffffep-3, 0.1};
    for (unsigned i = 0; i < 32; i++)
        c.fpr[i] = v[i % 8] * (1 + i / 8);
    c.fpr[30] = 0;
    CPU r = c;
    to_interp = 0;
    run(&c);
    ppc_fp_settle(&c);
    static uint8_t after[GUEST];
    memcpy(after, memory, GUEST);
    memset(memory + 0x8000, 0, 0x1000);
    while (r.pc != END)
        assert(interp_step(&r) == 0);
    ppc_fp_settle(&r);
    if (memcmp(c.fpr, r.fpr, sizeof c.fpr) || c.fpscr != r.fpscr || memcmp(after, memory, GUEST)) {
        fprintf(stderr, "directed blocks: %s differs from the interpreter (fpscr %08x/%08x)\n", name, c.fpscr, r.fpscr);
        for (unsigned i = 0; i < 32; i++)
            if (memcmp(&c.fpr[i], &r.fpr[i], 8))
                fprintf(stderr, "  f%u %016llx/%016llx\n", i, (unsigned long long)bits(c.fpr[i]), (unsigned long long)bits(r.fpr[i]));
        exit(1);
    }
    pthread_jit_write_protect_np(0);
    flush_all();
    pthread_jit_write_protect_np(1);
    return to_interp;
}
static void blocks(void) {
    uint32_t *code = mmap(NULL, 1 << 24, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    memory = mmap(NULL, GUEST, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(code != MAP_FAILED && memory != MAP_FAILED);
    nolink = 0;
    fp_old = 0;
    fp_directed_enabled = 1;
    fma_fast = fp_round_fast = fp_hoist_enabled = fp_lazy_enabled = fcache_enabled = fsf_enabled = 1;
    trampoline(code);
    buf = code;
    bufend = code + (1 << 22);
    jmsr = MSR_750 | MSR_FP;
    zbase = 0;
    maxblk = 128;
    settle_gen();
    flush_all();
    __builtin___clear_cache((char *)code, (char *)cp);
    pthread_jit_write_protect_np(1);
    /* the time conversion of the commpage: round toward zero around three operations, in a loop of nearest code */
    const uint32_t time[] = {MTFSFI7(1), AFP(0, 20, 3, 1, 2, 0), AFP(0, 25, 4, 3, 0, 5), XFP(15, 6, 4), STFD(6, 1, 0),
                             MTFSFI7(0), AFP(0, 21, 1, 1, 7, 0), AFP(0, 21, 8, 8, 4, 0), BDNZ(0x1000, 0x1020), B(END, 0x1024)};
    /* a chain under one mode set outside the loop: every form, double and single, values moving with the loop */
    static uint32_t chain[40];
    unsigned n = 0;
    const unsigned xo[] = {21, 20, 25, 18, 29, 28, 31, 30};
    for (unsigned k = 0; k < 16; k++)
        chain[n++] = AFP(k & 1, xo[k % 8], 9 + k % 6, 1 + k % 5, 3 + k % 7, 2 + k % 3);
    chain[n++] = XFP(12, 20, 9);
    chain[n++] = XFP(14, 21, 10);
    chain[n++] = STFD(21, 1, 8);
    chain[n++] = AFP(0, 21, 1, 1, 20, 0);
    chain[n++] = LFD(22, 1, 8);
    chain[n] = BDNZ(0x1000, 0x1000 + 4 * n);
    n++;
    chain[n] = B(END, 0x1000 + 4 * n);
    n++;
    unsigned on = program("time", time, 10, 300, 0), mode[4];
    for (uint32_t rn = 0; rn < 4; rn++)
        mode[rn] = program("chain", chain, n, 200, rn | (rn & 1 ? 0x82000000u : 0));
    /* special cases stay the interpreter's in every mode: 1/0, Inf - Inf, a NaN operand */
    const uint32_t special[] = {AFP(0, 18, 9, 1, 30, 0), AFP(0, 20, 10, 9, 9, 0), AFP(0, 21, 11, 10, 1, 0),
                                AFP(1, 25, 12, 11, 0, 3), AFP(0, 21, 1, 1, 7, 0), BDNZ(0x1000, 0x1014), B(END, 0x1018)};
    unsigned sp = 0;
    for (uint32_t rn = 0; rn < 4; rn++)
        sp += program("special", special, 7, 50, rn) >= 150;
    assert(sp == 4);
    /* RN = 0 on the same slow path: a block's first operation with a case the nearest form leaves (fadds of doubles) */
    const uint32_t mixed[] = {AFP(1, 21, 9, 1, 8, 0), AFP(0, 21, 1, 1, 9, 0), BDNZ(0x1000, 0x1008), B(END, 0x100c)};
    unsigned mx = program("mixed", mixed, 4, 100, 0);
    assert(mx >= 10);
    fp_directed_enabled = 0; /* the negative control: the same programs leave for the interpreter */
    unsigned off = program("time", time, 10, 300, 0), off3 = program("chain", chain, n, 200, 3);
    fp_directed_enabled = 1;
    printf("directed blocks: time conversion %u interpreter steps (%u without), chain %u/%u/%u/%u by RN (%u without at RN 3), RN 0 fadds of doubles %u\n",
           on, off, mode[0], mode[1], mode[2], mode[3], off3, mx);
    fflush(stdout);
    assert(on == 0 && off >= 600 && mode[1] + mode[2] + mode[3] == 0 && off3 >= 200 * 16);
}
int main(void) {
    uint32_t *code = mmap(NULL, 32768, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    assert(code != MAP_FAILED);
    uint8_t mem[8192] = {0};
    unsigned fast[4] = {0}, slow = 0, fr = 0;
    fp_directed_enabled = 1;
    nolink = 1;
    jsys = 0;
    fp_hoist = 0;
    fp_lazy_enabled = 0;
    const unsigned ops[] = {18, 20, 21, 25, 28, 29, 30, 31, 12};
    for (unsigned op = 0; op < 9; op++)
        for (unsigned sgl = 0; sgl < 2; sgl++)
            for (unsigned rn = 1; rn < 4; rn++) {
                if (op == 8 && !sgl)
                    continue;
                unsigned xo = ops[op];
                uint32_t in = (sgl && op != 8 ? 59u : 63u) << 26 | 3u << 21 | (op == 8 ? 0u : 1u) << 16 | 2u << 11 |
                              (xo == 25 || xo >= 28 ? 4u << 6 : 0) | xo << 1;
                for (unsigned n = 0; n < 2400; n++) {
                    CPU c = {0};
                    c.msr = MSR_FP;
                    c.pc = 0x1000;
                    c.npreg = 1;
                    c.preg[0].size = sizeof mem;
                    c.preg[0].host = mem;
                    for (unsigned g = 0; g < 32; g++)
                        c.fpr[g] = value(n);
                    if (n % 3 == 0) {
                        c.fpr[1] = 1.0;
                        c.fpr[2] = 0x1p-54;
                        c.fpr[4] = 0x1.0000001p+0;
                    }
                    c.fpscr = rn | (n % 17 == 0 ? 0x8 : 0) | (n % 19 == 0 ? 4 : 0) | (n % 2 ? 0x82000000 : 0);
                    wbe32(mem + 0x1000, in);
                    CPU expected = c;
                    assert(!interp_step(&expected));
                    CPU before = c;
                    run1(code, in, &c);
                    uint64_t fpcr;
                    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
                    assert(fpcr == 0);
                    if (c.jit_exit == X_INTERP) {
                        assert(!memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
                        slow++;
                        continue;
                    }
                    if (memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
                        fprintf(stderr,
                                "directed mismatch op=%u s=%u rn=%u a=%016llx b=%016llx c=%016llx fpscr=%08x result %016llx/%08x expected %016llx/%08x\n",
                                xo, sgl, rn, bits(before.fpr[1]), bits(before.fpr[2]), bits(before.fpr[4]),
                                before.fpscr, bits(c.fpr[3]), c.fpscr, bits(expected.fpr[3]), expected.fpscr);
                        return 1;
                    }
                    assert(c.pc == 0x2004);
                    fast[rn]++;
                    fr += !!(c.fpscr & 0x40000);
                }
            }
    assert(fast[1] > 5000 && fast[2] > 5000 && fast[3] > 5000 && fr > 1000);
    fp_directed_enabled = 0;
    CPU c = {0};
    c.msr = MSR_FP;
    c.fpscr = 1;
    c.fpr[1] = 1;
    c.fpr[2] = 0.1;
    run1(code, 63u << 26 | 3u << 21 | 1u << 16 | 2u << 11 | 21u << 1, &c);
    assert(c.jit_exit == X_INTERP);
    printf("PASS directed: RN1 %u RN2 %u RN3 %u; fallback %u; FR %u; FPCR restored; =0 fallback\n", fast[1], fast[2],
           fast[3], slow, fr);
    blocks();
}
