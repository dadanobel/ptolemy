/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's fmadd(s)/fmsub(s)/fnmadd(s)/fnmsub(s) (PT_FMA_FAST path) against interp_step: FPR result and
 * the whole FPSCR, bit for bit. Single inputs over the full range (tiny products, cancellation, huge addends,
 * single-precision midpoints with and without an error term, zeros, infinities, denormals), non-single inputs
 * (FPSR path) and FPSCR guard bits. A hand-over to the interpreter must leave FPRs and FPSCR untouched. */
#include "../epicycle/jit.c"
#include <assert.h>
#include <math.h>
#include <pthread.h>
int sys_call(CPU *c) { (void)c; abort(); }

static uint64_t rng = 0x2545f4914f6cdd1du;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float sf(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t single_bits(void) {
    uint32_t r = rnd(), sign = r & 0x80000000;
    switch (r & 7) {
    case 0: return (uint32_t[]){0, 0x80000000, 0x3f800000, 0x7f800000, 0xff800000, 0x00000001, 0x007fffff, 0x00800000}[r >> 3 & 7];
    case 1: return sign | (uint32_t)(1 + (r >> 8) % 30) << 23 | (rnd() & 0x7fffff);       /* tiny normals */
    case 2: return sign | (uint32_t)(220 + (r >> 8) % 34) << 23 | (rnd() & 0x7fffff);     /* huge normals */
    default: return sign | (uint32_t)(100 + (r >> 8) % 56) << 23 | (rnd() & 0x7fffff);
    }
}
static double sd(int dbl) {
    double x = (double)sf(single_bits());
    if (dbl && x != 0 && isfinite(x) && rnd() & 1) { uint64_t u; memcpy(&u, &x, 8); u ^= (uint64_t)rnd() & 0x1fffffff; memcpy(&x, &u, 8); }
    return x;                                                   /* doubles: low 29 mantissa bits too */
}

int main(void)
{
    uint32_t *code = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *mem = calloc(1, 1 << 16);
    assert(code != MAP_FAILED && mem);
    unsigned fast = 0, slow = 0, n = 0;
    for (unsigned direct=0;direct<2;direct++) for (n=0; n < 800000; n++) {
        unsigned xo5 = 28 + (n & 3), d = rnd() & 31, a = rnd() & 31, b = rnd() & 31, k = rnd() & 31, kind = n >> 2 & 7;
        if ((kind == 1 || kind == 2 || kind == 3 || kind == 6) && (a == k || b == a || b == k)) kind = 0;
        unsigned dbl = n >> 5 & 1;
        uint32_t in = (dbl ? 63u : 59u) << 26 | d << 21 | a << 16 | b << 11 | k << 6 | xo5 << 1;
        CPU c = {0}; c.msr = MSR_FP; c.pc = 0x1000; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = 1 << 16; c.preg[0].host = mem;
        wbe32(mem + 0x1000, in);
        for (unsigned r = 0; r < 32; r++) c.fpr[r] = sd(dbl);
        if (kind == 1) {             /* tiny product + huge addend (Geekbench Blur) */
            c.fpr[a] = 81.0; c.fpr[k] = (double)sf(0x02008000); c.fpr[b] = (double)sf(0x7edb6af3);
        } else if (kind == 2) {      /* single midpoint with an error term */
            uint32_t yb = (rnd() & 0x80000000) | (uint32_t)(60 + rnd() % 120) << 23 | (rnd() & 0x7fffff); int ey = (int)(yb >> 23 & 0xff) - 127;
            c.fpr[b] = (double)sf(yb); c.fpr[a] = ldexp(1.0 + 0x1p-23, ey - 24); c.fpr[k] = (yb >> 31) ^ (xo5 & 1 ? 0 : 1) ? -(1.0 - 0x1p-24) : 1.0 - 0x1p-24;
        } else if (kind == 3) {      /* exact cancellation: a*c = b */
            c.fpr[a] = 3.0; c.fpr[k] = 0.5; c.fpr[b] = xo5 & 1 ? -1.5 : 1.5;
        } else if (kind == 4) c.fpr[a] = 1.0 + 0x1p-40;            /* not a single: FPSR path */
        else if (kind == 6) { c.fpr[a] = 0.75; c.fpr[b] = 0x1p-20; c.fpr[k] = rnd() & 1 ? INFINITY : -INFINITY; }   /* Inf input: exact Inf */
        c.fpscr = (uint32_t[]){0, 0x82000000, 0x00020000, 0x00001000}[rnd() & 3] | (kind == 5 ? 1u << (rnd() % 8) : 0);
        CPU expected = c; assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        jmsr = MSR_750 | MSR_FP; jsys = 0; fp_old = 0; fma_fast = 1;fma_single_direct=(int)direct;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code; E(0xa9bf7bfb); E(0xaa0003fb);
        exit_common = code + 2048;
        assert(tr(in, 0x2000) == 0);
        b_to(exit_common);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        CPU before = c;
        ((void (*)(CPU *))(void *)code)(&c);
        if (c.jit_exit == X_INTERP) {
            assert(!memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
            assert(!(kind == 1 && !dbl) && kind != 3 && kind != 6);  /* the cases these paths exist for stay in the JIT */
            slow++;
        } else {
            if (memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
                fprintf(stderr, "MISMATCH direct=%u n=%u in=%08x kind=%u a=%a b=%a c=%a fpscr %08x jit %a/%08x interp %a/%08x\n", direct, n, in, kind,
                        before.fpr[a], before.fpr[b], before.fpr[k], before.fpscr, c.fpr[d], c.fpscr, expected.fpr[d], expected.fpscr);
                return 1;
            }
            fast++;
        }
    }
    printf("FP fmadd(s) family (PT_FMA_FAST): %u JIT + %u interpreter cases bit-exact (FPR + FPSCR) vs interpreter PASS.\n", fast, slow);
    return 0;
}
