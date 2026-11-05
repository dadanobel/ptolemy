/* SPDX-License-Identifier: GPL-2.0-or-later
 * The hot FP loops of Geekbench 2 (Dot Product, LU Decomposition, Mandelbrot) and Xbench 1.3 (CPU
 * "Floating Point Basic", with the +-Inf and zero data it reaches after its first passes), as their PPC code, through
 * the real block translator in the window's FP configuration. Default: every kernel against the interpreter (state
 * and memory). --bench: ns per iteration on this Mac (median of 7 samples of 10 runs). No Tiger, HVF or GPU is involved. */
#include "jit-test.h"
#include <math.h>
#pragma STDC FP_CONTRACT OFF
static uint8_t *memory, *reference;
static uint32_t *code;
static void setup(int cache) {
    code = mmap(NULL, 1 << 22, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    memory = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    reference = calloc(1, 1 << 20);
    assert(code != MAP_FAILED && memory != MAP_FAILED && reference);
    trampoline(code);
    buf = code;
    bufend = code + (1 << 20);
    jmsr = MSR_750 | MSR_FP | MSR_VEC;
    vm_vec = MSR_VEC;
    zbase = 0;
    maxblk = 128;
    jsys = 0;
    check = 0;
    nolink = 0;
    /* the window's defaults (firmware.c): FMA_FAST ROUND_FAST HOIST LAZY MORE; cache as asked */
    fp_old = 0;
    fma_fast = 1;
    fp_round_fast = 1;
    fp_hoist_enabled = 1;
    fp_lazy_enabled = 1;
    fp_more_enabled = 1;
    fp_moves_enabled = env_on("PT_FP_MOVES");
    fp_directed_enabled = env_on("PT_FP_DIRECTED");
    fcache_enabled = cache;
    fma_single_direct = 0;
    fp_mixed = 0;
    cr_fuse = 0;
    fp_fr_on = 1;
    fsf_enabled = !getenv("PT_FP_SGL") || atoi(getenv("PT_FP_SGL")); /* the window's default: on */
    settle_gen();
    __builtin___clear_cache((char *)code, (char *)cp);
    pthread_jit_write_protect_np(1);
}
static CPU initial(void) {
    CPU c = {0};
    c.pc = 0x1000;
    c.mem = c.vbase = memory;
    c.msr = MSR_FP | MSR_VEC;
    c.spr[287] = 0x80030000u;
    c.jit_jc = jc;
    c.npreg = 1;
    c.preg[0] = (PRegion){0, 1 << 20, memory, 0};
    c.fpscr = 0x82000000u; /* XX: any program after its first inexact op */
    for (unsigned i = 0; i < 16; i++)
        c.crtab[i] = cr_make(i >> 3 & 1, i >> 2 & 1, i >> 1 & 1, i & 1);
    return c;
}
static void putf(uint32_t a, float v) {
    uint32_t w;
    memcpy(&w, &v, 4);
    wbe32(memory + a, w);
}
static void putd(uint32_t a, double v) {
    uint64_t w;
    memcpy(&w, &v, 8);
    wbe32(memory + a, (uint32_t)(w >> 32));
    wbe32(memory + a + 4, (uint32_t)w);
}
typedef struct {
    const char *name;
    const uint32_t *ins;
    unsigned n;
} Kernel;
static const uint32_t K_DOT[] = {0x7dab142e, 0x7c09142e, 0x38420004, 0xed8d603a, 0x4200fff0};
static const uint32_t K_LU[] = {0x80470000, 0x81230000, 0x7d8814ae, 0x7da84cae, 0x7c02ecae,
                                0xfc00637c, 0x7c0815ae, 0x39080008, 0x4200ffe0};
static const uint32_t K_MANDEL[] = {0xfc0a02b2, 0x38420001, 0xfc0b02f8, 0xfd68002a, 0xfd8b582a, 0xfdab4028, 0xfd4a4b3a,
                                    0xfc0a4828, 0xfc000032, 0xfdad037a, 0xff8d3800, 0x4fddf382, 0x419e0008, 0x4200ffcc};
static const uint32_t K_FPB[] = {0x7c020378, 0xc1820008, 0xc162000c, 0xec1ff02a, 0x38020010, 0xedac582a,
                                 0xec0007f2, 0xedac0372, 0xed5f07ba, 0xd0020000, 0xc3c20014, 0xc3e20010,
                                 0xed8c6afa, 0xd1a20008, 0xd1420004, 0xd182000c, 0x4200ffc0};
static const Kernel kernels[] = {
        {"gb2-dot", K_DOT, 5},           {"gb2-lu", K_LU, 9},
        {"gb2-mandel", K_MANDEL, 14},    {"xb-fpbasic-inf", K_FPB, 17},
        {"xb-fpbasic-zero", K_FPB, 17},  {"xb-fpbasic-normal", K_FPB, 17},
        {"gb2-dot-double-sum", K_DOT, 5}}; /* a sum not exactly single at entry (single-format cache) */
static CPU prepare(unsigned k, uint32_t iters) { /* the kernel's data and registers (Geekbench / Xbench layouts) */
    CPU c = initial();
    memset(memory, 0, 1 << 20);
    const Kernel *x = &kernels[k];
    for (unsigned i = 0; i < x->n; i++)
        wbe32(memory + 0x1000 + 4 * i, x->ins[i]);
    if (k == 6)
        c.fpr[12] = 0.1;
    if (k == 0 || k == 6) {
        c.gpr[11] = 0x10000;
        c.gpr[9] = 0x50000;
        for (unsigned i = 0; i < 60000; i++) {
            putf(0x10000 + 4 * i, 1.0f + (i % 977) * 0.001f);
            putf(0x50000 + 4 * i, 0.5f - (i % 499) * 0.0003f);
        }
    }
    if (k == 1) {
        c.gpr[7] = 0x8000;
        c.gpr[3] = 0x8004;
        c.gpr[29] = 8;
        c.gpr[8] = 16;
        wbe32(memory + 0x8000, 0x10000);
        wbe32(memory + 0x8004, 0x60000);
        for (unsigned i = 0; i < 40000; i++) {
            putd(0x10000 + 8 * i, 1.0 + (i % 997) * 1e-3);
            putd(0x60000 + 8 * i, 0.25 + (i % 991) * 3e-4);
        }
    }
    if (k == 2) {
        c.fpr[7] = 4.0;
        c.fpr[8] = -0.12;
        c.fpr[9] = 0.11;
        c.fpr[10] = 0.05;
        c.fpr[11] = 0.04;
    }
    if (k >= 3 && k <= 5) {
        c.gpr[0] = 0x10000;
        for (unsigned i = 0; i < 4 * (iters + 1) && 0x10000 + 4 * i < (1u << 20); i++)
            putf(0x10000 + 4 * i, k == 3 ? INFINITY : k == 4 ? 0.0f : -(float)(1 + (i * 7919) % 1999) / 1000.0f);
        c.fpr[31] = k == 3 ? INFINITY : k == 4 ? 0.0 : -0.001;
        c.fpr[30] = c.fpr[31];
        putf(0x10000, (float)c.fpr[31]);
        putf(0x10004, (float)c.fpr[30]);
    }
    c.ctr = iters;
    return c;
}
static CPU run(CPU c, unsigned n) { /* as jit_run: links, interpreter steps, until the illegal word after the loop */
    uint32_t end = 0x1000 + 4 * n;
    pthread_jit_write_protect_np(0);
    uint32_t *t = block(&c);
    pthread_jit_write_protect_np(1);
    while (c.pc != end) {
        int why = enter(&c, t);
        if (why == X_LINK) {
            pthread_jit_write_protect_np(0);
            uint32_t *site = (uint32_t *)c.jit_link;
            t = block(&c);
            b_at(site, t);
            __builtin___clear_cache((char *)site, (char *)(site + 1));
            pthread_jit_write_protect_np(1);
            continue;
        }
        if (why == X_INTERP && c.pc != end)
            assert(interp_step(&c) == 0);
        if (why == X_FSF) {
            pthread_jit_write_protect_np(0);
            int k = jit_fsf_fail(&c);
            pthread_jit_write_protect_np(1);
            if (!k && c.pc != end)
                assert(interp_step(&c) == 0);
        } /* entry value not single */
        if (c.pc == end)
            break;
        pthread_jit_write_protect_np(0);
        t = block(&c);
        pthread_jit_write_protect_np(1);
    }
    return c;
}
static void reset(void) {
    pthread_jit_write_protect_np(0);
    kir = kpr = 0;
    flush_all();
    pthread_jit_write_protect_np(1);
    memset(nofsf, 0, sizeof nofsf);
    memset(nofsf_n, 0, sizeof nofsf_n); /* each kernel learns again */
}
/* Arithmetic-only scalar baseline: same data, BE layout and operation order as the PPC loops.
 * No FPSCR bookkeeping; this measures a lower bound, not equivalent CPU semantics. */
static volatile double sink;
static double getd(uint32_t a) {
    uint64_t u = (uint64_t)be32(memory + a) << 32 | be32(memory + a + 4);
    double v;
    memcpy(&v, &u, 8);
    return v;
}
static float getf(uint32_t a) {
    uint32_t u = be32(memory + a);
    float v;
    memcpy(&v, &u, 4);
    return v;
}
__attribute__((noinline)) static CPU native_loop(unsigned k, CPU c, uint32_t count) {
    if (k == 0) {
        float sum = (float)c.fpr[12];
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (uint32_t i = 0; i < count; i++)
            sum = fmaf(getf(c.gpr[11] + 4 * i), getf(c.gpr[9] + 4 * i), sum);
        c.fpr[12] = (double)sum;
    } else if (k == 1) {
        uint32_t r = be32(memory + c.gpr[7]), q = be32(memory + c.gpr[3]), off = c.gpr[8];
        double pivot = getd(r + c.gpr[29]);
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (uint32_t i = 0; i < count; i++) {
            c.fpr[12] = getd(r + off + 8 * i);
            c.fpr[13] = getd(q + off + 8 * i);
            c.fpr[0] = -fma(pivot, c.fpr[13], -c.fpr[12]);
            putd(r + off + 8 * i, c.fpr[0]);
        }
    } else {
        uint32_t i;
#pragma clang loop vectorize(disable) interleave(disable) unroll(disable)
        for (i = 0; i < count; i++) {
            c.fpr[0] = c.fpr[10] * c.fpr[10];
            c.fpr[0] = fma(c.fpr[11], c.fpr[11], -c.fpr[0]);
            c.fpr[11] = c.fpr[8] + c.fpr[0];
            c.fpr[12] = c.fpr[11] + c.fpr[11];
            c.fpr[13] = c.fpr[11] - c.fpr[8];
            c.fpr[10] = fma(c.fpr[10], c.fpr[12], c.fpr[9]);
            c.fpr[0] = c.fpr[10] - c.fpr[9];
            c.fpr[0] = c.fpr[0] * c.fpr[0];
            c.fpr[13] = fma(c.fpr[13], c.fpr[13], c.fpr[0]);
            if (c.fpr[13] >= c.fpr[7]) {
                i++;
                break;
            }
        }
        count = i;
    }
    c.icount = count;
    return c;
}
static void native_check(void) {
    for (unsigned k = 0; k < 3; k++)
        for (unsigned escape = 0; escape <= (k == 2); escape++) {
            CPU c = prepare(k, 3000);
            if (escape)
                c.fpr[7] = -1;
            memcpy(reference, memory, 1 << 20);
            CPU n = native_loop(k, c, 3000), r = c;
            r.mem = r.vbase = reference;
            r.preg[0].host = reference;
            uint32_t end = 0x1000 + 4 * kernels[k].n;
            while (r.pc != end)
                assert(!interp_step(&r));
            assert(n.icount == 3000 - r.ctr + (k == 2 && r.ctr != 0));
            unsigned first = k == 0 ? 12 : k == 1 ? 0 : 10, last = k == 0 ? 12 : k == 1 ? 0 : 13;
            for (unsigned f = first; f <= last; f++)
                assert(!memcmp(&n.fpr[f], &r.fpr[f], 8));
            assert(!memcmp(memory, reference, 1 << 20));
        }
    CPU c = prepare(0, 60000);
    (void)c;
    assert(getf(0x10000 + 4 * 59999) != 0 && getf(0x50000 + 4 * 59999) != 0);
    puts("fp-kernels: native arithmetic and memory equal interpreter; full Dot input PASS");
}
static double native(unsigned k) {
    double t[7];
    uint32_t count = k == 0 ? 60000 : k == 1 ? 39000 : 2000000;
    for (unsigned j = 0; j < 7; j++) {
        uint64_t ns = 0, n = 0;
        for (unsigned rep = 0; rep < 10; rep++) {
            CPU c = prepare(k, count);
            uint64_t t0 = jit_now();
            CPU r = native_loop(k, c, count);
            ns += jit_now() - t0;
            n += r.icount;
            sink = r.fpr[k == 0 ? 12 : k == 1 ? 0 : 10];
        }
        t[j] = (double)ns / n;
    }
    for (unsigned i = 0; i < 7; i++)
        for (unsigned j = i + 1; j < 7; j++)
            if (t[j] < t[i]) {
                double v = t[i];
                t[i] = t[j];
                t[j] = v;
            }
    return t[3];
}
int main(int argc, char **argv) {
    puts("fp-kernels: workload-v2 (complete input, scalar reference, identical arithmetic)");
    if (argc > 1 && !strcmp(argv[1], "--native")) {
        setup(1);
        native_check();
        static const char *const name[3] = {"gb2-dot", "gb2-lu", "gb2-mandel"};
        for (unsigned k = 0; k < 3; k++)
            printf("fp-kernels: %-18s %7.2f ns/iteration native-scalar-no-FPSCR (median of 7)\n", name[k], native(k));
        return 0;
    }
    int bench = argc > 1 && !strcmp(argv[1], "--bench"), cache = !(argc > 2 && !strcmp(argv[2], "--no-cache"));
    setup(cache);
    if (!bench)
        native_check();
    if (bench) { /* about 0.3 s of work first: the first timed kernel otherwise sees a core still ramping up */
        uint64_t t0 = jit_now();
        while (jit_now() - t0 < 300000000u) {
            CPU c = prepare(2, 200000);
            reset();
            (void)run(c, kernels[2].n);
        }
    }
    for (unsigned k = 0; k < sizeof kernels / sizeof *kernels; k++) {
        const Kernel *x = &kernels[k];
        if (!bench) {
            CPU c = prepare(k, 3000);
            memcpy(reference, memory, 1 << 20);
            CPU r = c;
            r.mem = r.vbase = reference;
            r.preg[0].host = reference;
            reset();
            c = run(c, x->n);
            while (r.pc != c.pc)
                assert(interp_step(&r) == 0);
            ppc_fp_settle(&c);
            if (diff(&c, &r, 0, c.pc) || memcmp(memory, reference, 1 << 20)) {
                fprintf(stderr, "fp-kernels: %s differs from the interpreter\n", x->name);
                return 1;
            }
            continue;
        }
        /* Iterations within each kernel's data (Dot: 60000 floats, LU: 40K doubles, FP Basic: 16 bytes each); the
         * Mandelbrot loop may leave early (escape), so time is divided by the iterations actually run (CTR). */
        double t[7];
        uint32_t iters = k == 0 ? 60000 : k == 1 ? 39000 : k == 2 ? 2000000 : 60000;
        for (unsigned i = 0; i < 7; i++) {
            uint64_t ns = 0, done = 0;
            for (unsigned r = 0; r < 10; r++) {
                CPU c = prepare(k, iters);
                if (!i && !r) {
                    reset();
                    pthread_jit_write_protect_np(0);
                    (void)block(&c);
                    pthread_jit_write_protect_np(1);
                } /* translated before timing */
                uint64_t t0 = jit_now();
                CPU e = run(c, x->n);
                ns += jit_now() - t0;
                done += iters - e.ctr + (k == 2 && e.ctr != 0); /* escape precedes bdnz */
            }
            t[i] = (double)ns / (double)(done ? done : 1);
        }
        for (unsigned i = 0; i < 7; i++)
            for (unsigned j = i + 1; j < 7; j++)
                if (t[j] < t[i]) {
                    double v = t[i];
                    t[i] = t[j];
                    t[j] = v;
                }
        printf("fp-kernels: %-18s %7.2f ns/iteration (median of 7, min %.2f, max %.2f)%s\n", x->name, t[3], t[0], t[6],
               cache ? "" : " no FPR cache");
    }
    if (!bench)
        printf("fp-kernels: %u kernels equal to the interpreter (state, FPSCR, memory) PASS\n",
               (unsigned)(sizeof kernels / sizeof *kernels));
    return 0;
}
