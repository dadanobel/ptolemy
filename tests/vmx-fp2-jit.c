/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's vcmpeqfp/vcmpgefp/vcmpgtfp (and the Rc forms: CR6), vrsqrtefp, vrefp and vrfin/z/p/m (PT_VMX_FP2) against
 * the interpreter, bit for bit over the whole CPU state: zero, denormal, NaN, infinity, huge and tiny lanes, equal and
 * ordered vectors (all/none CR6), VSCR.NJ on/off, user/system mode, register aliasing. The JIT may hand a case to the
 * interpreter, untouched, only for a denormal input lane or a denormal vrefp result lane. */
#include "../epicycle/jit.c"
#include <assert.h>
#include <pthread.h>
int sys_call(CPU *c) { (void)c; abort(); }

static uint64_t rng = 0x9e3779b97f4a7c15u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static uint32_t lane(void) {
    static const uint32_t special[] = {0, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000, 0x7fc00000,
        0x7fa00000, 0xffc00001, 0x00000001, 0x807fffff, 0x00800000, 0x80800001, 0x7f7fffff, 0x1f800000, 0x5f800000};
    uint32_t r = rnd();
    switch (r & 7) {
    case 0: return special[r >> 3 & 15];
    case 1: return (r & 0x80000000) | (uint32_t)(1 + (r >> 8) % 40) << 23 | (rnd() & 0x7fffff);     /* tiny normals */
    case 2: return (r & 0x80000000) | (uint32_t)(214 + (r >> 8) % 40) << 23 | (rnd() & 0x7fffff);   /* huge normals */
    default: return (r >> 1 & 0x80000000) | (uint32_t)(100 + (r >> 8) % 56) << 23 | (rnd() & 0x7fffff);
    }
}
static int denormal(uint32_t u) { uint32_t m = u & 0x7fffffff; return m && m < 0x00800000; }

int main(void)
{
    uint32_t *code = mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    assert(code != MAP_FAILED);
    static const uint32_t ops[12] = {0x0c6, 0x1c6, 0x2c6, 0x4c6, 0x5c6, 0x6c6, 0x14a, 0x10a, 0x20a, 0x24a, 0x28a, 0x2ca};
    unsigned fast = 0, slow = 0, cr_all = 0, cr_none = 0;
    for (unsigned n = 0; n < 1200000; n++) {
        unsigned o = n % 12, d = rnd() & 31, a = rnd() & 31, b = rnd() & 31, sys = n >> 1 & 1, rsq = o >= 6, kind = rnd() & 7;
        uint32_t in = 4u << 26 | d << 21 | (rsq ? 0 : a << 16) | b << 11 | ops[o];
        CPU c = {0}; c.sys = sys; c.msr = MSR_VEC; c.pc = 0x1000; c.spr[287] = 0x80030101;   /* PVR 7447A */
        c.vscr = n & 4 ? 0x10000 : 0;
        for (unsigned i = 0; i < 16; i++) c.crtab[i] = cr_make(i >> 3 & 1, i >> 2 & 1, i >> 1 & 1, i & 1);
        for (unsigned r = 0; r < 32; r++) for (unsigned i = 0; i < 4; i++) wbe32(c.vr[r] + 4 * i, lane());
        if (!rsq && kind == 1) memcpy(c.vr[b], c.vr[a], 16);                        /* equal: eq/ge all true */
        if (!rsq && kind == 2 && a != b) for (unsigned i = 0; i < 4; i++) {         /* ordered: gt/ge all true */
            wbe32(c.vr[a] + 4 * i, 0x40000000u + (rnd() & 0x3fffff)); wbe32(c.vr[b] + 4 * i, 0x3f800000u + (rnd() & 0x3fffff)); }
        if (rsq && kind < 3) for (unsigned i = 0; i < 4; i++) wbe32(c.vr[b] + 4 * i, (uint32_t)(1 + rnd() % 253) << 23 | (rnd() & 0x7fffff));
        if (o >= 8 && kind == 3) for (unsigned i = 0; i < 4; i++) {                 /* vrfi*: halves, quarters and integers */
            float f = (float)((int)(rnd() % 2001) - 1000) + 0.25f * (rnd() & 3); uint32_t u; memcpy(&u, &f, 4); wbe32(c.vr[b] + 4 * i, u); }
        int special = 0;
        for (unsigned i = 0; i < 4; i++) { uint32_t ub = be32(c.vr[b] + 4 * i), ur; float fb, r; memcpy(&fb, &ub, 4); r = 1.0f / fb; memcpy(&ur, &r, 4);
            special |= denormal(ub) | (!rsq && denormal(be32(c.vr[a] + 4 * i))) | (o == 7 && denormal(ur)); }
        CPU expected = c; assert(ppc_altivec(&expected, in));
        jmsr = MSR_750 | MSR_VEC; jsys = sys; vmx_neon_enabled = vmx_fp2_enabled = 1;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code; E(0xa9bf7bfb); E(0xaa0003fb);       /* stp x27, x30, [sp, #-16]!; mov x27, x0 */
        exit_common = code + 1024;
        assert(tr(in, 0x2000) == 0);
        b_to(exit_common);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        CPU before = c;
        ((void (*)(CPU *))(void *)code)(&c);
        if (c.jit_exit == X_INTERP) {                  /* handed over untouched, and only for a denormal lane */
            before.pc = 0x2000; before.jit_exit = X_INTERP;
            assert(special && !memcmp(&c, &before, sizeof c)); slow++;
        } else {
            if (special || memcmp(&c, &expected, sizeof c)) {
                fprintf(stderr, "MISMATCH n=%u in=%08x special=%d vscr=%x\n", n, in, special, before.vscr);
                for (unsigned i = 0; i < 4; i++) fprintf(stderr, "  a %08x b %08x -> jit %08x interp %08x\n", be32(before.vr[a] + 4 * i),
                    be32(before.vr[b] + 4 * i), be32(c.vr[d] + 4 * i), be32(expected.vr[d] + 4 * i));
                fprintf(stderr, "  cr6 jit %016llx interp %016llx\n", (unsigned long long)c.cr[6], (unsigned long long)expected.cr[6]);
                return 1;
            }
            if (ops[o] & 0x400) { cr_all += be32(c.vr[d]) && be32(c.vr[d] + 4) && be32(c.vr[d] + 8) && be32(c.vr[d] + 12);
                                  cr_none += !(be32(c.vr[d]) | be32(c.vr[d] + 4) | be32(c.vr[d] + 8) | be32(c.vr[d] + 12)); }
            fast++;
        }
    }
    assert(cr_all > 1000 && cr_none > 1000);
    cp = code; vmx_fp2_enabled = 0;
    assert(tr(4u << 26 | 0x2c6u, 0x2000) == -1 && cp == code);   /* PT_VMX_FP2=0 */
    vmx_fp2_enabled = 1; jmsr = MSR_750;
    assert(tr(4u << 26 | 0x14au, 0x2000) == -1 && cp == code);   /* VEC off at translation: interpreter */
    printf("VMX FP2 JIT (vcmpeqfp/gefp/gtfp[.], vrsqrtefp, vrefp, vrfin/z/p/m): %u fast + %u interpreter cases bit-exact vs interpreter "
           "(CR6 all %u, none %u), switch and VEC rejection PASS.\n", fast, slow, cr_all, cr_none);
    return 0;
}
