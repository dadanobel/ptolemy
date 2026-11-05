/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's vaddfp/vsubfp/vmaxfp/vminfp/vmaddfp/vnmsubfp against the interpreter, bit for bit: random and special lanes
 * (zero, denormal, NaN, infinity, overflow/underflow), VSCR.NJ on/off, user/system mode, register aliasing.
 * Every case completes in the JIT (NaN and denormal lanes too); vcfux/vcfsx/vctuxs/vctsxs with every UIMM. */
#include "jit-test.h"
#include <math.h>

static uint32_t lane(void) {
    static const uint32_t special[] = {0,          0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000,
                                       0x7fc00000, 0x7fa00000, 0xffc00001, 0x00000001, 0x807fffff, 0x00800000,
                                       0x80800001, 0x7f7fffff, 0x1f800000, 0x5f800000};
    uint32_t r = rnd();
    switch (r & 7) {
    case 0:
        return special[r >> 3 & 15];
    case 1:
        return (r & 0x80000000) | (uint32_t)(1 + (r >> 8) % 40) << 23 | (rnd() & 0x7fffff); /* tiny normals */
    case 2:
        return (r & 0x80000000) | (uint32_t)(214 + (r >> 8) % 40) << 23 | (rnd() & 0x7fffff); /* huge normals */
    default:
        return (r & 0x80000000) | (uint32_t)(100 + (r >> 8) % 56) << 23 | (rnd() & 0x7fffff);
    }
}
static int odd(uint32_t u) {
    uint32_t m = u & 0x7fffffff;
    return m > 0x7f800000 || (m && m < 0x00800000);
}
static uint32_t bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
static float flt(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

int main(void) {
    uint32_t *code =
            mmap(NULL, 1 << 16, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    assert(code != MAP_FAILED);
    unsigned fast = 0, slow = 0;
    for (unsigned n = 0; n < 600000; n++) {
        static const uint32_t ops[6] = {46,    47,    0x00a,
                                        0x04a, 0x40a, 0x44a}; /* vmaddfp vnmsubfp vaddfp vsubfp vmaxfp vminfp */
        unsigned d = rnd() & 31, a = rnd() & 31, b = rnd() & 31, k = rnd() & 31, sys = n >> 1 & 1, o = n % 6,
                 nm = o == 1;
        uint32_t in = 4u << 26 | d << 21 | a << 16 | b << 11 | (o < 2 ? k << 6 : 0) | ops[o];
        CPU c = {0};
        c.sys = sys;
        c.msr = MSR_VEC;
        c.pc = 0x1000;
        c.spr[287] = 0x80030101; /* PVR 7447A */
        c.vscr = n & 4 ? 0x10000 : 0;
        for (unsigned r = 0; r < 32; r++)
            for (unsigned i = 0; i < 4; i++)
                wbe32(c.vr[r] + 4 * i, lane());
        int special = 0;
        for (unsigned i = 0; i < 4; i++) {
            uint32_t ua = be32(c.vr[a] + 4 * i), ub = be32(c.vr[b] + 4 * i), uk = be32(c.vr[k] + 4 * i);
            float fa = flt(ua), fb = flt(ub),
                  f = o == 2   ? fa + fb
                      : o == 3 ? fa - fb
                      : o == 4 ? fmaxf(fa, fb)
                      : o == 5 ? fminf(fa, fb)
                               : fmaf(fa, flt(uk), nm ? -fb : fb);
            special |= odd(ua) | odd(ub) | (o < 2 && odd(uk)) | odd(bits(f));
        }
        CPU expected = c;
        assert(ppc_altivec(&expected, in));
        jmsr = MSR_750 | MSR_VEC;
        jsys = sys;
        vmx_neon_enabled = vmx_fp_enabled = 1;
        vmx_checked = 0;
        CPU before = c;
        run1(code, in, &c);
        (void)before;
        if (memcmp(&c, &expected, sizeof c)) {
            fprintf(stderr, "VMX FP mismatch op %03x NJ %u special %d exit %u\n", ops[o], !!(c.vscr & 0x10000), special,
                    c.jit_exit);
            for (unsigned i = 0; i < 4; i++)
                fprintf(stderr, "  lane %u: a %08x b %08x c %08x -> jit %08x interp %08x\n", i, be32(c.vr[a] + 4 * i),
                        be32(before.vr[b] + 4 * i), be32(before.vr[k] + 4 * i), be32(c.vr[d] + 4 * i),
                        be32(expected.vr[d] + 4 * i));
            return 1;
        }
        if (special)
            slow++;
        else
            fast++;
    }
    for (unsigned n = 0; n < 64000; n++) { /* vcfux vcfsx vctuxs vctsxs */
        unsigned d = rnd() & 31, k = n & 31, b = rnd() & 31,
                 x = (const unsigned[]){0x30a, 0x34a, 0x38a, 0x3ca}[n >> 5 & 3];
        uint32_t in = 4u << 26 | d << 21 | k << 16 | b << 11 | x;
        CPU c = {0};
        c.msr = MSR_VEC;
        c.pc = 0x1000;
        c.spr[287] = 0x80030101;
        c.vscr = n & 128 ? 0x10000 : 0;
        for (unsigned r = 0; r < 32; r++)
            for (unsigned i = 0; i < 4; i++)
                wbe32(c.vr[r] + 4 * i, x >= 0x38a ? lane() : n & 256 ? rnd() : rnd() >> (rnd() & 31));
        CPU expected = c;
        assert(ppc_altivec(&expected, in));
        jsys = 0;
        run1(code, in, &c);
        if (memcmp(&c, &expected, sizeof c)) {
            fprintf(stderr, "conversion %03x mismatch k=%u\n", x, k);
            return 1;
        }
    }
    cp = code;
    vmx_fp_enabled = 0;
    assert(tr(4u << 26 | 46u, 0x2000) == -1 && cp == code); /* PT_VMX_FP=0 */
    vmx_fp_enabled = 1;
    jmsr = MSR_750;
    assert(tr(4u << 26 | 46u, 0x2000) == -1 && cp == code); /* VEC off at translation: interpreter */
    printf("VMX FP JIT (6 ops): %u ordinary + %u NaN/denormal cases, and 64000 vcfux/vcfsx/vctuxs/vctsxs, all in the JIT, bit-exact vs interpreter; switch and VEC rejection PASS.\n",
           fast, slow);
    return 0;
}
