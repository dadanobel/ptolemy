/* ARM words the stand JIT emits per PPC instruction (static, translation only; nothing executed). */
#include "../epicycle/jit.c"
int sys_call(CPU *c) { (void)c; abort(); }
static const struct { uint32_t in; const char *name; } T[] = {
    {0x7c632214, "add r3,r3,r4"}, {0x80640008, "lwz r3,8(r4)"}, {0x90640008, "stw r3,8(r4)"}, {0x7c032000, "cmpw r3,r4"},
    {0x7c000026, "mfcr r0"}, {0x7c0ff120, "mtcrf 0xff,r0"}, {0x5463103a, "rlwinm r3,r3,2,0,29"},
    {0xc8240008, "lfd f1,8(r4)"}, {0xd8240008, "stfd f1,8(r4)"}, {0xc0240008, "lfs f1,8(r4)"},
    {0xfc21102a, "fadd f1,f1,f2"}, {0xfc211028, "fsub f1,f1,f2"}, {0xfc2100b2, "fmul f1,f1,f2"}, {0xfc211024, "fdiv f1,f1,f2"},
    {0xec21102a, "fadds f1,f1,f2"}, {0xec2100b2, "fmuls f1,f1,f2"},
    {0xfc2110fa, "fmadd f1,f1,f3,f2"}, {0xfc2110fc, "fnmsub f1,f1,f3,f2"}, {0xec2110fa, "fmadds f1,f1,f3,f2"},
    {0xfc200818, "frsp f1,f1"}, {0xfc20081e, "fctiwz f1,f1"}, {0xfc011000, "fcmpu cr0,f1,f2"}, {0xfc201090, "fmr f1,f2"},
    {0xfc2110ee, "fsel f1,f1,f3,f2"}, {0xfc000034, "frsqrte f0,f0"},
    {0x7c2220ce, "lvx v1,r2,r4"}, {0x7c2221ce, "stvx v1,r2,r4"},
    {0x10211000, "vaddubm v1,v1,v2"}, {0x10211484, "vor v1,v1,v2"}, {0x1021102b, "vperm v1,v1,v2,v0"},
    {0x102110ee, "vmaddfp v1,v1,v3,v2"}, {0x1021100a, "vaddfp v1,v1,v2"}, {0x10211200, "vaddubs v1,v1,v2 (sat)"},
    {0x10211406, "vcmpequb. v1,v1,v2"}, {0x1021104e, "vpkuwum v1,v1,v2"}, {0x1021110a, "vrefp v1,v2"},
};
int main(void) {
    static uint32_t buf[1 << 16];
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        jmsr = MSR_750 | MSR_FP | MSR_VEC; jsys = 0; fp_old = 0; fma_fast = 1; vmx_neon_enabled = vmx_mem_enabled = vmx_vscr_enabled = vmx_fp_enabled = 1; zbase = 0;
        memset(cmap, -1, sizeof cmap); cwr = 0; nside = 0; cp = buf; exit_common = buf + (1 << 15);
        int r = tr(T[i].in, 0x2000);
        printf("%-24s %4ld ARM words%s\n", T[i].name, (long)(cp - buf), r < 0 ? "  (not translated: interpreter)" : "");
    }
    return 0;
}
