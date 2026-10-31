/* SPDX-License-Identifier: GPL-2.0-or-later
 * Scalar FP results and the whole FPSCR (FR, FI, FPRF, sticky bits, summaries) and CR1 against an
 * independent oracle: tests/fp-oracle.py (exact rational arithmetic, Book I rules) -> tests/fp-fr-vectors.h.
 * The interpreter runs everywhere; on Apple Silicon also the real JIT emitter, one instruction at a time, in each
 * FP mode (a hand-over to the interpreter must leave the state untouched). Negative control: with FR disabled
 * (fp_fr_on = 0) exactly the vectors expecting FR = 1 must fail. */
#if defined(__aarch64__) && defined(__APPLE__)
#define FR_JIT 1
#include "../epicycle/jit.c"
#include <pthread.h>
#else
#include "ppc.h"
extern int fp_fr_on;
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fp-fr-vectors.h"
int sys_call(CPU *c) { (void)c; abort(); }

enum { NV = sizeof fp_vectors / sizeof *fp_vectors };
static uint8_t vmem[1 << 16];
static const uint64_t SENTINEL = 0x123456789abcdef0ull;

static CPU setup(const FpVector *v) {
    CPU c; memset(&c, 0, sizeof c);
    c.msr = MSR_FP; c.pc = 0x1000; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = sizeof vmem; c.preg[0].host = vmem;
    wbe32(vmem + 0x1000, v->in);
    c.fpscr = v->fpscr;
    memcpy(&c.fpr[1], &SENTINEL, 8); memcpy(&c.fpr[2], &v->a, 8); memcpy(&c.fpr[3], &v->b, 8); memcpy(&c.fpr[4], &v->c, 8);
    cr_set_field(&c, 1, 0);
    return c;
}
static int verify(const CPU *c, const FpVector *v, const char *who, int quiet) {
    uint64_t t; memcpy(&t, &c->fpr[1], 8);
    uint64_t want = v->keep ? SENTINEL : v->t;
    uint32_t cr1 = v->in & 1 ? v->fpscr_out >> 28 : 0;
    if (t == want && c->fpscr == v->fpscr_out && cr_field(c, 1) == cr1 && c->pc == 0x1004) return 0;
    if (!quiet)
        fprintf(stderr, "%s MISMATCH %s/%s in=%08x fpscr %08x a=%016llx b=%016llx c=%016llx: got %016llx/%08x/cr1 %x, "
                "want %016llx/%08x/cr1 %x\n", who, v->origin, v->tag, v->in, v->fpscr, (unsigned long long)v->a,
                (unsigned long long)v->b, (unsigned long long)v->c, (unsigned long long)t, c->fpscr, cr_field(c, 1),
                (unsigned long long)want, v->fpscr_out, cr1);
    return 1;
}
static unsigned interp_run(int quiet) {
    unsigned bad = 0;
    for (unsigned i = 0; i < NV; i++) { CPU c = setup(&fp_vectors[i]); assert(interp_step(&c) == 0); bad += verify(&c, &fp_vectors[i], "interp", quiet); }
    return bad;
}

#ifdef FR_JIT
static uint32_t *code;
/* One JIT mode: fp_old, fma_fast, fma_single_direct, fp_round_fast, fp_mixed. Returns mismatches; *jit counts the
 * vectors the JIT completed itself and *fr those of them expecting FR = 1. */
static unsigned run_jit(const int mode[5], unsigned *jit, unsigned *fr, int quiet) {
    unsigned bad = 0; *jit = *fr = 0;
    for (unsigned i = 0; i < NV; i++) {
        const FpVector *v = &fp_vectors[i];
        CPU c = setup(v), before = c;
        jmsr = MSR_750 | MSR_FP; jsys = 0; fp_old = mode[0]; fma_fast = mode[1]; fma_single_direct = mode[2];
        fp_round_fast = mode[3]; fp_mixed = mode[4]; fp_hoist = fp_checked = fp_guarded = 0; fcache_active = 0; fp_lazy_active = 0;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code; E(0xa9bf7bfb); E(0xaa0003fb);
        exit_common = code + 4096;
        int r = tr(v->in, 0x1000);
        if (r) exit_why(0x1000, X_INTERP); else b_to(exit_common);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        c.jit_exit = 0;
        ((void (*)(CPU *))(void *)code)(&c);
        if (r || c.jit_exit == X_INTERP) {
            if (memcmp(c.fpr, before.fpr, sizeof c.fpr) || c.fpscr != before.fpscr) { fprintf(stderr, "JIT hand-over changed state in=%08x\n", v->in); bad++; continue; }
            c = before; assert(interp_step(&c) == 0);
        } else { c.pc = 0x1004; (*jit)++; *fr += !!(v->fpscr_out & 1u << 18); }
        bad += verify(&c, v, "jit", quiet);
    }
    return bad;
}
#endif

int main(void)
{
    unsigned expect_fr = 0;
    for (unsigned i = 0; i < NV; i++) expect_fr += !fp_vectors[i].keep && (fp_vectors[i].fpscr_out & 1u << 18);
    unsigned bad = interp_run(0);
    fp_fr_on = 0; unsigned neg = interp_run(1); fp_fr_on = 1;
    printf("fpscr-fr: %u oracle vectors (%u expect FR=1): interpreter %u mismatches; FR off: %u mismatches\n", NV, expect_fr, bad, neg);
    int pass = !bad && neg == expect_fr;
#ifdef FR_JIT
    code = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    assert(code != MAP_FAILED);
    static const int modes[][5] = { {0,1,0,1,0}, {0,1,1,1,0}, {0,1,0,1,1}, {0,0,0,1,0}, {0,1,0,0,0}, {1,0,0,0,0} };
    static const char *names[] = { "window default", "PT_FMA_DIRECT_S=1", "PT_FP_MIXED=1", "PT_FMA_FAST=0", "PT_FP_ROUND_FAST=0", "PT_FP_OLD=1" };
    for (unsigned m = 0; m < sizeof modes / sizeof *modes; m++) {
        unsigned jit, fr, b = run_jit(modes[m], &jit, &fr, 0);
        printf("  JIT %-18s: %4u completed in the JIT (%u with FR=1), %u mismatches\n", names[m], jit, fr, b);
        pass &= !b && (m || fr > 100);
    }
    fp_fr_on = 0; unsigned jit, fr, b = run_jit(modes[0], &jit, &fr, 1); fp_fr_on = 1;
    printf("  JIT window default, FR off: %u mismatches (negative control)\n", b);
    pass &= b == expect_fr;
#endif
    puts(pass ? "PASS" : "FAIL");
    return !pass;
}
