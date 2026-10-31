/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real emitter vs interpreter: added integer VMX, SAT/CR6, aliasing, NJ and VEC.
 * Build as vmx-fp-jit.c (tools/build-perf.sh; executed with --run-unit). */
#include "../epicycle/jit.c"
#include <assert.h>
#include <pthread.h>
int sys_call(CPU *c) { (void)c; abort(); }
static uint64_t seed = 0x943e5f1326ab80d7ull;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return (uint32_t)seed; }
int main(void) {
    static const unsigned ops[] = {
        0x00e,0x04e,0x20e,0x24e,0x28e,0x2ce,
        0x008,0x048,0x108,0x148,0x208,0x248,0x308,0x348,
        0x402,0x442,0x482,0x502,0x542,0x582,
        0x006,0x046,0x086,0x206,0x246,0x286,0x306,0x346,0x386,
        0x406,0x446,0x486,0x606,0x646,0x686,0x706,0x746,0x786,
        0x200,0x240,0x280,0x300,0x340,0x380,0x600,0x640,0x680,0x700,0x740,0x780,
        0x020,0x021, /* vmhaddshs vmhraddshs (VA: c in bits 6-10) */
        0x08e,0x0ce,0x10e,0x14e,0x18e,0x1ce, /* saturating packs */
        0x022, /* vmladduhm (VA: c in bits 6-10) */
        0x1c4,0x2c4 /* vsl vsr: the whole register by vB[15] & 7 bits */
    };
    uint32_t *code = mmap(NULL, 65536, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_JIT, -1, 0);
    assert(code != MAP_FAILED);
    unsigned count = 0;
    for (unsigned oi = 0; oi < sizeof ops / sizeof *ops; oi++) for (unsigned k = 0; k < 1024; k++) {
        unsigned xo = ops[oi], a = rnd() & 31, b = rnd() & 31, d = rnd() & 31;
        if (xo == 0x20e || xo == 0x24e || xo == 0x28e || xo == 0x2ce) a = 0;
        uint32_t in = 4u << 26 | d << 21 | a << 16 | b << 11 | xo;
        if (xo == 0x020 || xo == 0x021 || xo == 0x022) in |= (rnd() & 31) << 6;
        CPU c = {0}; c.pc = 0x1000; c.sys = k & 1; c.msr = k & 2 ? MSR_VEC : 0;
        c.spr[287] = 0x80030101; c.vscr = (k >> 2 & 1) | (k >> 3 & 1) << 16;
        for (unsigned i = 0; i < 16; i++) c.crtab[i] = cr_make(i >> 3 & 1, i >> 2 & 1, i >> 1 & 1, i & 1);
        for (unsigned r = 0; r < 32; r++) {
            c.cr[r & 7] = ((uint64_t)rnd() << 32) | rnd();
            for (unsigned j = 0; j < 16; j++) c.vr[r][j] = k & 16 ? (uint8_t)rnd() : (uint8_t[]){0,0x7f,0x80,0xff}[k >> 5 & 3];
        }
        if (k & 128) memcpy(c.vr[b], c.vr[a], 16); /* all true, including destination/input aliases */
        CPU expected = c;
        if (c.sys && !(c.msr & MSR_VEC)) { expected.pc = 0x2000; expected.jit_exit = X_INTERP; }
        else assert(ppc_altivec(&expected, in));
        jmsr = MSR_750|MSR_VEC; jsys = c.sys; vmx_neon_enabled = vmx_int2_enabled = vmx_mhadd_enabled = 1;
        cwr = 0; memset(cmap, -1, sizeof cmap);
        pthread_jit_write_protect_np(0);
        cp = code; E(0xa9bf7bfb); E(0xaa0003fb); exit_common = code + 4096;
        assert(tr(in, 0x2000) == 0); b_to(exit_common);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        ((void (*)(CPU *))(void *)code)(&c);
        if (memcmp(&c, &expected, sizeof c)) {
            fprintf(stderr, "integer VMX mismatch xo=%03x case=%u SAT=%x/%x CR6=%llx/%llx\n", xo,k,c.vscr,expected.vscr,
                    (unsigned long long)c.cr[6],(unsigned long long)expected.cr[6]); return 1;
        }
        count++;
    }
    printf("integer VMX: %u cases, full CPU state matches interpreter\n", count);
    return 0;
}
