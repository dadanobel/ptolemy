/* SPDX-License-Identifier: GPL-2.0-or-later
 * The real JIT emitter's lvx/lvxl/stvx/stvxl against the interpreter,
 * without HVF: user and system mode, VEC on/off, rA = 0 and not, unaligned EA. */
#include "../epicycle/jit.c"
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
int sys_call(CPU *c) { (void)c; abort(); }
enum { MEMSZ = 1 << 16 };

static uint32_t x31(unsigned xo, unsigned d, unsigned a, unsigned b) { return 31u << 26 | d << 21 | a << 16 | b << 11 | xo << 1; }

int main(void)
{
    size_t bytes = 1 << 16;
    uint32_t *code = mmap(NULL, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT, -1, 0);
    uint8_t *m1 = malloc(MEMSZ), *m2 = malloc(MEMSZ);
    assert(code != MAP_FAILED && m1 && m2);
    static const unsigned xos[] = {103,359,231,487,6,38,7,39,71,135,167,199};
    unsigned n = 0;
    for (unsigned k = 0; k < sizeof xos/sizeof *xos; k++) for (int sys = 0; sys <= 1; sys++) for (int vec = 0; vec <= 1; vec++)
    for (unsigned a = 0; a <= 3; a += 3) for (uint32_t ea = 0x1230; ea < 0x1240; ea += 3) {
        uint32_t in = x31(xos[k], 7, a, 4);
        for (unsigned i = 0; i < MEMSZ; i++) m1[i] = m2[i] = (uint8_t)(i * 7 + 3);
        CPU c = {0}; c.sys = sys; c.msr = vec ? MSR_VEC : 0; c.pc = 0x1000; c.spr[287] = 0x000c0209;   /* PVR 7400 */
        for (unsigned r = 0; r < 32; r++) for (unsigned i = 0; i < 16; i++) c.vr[r][i] = (uint8_t)(r * 16 + i + 0x80);
        c.gpr[3] = 0x1000; c.gpr[4] = a ? ea - 0x1000 : ea;
        c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = MEMSZ;
        CPU expected = c; expected.preg[0].host = m2; c.preg[0].host = m1;
        if (sys && !vec) { expected.pc = 0x2000; expected.jit_exit = X_INTERP; }
        else assert(ppc_altivec_mem(&expected, in) == 0);
        jmsr = MSR_750 | MSR_VEC; jsys = sys; vmx_mem_enabled = vmx_int2_enabled = 1; zbase = 0;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code;
        E(0xa9bf7bfb);          /* stp x27, x30, [sp, #-16]! */
        E(0xa9bf77fc);          /* stp x28, x29, [sp, #-16]! */
        E(0xaa0003fb);          /* mov x27, x0 */
        E(0xaa0103fc);          /* mov x28, x1 */
        ldrw(3, ENV, OGPR(3)); ldrw(4, ENV, OGPR(4));   /* r3, r4 live in x3, x4 (GMAP) */
        exit_common = code + 1024;
        assert(tr(in, 0x2000) == 0);
        b_to(exit_common);
        cp = exit_common; E(0xa8c177fc); E(0xa8c17bfb); E(0xd65f03c0);   /* ldp, ldp, ret */
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 3));
        ((void (*)(CPU *, uint8_t *))(void *)code)(&c, m1);
        c.preg[0].host = m2;
        assert(!memcmp(&c, &expected, sizeof c));
        assert(!memcmp(m1, m2, MEMSZ));
        n++;
    }
    /* mfvscr/mtvscr against ppc_altivec */
    unsigned nv = 0;
    for (unsigned k = 0; k < 2; k++) for (int sys = 0; sys <= 1; sys++) for (int vec = 0; vec <= 1; vec++)
    for (unsigned t = 0; t < 4; t++) {
        uint32_t in = 4u << 26 | 7u << 21 | 9u << 11 | (k ? 0x644u : 0x604u);
        CPU c = {0}; c.sys = sys; c.msr = vec ? MSR_VEC : 0; c.pc = 0x1000; c.spr[287] = 0x000c0209;
        c.vscr = (uint32_t[]){0, 1, 0x10000, 0x10001}[t];
        for (unsigned r = 0; r < 32; r++) for (unsigned i = 0; i < 16; i++) c.vr[r][i] = (uint8_t)(r * 16 + i * 37 + t);
        CPU expected = c;
        if (sys && !vec) { expected.pc = 0x2000; expected.jit_exit = X_INTERP; }
        else assert(ppc_altivec(&expected, in));
        jmsr = MSR_750 | MSR_VEC; jsys = sys; vmx_vscr_enabled = 1;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code;
        E(0xa9bf7bfb); E(0xaa0003fb);
        exit_common = code + 1024;
        assert(tr(in, 0x2000) == 0);
        b_to(exit_common);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        ((void (*)(CPU *))(void *)code)(&c);
        assert(!memcmp(&c, &expected, sizeof c));
        nv++;
    }
    printf("VSCR JIT: %u cases vs interpreter PASS.\n", nv);
    /* mtspr HID0, mtspr/mfspr MSSCR0 in the JIT (G4 idle loop) against interp_step; HID0 goes to the
       interpreter, untouched, exactly when HIGH_BAT/XBSEN/STEN change or XBSEN is written. */
    unsigned nh = 0, nhx = 0;
    for (unsigned t = 0; t < 12288; t++) {
        static const uint32_t bits[8] = {0, 0x00800000, 0x100, 0x01000000, 0x00200000, 0x00400000, 0x80000000, 0x4000};
        uint32_t old = bits[t & 7] | bits[t >> 3 & 7], nw = bits[t >> 6 & 7] | bits[t >> 9 & 7], k = t % 3, spr = k ? 1014 : 1008;
        uint32_t in = 31u << 26 | 5u << 21 | (spr & 31) << 16 | (spr >> 5) << 11 | (k == 2 ? 339u : 467u) << 1;
        for (unsigned i = 0; i < MEMSZ; i++) m2[i] = 0;
        wbe32(m2 + 0x1000, in);
        CPU c = {0}; c.sys = 2; c.pc = 0x1000; c.spr[287] = 0x80030102; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = MEMSZ; c.preg[0].host = m2;
        c.spr[1008] = old; c.spr[1014] = old ^ 0x5a5a; c.gpr[5] = nw; c.xbsen = t >> 12 & 1 ? 0x100 : 0;
        CPU expected = c; assert(interp_step(&expected) == 0 && expected.pc == 0x1004);
        int hand = !k && (((old ^ nw) & 0x01800100u) || (nw & 0x100u));
        jmsr = MSR_750 | MSR_VEC; jsys = 2; vm_vec = MSR_VEC; kpr = 0;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code; E(0xa9bf7bfb); E(0xaa0003fb); ldrw(5, ENV, OGPR(5));
        exit_common = code + 1024;
        assert(tr(in, 0x2000) == 0);
        strw(5, ENV, OGPR(5)); b_to(exit_common);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        CPU before = c;
        ((void (*)(CPU *))(void *)code)(&c);
        if (hand) { before.pc = 0x2000; before.jit_exit = X_INTERP; assert(!memcmp(&c, &before, sizeof c)); nhx++; }
        else { assert(c.jit_exit != X_INTERP); c.pc = expected.pc; c.icount = expected.icount; assert(!memcmp(&c, &expected, sizeof c)); nh++; }
    }
    assert(nh > 4000 && nhx > 1000);
    printf("HID0/MSSCR0 JIT: %u in the JIT + %u handed to the interpreter, vs interp_step PASS.\n", nh, nhx);
    /* rfi in the VM with the target's SRs not yet in the ARM tables (srgen != srgen_ok, SRR1.DR = 1) leaves
       for a lookup at SRR0 with MSR = SRR1, as interp_step; icbi pending, other MSR bits, an IRQ waiting for EE — interpreter. */
    unsigned nr = 0, nri = 0;
    for (unsigned t = 0; t < 2048; t++) {
        static const uint32_t m[8] = {MSR_IR, MSR_PR, MSR_EE, MSR_FP, MSR_VEC, MSR_ME, 0x400 /* SE */, MSR_POW};
        uint32_t srr1 = MSR_DR, msr0 = t >> 8 & 1 ? MSR_EE | MSR_ME : MSR_ME;
        for (unsigned i = 0; i < 8; i++) if (t >> i & 1) srr1 |= m[i];
        for (unsigned i = 0; i < MEMSZ; i++) m2[i] = 0;
        wbe32(m2 + 0x1000, 0x4c000064);
        CPU c = {0}; c.sys = 2; c.pc = 0x1000; c.msr = msr0; c.spr[287] = 0x80030102; c.npreg = 1; c.preg[0].base = 0; c.preg[0].size = MEMSZ; c.preg[0].host = m2;
        c.spr[SPR_SRR0] = 0x12345 + t * 0x111; c.spr[SPR_SRR1] = srr1; c.srgen = 5; c.srgen_ok = 4; c.icbi_n = t >> 9 & 1; c.pend_masked = t >> 10 & 1;
        CPU expected = c; assert(interp_step(&expected) == 0);
        jmsr = MSR_750 | MSR_VEC; jsys = 2; vm_rfi = 1; vm_vec = MSR_VEC; kpr = 0; jit_vm_buf = code;
        memset(cmap, -1, sizeof cmap); cwr = 0;
        pthread_jit_write_protect_np(0);
        cp = code; E(0xa9bf7bfb); E(0xaa0003fb);
        exit_common = code + 1024;
        assert(tr(0x4c000064, 0x1000) == 1);
        cp = exit_common; E(0xa8c17bfb); E(0xd65f03c0);
        pthread_jit_write_protect_np(1);
        __builtin___clear_cache((char *)code, (char *)(exit_common + 2));
        CPU before = c;
        ((void (*)(CPU *))(void *)code)(&c);
        int hand = c.icbi_n || (srr1 & 0x400) || ((~msr0 & srr1 & MSR_EE) && before.pend_masked);
        if (hand) { before.jit_exit = X_INTERP; assert(!memcmp(&c, &before, sizeof c)); nri++; }
        else { assert(c.jit_exit == X_LOOKUP); c.icount = expected.icount; c.jit_exit = expected.jit_exit;
               if (!before.pend_masked && (~msr0 & srr1 & MSR_EE)) {
                   /* No pending IRQ: the JIT deliberately skips the interpreter's redundant scan. */
                   assert(expected.irq == 1 && c.irq == 0); expected.irq = 0;
               }
               assert(!memcmp(&c, &expected, sizeof c)); nr++; }
    }
    jit_vm_buf = NULL;
    assert(nr == 448 && nri == 1600);
    printf("rfi to new SRs: %u looked up at SRR0 + %u handed to the interpreter, vs interp_step PASS.\n", nr, nri);
    cp = code; jmsr = MSR_750;
    assert(tr(x31(103, 7, 0, 4), 0x2000) == -1 && cp == code);   /* G3: interpreter */
    jmsr |= MSR_VEC; vmx_mem_enabled = 0;
    assert(tr(x31(231, 7, 0, 4), 0x2000) == -1 && cp == code);   /* PT_VMX_MEM=0 */
    printf("VMX memory JIT: %u cases vs interpreter, G3 rejection and switch PASS.\n", n);
    return 0;
}
