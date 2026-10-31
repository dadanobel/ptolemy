/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's frsqrte and stfiwx (PT_FP_MORE) against interp_step, bit for bit: FPRs, the whole
 * FPSCR and memory. frsqrte inputs over the full double range (denormals, huge, +Inf, zeros, negatives, NaNs) and
 * FPSCR guard bits; the JIT may hand over only for b <= 0, NaN or a guarded FPSCR, leaving FPRs and FPSCR untouched.
 * stfiwx: rA = 0 and not, any FPR bit pattern. */
#include "../epicycle/jit.c"
#include <assert.h>
#include <math.h>
#include <pthread.h>
int sys_call(CPU *c) { (void)c; abort(); }
enum { MEMSZ = 1 << 16 };

static uint64_t rng = 0x2545f4914f6cdd1du;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static uint64_t rnd64(void) { return (uint64_t)rnd() << 32 | rnd(); }
static double dval(void) {
    uint32_t r = rnd(); uint64_t u; double x;
    switch (r & 7) {
    case 0: return (double[]){0.0, -0.0, 1.0, -1.0, INFINITY, -INFINITY, NAN, 0x1p-1074, 0x1p-1022, 0x1.fffffffffffffp+1023,
                              4.0, 0.25, 0x1.8p-1050, 2.0, 0x1p+1000, -0x1p-1074}[r >> 3 & 15];
    case 1: u = rnd64(); break;                                                     /* any bits, NaNs too */
    case 2: u = (rnd64() & 0xFFFFFFFFFFFFFull) >> (rnd() % 52); break;              /* denormals */
    default: u = (uint64_t)(900 + rnd() % 250) << 52 | (rnd64() & 0xFFFFFFFFFFFFFull); break;
    }
    memcpy(&x, &u, 8); return x;
}

int main(void)
{
    uint32_t *code = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *m1 = calloc(1, MEMSZ), *m2 = calloc(1, MEMSZ);
    assert(code != MAP_FAILED && m1 && m2);
    unsigned fast = 0, slow = 0, inexact = 0;
    for (unsigned n = 0; n < 1000000; n++) {                     /* frsqrte */
        unsigned d = rnd() & 31, b = rnd() & 31;
        uint32_t in = 63u << 26 | d << 21 | b << 11 | 26u << 1;
        CPU c = {0}; c.msr = MSR_FP; c.pc = 0x1000; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = MEMSZ; c.preg[0].host = m2;
        wbe32(m2 + 0x1000, in);
        for (unsigned r = 0; r < 32; r++) c.fpr[r] = dval();
        if (rnd() & 1) c.fpr[b] = fabs(c.fpr[b]);
        c.fpscr = (uint32_t[]){0, 0x82000000, 0x00020000, 0x00001000, 0x00004000, 0x02000000}[rnd() % 6] | (rnd() % 16 == 0 ? 1u << (rnd() % 8) : 0);
        CPU expected = c; assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        jmsr = MSR_750 | MSR_FP; jsys = 0; fp_more_enabled = 1;
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
        double x = before.fpr[b];
        int must_slow = !(x > 0) || (before.fpscr & 0xff);
        if (c.jit_exit == X_INTERP) {
            assert(must_slow && !memcmp(c.fpr, before.fpr, sizeof c.fpr) && c.fpscr == before.fpscr);
            slow++;
        } else {
            if (must_slow || memcmp(c.fpr, expected.fpr, sizeof c.fpr) || c.fpscr != expected.fpscr) {
                fprintf(stderr, "MISMATCH n=%u in=%08x b=%a fpscr %08x jit %a/%08x interp %a/%08x\n", n, in, x, before.fpscr,
                        c.fpr[d], c.fpscr, expected.fpr[d], expected.fpscr);
                return 1;
            }
            inexact += !!(c.fpscr & 0x00020000);
            fast++;
        }
    }
    assert(fast > 300000 && inexact > 100000);
    unsigned st = 0;
    for (unsigned n = 0; n < 20000; n++) {                       /* stfiwx, rA = 0 or r3, rB = r4 */
        unsigned s = rnd() & 31, a = n & 1 ? 3 : 0;
        uint32_t in = 31u << 26 | s << 21 | a << 16 | 4u << 11 | 983u << 1, ea = 0x2000 + rnd() % 0xC000;   /* any alignment */
        for (unsigned i = 0; i < MEMSZ; i++) m1[i] = m2[i] = (uint8_t)(i * 7 + n);
        wbe32(m1 + 0x1000, in); wbe32(m2 + 0x1000, in);
        CPU c = {0}; c.msr = MSR_FP; c.pc = 0x1000; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = MEMSZ;
        for (unsigned r = 0; r < 32; r++) { uint64_t u = rnd64(); memcpy(&c.fpr[r], &u, 8); }
        c.gpr[3] = 0x1000; c.gpr[4] = a ? ea - 0x1000 : ea;
        CPU expected = c; expected.preg[0].host = m2; assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        c.preg[0].host = m1;
        jmsr = MSR_750 | MSR_FP; jsys = 0; fp_more_enabled = 1; zbase = 0;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code;
        E(0xa9bf7bfb); E(0xa9bf77fc); E(0xaa0003fb); E(0xaa0103fc);    /* stp x27,x30; stp x28,x29; mov x27,x0; mov x28,x1 */
        ldrw(3, ENV, OGPR(3)); ldrw(4, ENV, OGPR(4));
        exit_common = code + 1024;
        assert(tr(in, 0x2000) == 0);
        b_to(exit_common);
        cp = exit_common; E(0xa8c177fc); E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 3));
        ((void (*)(CPU *, uint8_t *))(void *)code)(&c, m1);
        assert(c.jit_exit != X_INTERP);
        assert(!memcmp(m1, m2, MEMSZ) && !memcmp(c.fpr, expected.fpr, sizeof c.fpr) && c.fpscr == expected.fpscr);
        st++;
    }
    unsigned sd = 0;
    for (unsigned n = 0; n < 200000; n++) {                      /* stfs/stfsu in the JIT, single denormals too */
        unsigned s = rnd() & 31, a = n & 1 ? 3 : 0, op = a && n & 2 ? 53 : 52; uint32_t ea = 0x2000 + rnd() % 0x6000;   /* d < 0x8000 */
        uint32_t in = op << 26 | s << 21 | a << 16 | (a ? ea - 0x1000 : ea);
        for (unsigned i = 0x2000; i < 0xE004; i++) m1[i] = m2[i] = (uint8_t)(i * 7 + n);
        CPU c = {0}; c.msr = MSR_FP; c.pc = 0x1000; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = MEMSZ;
        for (unsigned r = 0; r < 32; r++) { uint64_t u = (rnd() & 3) ? (uint64_t)rnd() << 63 | (uint64_t)(850 + rnd() % 60) << 52 | (rnd64() & 0xFFFFFFFFFFFFFull)
                                                                     : rnd64(); memcpy(&c.fpr[r], &u, 8); }
        c.gpr[3] = 0x1000;
        wbe32(m2 + 0x1000, in); CPU expected = c; expected.preg[0].host = m2; assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        c.preg[0].host = m1;
        jmsr = MSR_750 | MSR_FP; jsys = 0; zbase = 0;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code;
        E(0xa9bf7bfb); E(0xa9bf77fc); E(0xaa0003fb); E(0xaa0103fc);
        ldrw(3, ENV, OGPR(3));
        exit_common = code + 1024;
        assert(tr(in, 0x2000) == 0);
        if (a) strw(3, ENV, OGPR(3));                            /* stfsu: r3 lives in x3 (GMAP) */
        b_to(exit_common);
        cp = exit_common; E(0xa8c177fc); E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 3));
        ((void (*)(CPU *, uint8_t *))(void *)code)(&c, m1);
        assert(c.jit_exit != X_INTERP);
        uint64_t u; memcpy(&u, &c.fpr[s], 8); sd += (u >> 52 & 0x7ff) <= 896 && (u << 1);
        assert(!memcmp(m1 + 0x2000, m2 + 0x2000, 0xC004) && c.gpr[3] == expected.gpr[3]);
    }
    assert(sd > 50000);
    cp = code; fp_more_enabled = 0;
    assert(tr(63u << 26 | 26u << 1, 0x2000) == -1 && cp == code);                 /* PT_FP_MORE=0 */
    assert(tr(31u << 26 | 983u << 1, 0x2000) == -1 && cp == code);
    fp_more_enabled = 1;
    assert(tr(63u << 26 | 26u << 1 | 1, 0x2000) == -1 && cp == code);             /* frsqrte.: interpreter */
    printf("FP more (PT_FP_MORE): frsqrte %u JIT (%u inexact) + %u interpreter cases, stfiwx %u stores, bit-exact "
           "(FPR + FPSCR + memory) vs interpreter, switch PASS; stfs %u denormal stores in the JIT.\n", fast, inexact, slow, st, sd);
    return 0;
}
