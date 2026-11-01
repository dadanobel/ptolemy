// PowerPC interpreter (750/G3 with FPU and supervisor state; 7447A/G4 additions, AltiVec through altivec.c).
// The reference for the JIT and the fallback path for rare instructions.
#include "ppc.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <fenv.h>
#include <float.h>
#include <stdlib.h>

int trace_unimpl = 1;
#define LD(n, ea) ({ uint64_t v_; if (ppc_ld(c, ea, n, &v_)) return 0; v_; })          // through the MMU; DSI -> return
#define ST(n, ea, v) do { if (ppc_st(c, ea, n, v)) return 0; } while (0)
#define R(n) c->gpr[n]
#define RA0(n) ((n) ? c->gpr[n] : 0)

static uint32_t rotl(uint32_t x, int n) { n &= 31; return n ? x << n | x >> (32 - n) : x; }
uint32_t ppc_mask(int mb, int me) {
    uint32_t a = 0xFFFFFFFFu >> mb, b = me == 31 ? 0 : 0xFFFFFFFFu >> (me + 1);
    return mb <= me ? a & ~b : a | ~b;
}
static void cr_v(CPU *c, int f, int64_t v) { c->cr[f] = c->so ? cr_make(v < 0, v > 0, v == 0, 1) : (uint64_t)v; }
static void cr0(CPU *c, uint32_t r) { cr_v(c, 0, (int32_t)r); }
static void cmp_s(CPU *c, int f, uint32_t a, uint32_t b) { cr_v(c, f, (int64_t)(int32_t)a - (int32_t)b); }
static void cmp_u(CPU *c, int f, uint32_t a, uint32_t b) { cr_v(c, f, (int64_t)a - (int64_t)b); }
static uint64_t fbits(CPU *c, int r) { uint64_t b; memcpy(&b, &c->fpr[r], 8); return b; }
static void fsetb(CPU *c, int r, uint64_t b) { memcpy(&c->fpr[r], &b, 8); }
static double b2d(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }
static uint64_t d2b(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }

// --- FPSCR (IBM bit numbering: bit 0 is the most significant) ---
enum { FX = 1u << 31, FEX = 1u << 30, VX = 1u << 29, OX = 1u << 28, UX = 1u << 27, ZX = 1u << 26, XX = 1u << 25,
       VXSNAN = 1u << 24, VXISI = 1u << 23, VXIDI = 1u << 22, VXZDZ = 1u << 21, VXIMZ = 1u << 20, VXVC = 1u << 19,
       FR = 1u << 18, FI = 1u << 17, FPRF = 0x1Fu << 12, VXSOFT = 1u << 10, VXSQRT = 1u << 9, VXCVI = 1u << 8,
       VE = 1u << 7, OE = 1u << 6, UE = 1u << 5, ZE = 1u << 4, XE = 1u << 3,
       VXALL = VXSNAN | VXISI | VXIDI | VXZDZ | VXIMZ | VXVC | VXSOFT | VXSQRT | VXCVI, EXC = OX | UX | ZX | XX | VXALL };
static void fp_summ(CPU *c) {                                   // VX and FEX are summary bits, computed here
    uint32_t f = c->fpscr & ~(VX | FEX);
    if (f & VXALL) f |= VX;
    if ((f & VX && f & VE) || (f & OX && f & OE) || (f & UX && f & UE) || (f & ZX && f & ZE) || (f & XX && f & XE)) f |= FEX;
    c->fpscr = f;
}
static void fp_raise(CPU *c, uint32_t b) { if (b & ~c->fpscr & EXC) c->fpscr |= FX; c->fpscr |= b; fp_summ(c); }
static int is_nan(uint64_t b) { return (b & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (b & 0xFFFFFFFFFFFFFull); }
static int is_snan(uint64_t b) { return is_nan(b) && !(b & 0x8000000000000ull); }
static void set_fprf(CPU *c, uint64_t b, int sgl) {             // result class: C|FPCC
    double r = b2d(b); int neg = (int)(b >> 63); uint32_t k;
    if (is_nan(b)) k = 0x11;
    else if (isinf(r)) k = neg ? 0x09 : 0x05;
    else if (r == 0) k = neg ? 0x12 : 0x02;
    else if (fabs(r) < (sgl ? (double)FLT_MIN : DBL_MIN)) k = neg ? 0x18 : 0x14;
    else k = neg ? 0x08 : 0x04;
    c->fpscr = (c->fpscr & ~FPRF) | k << 12;
}
// The host FPU in the guest's rounding mode (FPSCR.RN), IEEE exception flags -> FPSCR. The host mode is restored.
static const int RMODE[4] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};
static int host_rm;
static void fp_begin(int rn) { host_rm = fegetround(); fesetround(RMODE[rn]); feclearexcept(FE_ALL_EXCEPT); }
static int fp_end(void) { int e = fetestexcept(FE_ALL_EXCEPT); fesetround(host_rm); return e; }
/* FR = the rounding incremented the fraction, i.e. |rounded| > |exact| (Book I, Rounding). Callers set it from a
 * second, truncating evaluation; architecturally undefined cases (disabled overflow, fres/frsqrte) keep FR = 0.
 * PT_FP_FR=0 (fp_fr_on = 0) leaves FR = 0 everywhere, only to measure the cost. */
int fp_fr_on = 1;
static void fp_fr(CPU *c, int e, int up) { if (fp_fr_on && up && (e & (FE_INEXACT | FE_OVERFLOW)) == FE_INEXACT) c->fpscr |= FR; }
static void fp_flags(CPU *c, int e) {
    c->fpscr &= ~(FR | FI);
    uint32_t b = 0;
    if (e & FE_OVERFLOW) b |= OX;
    if (e & FE_UNDERFLOW) b |= UX;
    if (e & FE_DIVBYZERO) b |= ZX;
    if (e & FE_INEXACT) { b |= XX; c->fpscr |= FI; }
    fp_raise(c, b);
}
// Single precision without double rounding: double rounded toward zero + sticky bit ("round to odd"), then to float by RN.
typedef double (*fp2)(double, double, double);
static double op_add(double a, double b, double c) { (void)c; volatile double r = a + b; return r; }
static double op_sub(double a, double b, double c) { (void)c; volatile double r = a - b; return r; }
static double op_mul(double a, double b, double c) { (void)c; volatile double r = a * b; return r; }
static double op_div(double a, double b, double c) { (void)c; volatile double r = a / b; return r; }
static double op_fma(double a, double b, double c) { volatile double r = fma(a, b, c); return r; }
static double op_rec(double a, double b, double c) { (void)a; (void)c; volatile double r = 1.0 / b; return r; }
static double op_rsq(double a, double b, double c) { (void)a; (void)c; volatile double r = 1.0 / sqrt(b); return r; }
/* Rounding to single (rv exact, or round-to-odd above single precision; e1: flags of computing it) with
 * enabled overflow/underflow (Book I, Overflow/Underflow Exception): OE=1 and an overflow, or UE=1 and a value tiny
 * before rounding: the exponent is adjusted by -192 / +192, the adjusted rounded result goes to FRT, OX / UX are set
 * (UX also when exact), FR/FI/XX follow the adjusted rounding and FEX the enable. Double precision with OE/UE=1 and
 * the FE0/FE1 program interrupt remain separate. PT_FP_RANGE handles double multiply/divide range results only. */
static uint64_t to_single(CPU *c, double rv0, int e1) {
    volatile double rv = rv0; int adj = 0, e;
    if (c->fpscr & UE && rv0 != 0 && fabs(rv0) < (double)FLT_MIN) { rv = rv0 * 0x1p192; adj = -1; }
    fp_begin(c->fpscr & 3); volatile float s = (float)rv; e = fp_end();
    if (!adj && c->fpscr & OE && e & FE_OVERFLOW && !(e1 & FE_OVERFLOW)) { rv = rv0 * 0x1p-192; adj = 1; fp_begin(c->fpscr & 3); s = (float)rv; e = fp_end(); }
    if (adj) { e &= adj > 0 ? ~0 : ~FE_UNDERFLOW; e1 &= ~FE_OVERFLOW; }   /* still over after -192 (huge frsp input): undefined, as disabled */
    fp_flags(c, e | (e1 & (FE_DIVBYZERO | FE_OVERFLOW)));
    if (adj) fp_raise(c, adj > 0 ? OX : UX);
    if (e & FE_INEXACT && fp_fr_on) { fp_begin(1); volatile float z = (float)rv; fp_end(); fp_fr(c, e | e1, fabsf(s) > fabsf(z)); }   // RZ of round-to-odd = RZ of exact
    return d2b((double)s);
}
/* Book I enabled range exceptions: retain precision after an exponent adjustment
 * of -1536/+1536. This cold path covers multiply/divide, including exact tiny
 * results. Both operand scalings are exact and stay normal whenever selected. */
static int fp_range_enabled(void) {
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("PT_FP_RANGE");enabled=v&&atoi(v)!=0;}
    return enabled;
}
static int fp_tiny_binary(fp2 f,double a,double b) {
    if(!isfinite(a)||!isfinite(b)||a==0||b==0)return 0;
    int ea,eb;double ma=frexp(fabs(a),&ea),mb=frexp(fabs(b),&eb);
    if(f==op_div)return ea-eb < -1022 || (ea-eb == -1022 && ma<mb);
    /* The exact 106-bit product decides tininess before rounding at DBL_MIN. */
    uint64_t ia=(uint64_t)scalbn(ma,53),ib=(uint64_t)scalbn(mb,53);
    __uint128_t product=(__uint128_t)ia*ib;
    int top=(product>>105)?105:104;
    return ea+eb-106+top < -1022;
}
static uint64_t fp_compute(CPU *c, fp2 f, double a, double b, double x, int sgl) {
    int e;
    if (!sgl) {
        fp_begin(c->fpscr & 3); volatile double r = f(a, b, x); e = fp_end();
        if(fp_range_enabled()&&(f==op_mul||f==op_div)&&isfinite(a)&&isfinite(b)&&a!=0&&b!=0) {
            int adjustment=(c->fpscr&OE)&&(e&FE_OVERFLOW)?-768:
                (c->fpscr&UE)&&fp_tiny_binary(f,a,b)?768:0;
            if(adjustment) {
                /* Product: scale both in the same direction. Quotient: opposite. */
                double sa=scalbn(a,adjustment),sb=scalbn(b,f==op_mul?adjustment:-adjustment);
                fp_begin(c->fpscr&3);r=f(sa,sb,x);int adjusted=fp_end();fp_flags(c,adjusted);
                fp_raise(c,adjustment<0?OX:UX);
                if((adjusted&FE_INEXACT)&&fp_fr_on){fp_begin(1);volatile double z=f(sa,sb,x);fp_end();fp_fr(c,adjusted,fabs(r)>fabs(z));}
                return d2b(r);
            }
        }
        fp_flags(c, e);
        if (e & FE_INEXACT && fp_fr_on) { fp_begin(1); volatile double z = f(a, b, x); fp_end(); fp_fr(c, e, fabs(r) > fabs(z)); }
        return d2b(r);
    }
    fp_begin(1); volatile double r = f(a, b, x); int e1 = fp_end();
    if (r == 0 && !(e1 & FE_INEXACT)) { fp_begin(c->fpscr & 3); r = f(a, b, x); fp_end(); }   // exact zero: the sign depends on RN (-0 with RN = -inf)
    uint64_t rb = d2b(r);
    if (e1 & FE_INEXACT && isfinite(r)) rb |= 1;                 // sticky bit
    (void)e; return to_single(c, b2d(rb), e1);
}
static uint64_t sgl2dbl(uint32_t w) {                           // lfs: exact, an SNaN is not quieted
    if ((w >> 23 & 0xff) == 0xff) return (uint64_t)(w >> 31) << 63 | 0x7FF0000000000000ull | (uint64_t)(w & 0x7fffff) << 29;
    float x; memcpy(&x, &w, 4); return d2b((double)x);
}
static uint32_t dbl2sgl(uint64_t b) {                           // stfs: per Book I, no rounding
    int e = (int)(b >> 52 & 0x7ff);
    if (e > 896 || !(b << 1)) return (uint32_t)(b >> 62) << 30 | (uint32_t)(b >> 29 & 0x3FFFFFFF);
    if (e >= 874) {                                             // denormalization
        uint64_t frac = 1ull << 52 | (b & 0xFFFFFFFFFFFFFull); int ex = e - 1023;
        while (ex < -126) { frac >>= 1; ex++; }
        return (uint32_t)(b >> 63) << 31 | (uint32_t)(frac >> 29 & 0x7FFFFF);
    }
    return (uint32_t)(b >> 63) << 31;                           // architecturally undefined: signed zero
}
static void fcmp(CPU *c, int f, uint64_t ab, uint64_t bb, int ordered) {
    double a = b2d(ab), b = b2d(bb);
    uint32_t n = is_nan(ab) || is_nan(bb) ? 1 : a < b ? 8 : a > b ? 4 : 2;
    cr_set_field(c, f, n);
    c->fpscr = (c->fpscr & ~0xF000u) | n << 12;
    uint32_t x = 0;
    if (is_snan(ab) || is_snan(bb)) x |= VXSNAN | (ordered && !(c->fpscr & VE) ? VXVC : 0);
    else if (ordered && n == 1) x |= VXVC;
    if (x) fp_raise(c, x);
}
static int fct_inv;                                            // the last fctiw was invalid
static uint32_t fctiw(CPU *c, uint64_t xb, int tz) {
    fct_inv = 1;
    double x = b2d(xb);
    if (is_nan(xb)) { fp_raise(c, VXCVI | (is_snan(xb) ? VXSNAN : 0)); c->fpscr &= ~(FR | FI); return 0x80000000u; }
    fp_begin(tz ? 1 : c->fpscr & 3); volatile double r = nearbyint(x); fp_end();
    if (r > 2147483647.0 || r < -2147483648.0) { fp_raise(c, VXCVI); c->fpscr &= ~(FR | FI); return r > 0 ? 0x7fffffff : 0x80000000u; }
    fct_inv = 0; c->fpscr &= ~(FR | FI);
    if (r != x) { c->fpscr |= FI; fp_fr(c, FE_INEXACT, fabs(r) > fabs(x)); fp_raise(c, XX); }
    return (uint32_t)(int32_t)r;
}
static void ov_set(CPU *c, int ov) { c->ov = ov; c->so |= ov; }

// XO-form arithmetic (op 31); returns 1 if xo9 is recognized.
static int arith(CPU *c, uint32_t in, int d, int a, int b) {
    uint32_t x = R(a), y = R(b), r; uint64_t w; int oe = in >> 10 & 1, ov = 0;
    switch (in >> 1 & 0x1ff) {
    case 266: r = x + y; ov = (~(x ^ y) & (x ^ r)) >> 31; break;                       // add
    case 10:  w = (uint64_t)x + y; r = (uint32_t)w; c->ca = w >> 32; ov = (~(x ^ y) & (x ^ r)) >> 31; break; // addc
    case 138: w = (uint64_t)x + y + c->ca; r = (uint32_t)w; c->ca = w >> 32; ov = (~(x ^ y) & (x ^ r)) >> 31; break; // adde
    case 202: w = (uint64_t)x + c->ca; r = (uint32_t)w; c->ca = w >> 32; ov = (~x & r) >> 31; break;            // addze
    case 234: w = (uint64_t)x + c->ca + 0xFFFFFFFFu; r = (uint32_t)w; c->ca = w >> 32; ov = (x & ~r) >> 31; break; // addme
    case 40:  r = y - x; ov = ((x ^ y) & (y ^ r)) >> 31; break;                        // subf
    case 8:   w = (uint64_t)(~x) + y + 1; r = (uint32_t)w; c->ca = w >> 32; ov = ((x ^ y) & (y ^ r)) >> 31; break;  // subfc
    case 136: w = (uint64_t)(~x) + y + c->ca; r = (uint32_t)w; c->ca = w >> 32; ov = ((x ^ y) & (y ^ r)) >> 31; break; // subfe
    case 200: w = (uint64_t)(~x) + c->ca; r = (uint32_t)w; c->ca = w >> 32; ov = (x & r) >> 31; break;          // subfze
    case 232: w = (uint64_t)(~x) + c->ca + 0xFFFFFFFFu; r = (uint32_t)w; c->ca = w >> 32; ov = (~x & ~r) >> 31; break; // subfme
    case 104: r = -x; ov = x == 0x80000000u; break;                                     // neg
    case 235: { int64_t p = (int64_t)(int32_t)x * (int32_t)y; r = (uint32_t)p; ov = p != (int32_t)p; break; } // mullw
    case 75:  r = (uint32_t)(((int64_t)(int32_t)x * (int32_t)y) >> 32); break;          // mulhw
    case 11:  r = (uint32_t)(((uint64_t)x * y) >> 32); break;                           // mulhwu
    case 491: if (y == 0 || (x == 0x80000000u && y == 0xFFFFFFFFu)) { r = y ? x : 0; ov = 1; } else r = (uint32_t)((int32_t)x / (int32_t)y); break; // divw (undefined results as ARM sdiv)
    case 459: if (y == 0) { r = 0; ov = 1; } else r = x / y; break;                     // divwu
    default: return 0;
    }
    R(d) = r;
    if (oe) ov_set(c, ov);
    if (in & 1) cr0(c, r);
    return 1;
}

static int unimpl(CPU *c, uint32_t in) {
    if (c->sys) {                                                                       // program exception: illegal instruction
        static int shown;                                                               // an engine gap shows at once (first 20)
        if (trace_unimpl && shown < 20) { shown++; fprintf(stderr, "ptolemy: invalid instruction %08x (op %u/%u) @%08x -> exception 0x700\n", in, in >> 26, in >> 1 & 0x3ff, c->pc); }
        ppc_exception(c, 0x700, c->pc, 0x80000); return 0; }
    if (trace_unimpl) fprintf(stderr, "ptolemy: unknown instruction %08x (op %u/%u) @%08x\n", in, in >> 26, in >> 1 & 0x3ff, c->pc);
    c->halted = 1; c->exit_code = 128;
    return -1;
}

static void csync(CPU *c) { if (c->icbi_n) c->cgen++; }   // context synchronization after icbi: the JIT drops the code of the icbi_ea pages
// 750 exception: SRR1 = MSR[16-31] without reserved bits + reason; the new MSR keeps only ME, IP, ILE (LE <- ILE).
extern uint32_t ppc_exc_ring[64][8]; extern unsigned ppc_exc_n;   /* recent exceptions for the user-DSI-near-0 diagnostics (mmu.c) */
void ppc_exception(CPU *c, uint32_t vec, uint32_t srr0, uint32_t bits) {
    uint32_t *e = ppc_exc_ring[ppc_exc_n++ & 63];
    e[0] = vec; e[1] = srr0; e[2] = c->msr; e[3] = c->spr[SPR_DAR]; e[4] = c->spr[SPR_DSISR]; e[5] = c->gpr[1]; e[6] = c->gpr[3]; e[7] = c->gpr[31];
    c->spr[SPR_SRR0] = srr0;
    c->spr[SPR_SRR1] = (c->msr & (0xFF77 | (ppc_has_vec(c)?MSR_VEC:0))) | bits;
    c->msr = (c->msr & (MSR_ME | MSR_IP | MSR_ILE)) | (c->msr & MSR_ILE ? MSR_LE : 0);
    c->pc = (c->msr & MSR_IP ? 0xFFF00000u : 0) | vec;
    csync(c);
}
static int program(CPU *c, uint32_t in, uint32_t why) {        // why (SRR1): 0x80000 illegal, 0x40000 privileged, 0x20000 trap
    if (why == 0x80000) return unimpl(c, in);                                   // unimpl delivers the exception itself when sys
    if (c->sys) { ppc_exception(c, 0x700, c->pc, why); return 0; }
    fprintf(stderr, "ptolemy: %s @%08x\n", why == 0x20000 ? "trap" : "privileged instruction", c->pc);
    c->halted = 1; c->exit_code = 134; return 1;
}
static void setmsr(CPU *c, uint32_t v) {                       // mtmsr/rfi: EE 0->1 or POW: check interrupts
    uint32_t o = c->msr; c->msr = v & ppc_msr_mask(c);
    if ((~o & c->msr & MSR_EE) || c->msr & MSR_POW) c->irq = 1;
}
#define PRIV() do { if (c->msr & MSR_PR) return program(c, in, 0x40000); } while (0)
static int trap_if(int to, uint32_t a, uint32_t b) {
    return (to & 16 && (int32_t)a < (int32_t)b) || (to & 8 && (int32_t)a > (int32_t)b) || (to & 4 && a == b) || (to & 2 && a < b) || (to & 1 && a > b);
}
static int spr_ok(CPU *c,int n) {                                     // SPRs the CPU has (750, plus the G4/7447A ones above)
    if(ppc_has_vec(c) && n==256)return 1; /* VRSAVE is user accessible. */
    if(ppc_has_vec(c) && (n==928 || n==935 || n==944 || n==951 || n==1014 || n==1015 || n==1023))return 1;
    if(ppc_7447a(c) && ((n>=560 && n<=575) || (n>=980 && n<=982) || n==929 || n==930 || n==945 || n==946 || n==1011 || n==1016 || n==1018))return 1;
    static const uint16_t L[] = {1, 8, 9, 18, 19, 22, 25, 26, 27, 268, 269, 272, 273, 274, 275, 282, 284, 285, 287,
                                 1008, 1009, 1010, 1013, 1017, 1019, 1020, 1021, 1022};
    if ((n >= 528 && n <= 543) || (n >= 936 && n <= 942) || (n >= 952 && n <= 958)) return 1;   // BAT, UMMCR/UPMC, MMCR/PMC
    for (unsigned i = 0; i < sizeof L / sizeof *L; i++) if (L[i] == n) return 1;
    return 0;
}
// Default clock (the environment may supply its own through env_spr/env_irq): TB at 25 MHz from the host monotonic clock; DEC from TB.
static uint64_t tbnow(CPU *c) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return ((uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec) / 40 + c->tb_off; }
static uint32_t std_spr(CPU *c, int spr, int write, uint32_t v) {
    uint64_t t = tbnow(c); uint32_t dec = c->decv - (uint32_t)(t - c->dec0);
    switch (spr) {
    case 268: return (uint32_t)t;
    case 269: return (uint32_t)(t >> 32);
    case 284: c->tb_off += (t & ~0xFFFFFFFFull) + v - t; return 0;                  // TBL
    case 285: c->tb_off += ((uint64_t)v << 32 | (uint32_t)t) - t; return 0;           // TBU
    }
    if (!write) return dec;
    if ((int32_t)v < 0 && (int32_t)dec >= 0) c->dec_pend = 1;                         // the write set bit 0 (0 -> 1)
    c->dec0 = t; c->decv = v; c->dec_armed = (int32_t)v >= 0;
    return 0;
}
static uint32_t std_irq(CPU *c, uint32_t ack) {
    if (c->dec_armed && tbnow(c) - c->dec0 > c->decv) { c->dec_armed = 0; c->dec_pend = 1; }   // DEC passed 0 -> -1
    if (ack & PEND_DEC) c->dec_pend = 0;
    return c->dec_pend ? PEND_DEC : 0;
}
// A DBAT changes (XNU sets DBAT0/1 while it copies physical pages): the pages of the old and the new block go to the
// tlbie list with bit 0 = "data only" (their code is not dropped); a block larger than 512 KB flushes everything.
static void dbat_inv(CPU *c, int spr, uint32_t s) {
    int u = spr & ~1; uint32_t v[2] = {c->spr[u], spr == u ? s : c->spr[u]};
    for (int k = 0; k < 2 - (v[0] == v[1]); k++) {
        if (!(v[k] & 3)) continue;
        uint32_t base = v[k] & 0xFFFE0000u, n = ((v[k] >> 2 & 0x7FF) + 1) << 5;
        if (n > 128) { c->xgen++; return; }
        for (uint32_t i = 0; i < n; i++) { if (c->tlbie_n < 512) c->tlbie_ea[c->tlbie_n] = (base + (i << 12)) | 1; c->tlbie_n++; }
    }
}
static uint32_t tspr(CPU *c, int spr, int write, uint32_t v) { return c->env_spr ? c->env_spr(c, spr, write, v) : std_spr(c, spr, write, v); }
static uint32_t pending(CPU *c, uint32_t ack) { return c->env_irq ? c->env_irq(c, ack) : std_irq(c, ack); }
// Interrupts are checked between instructions once the environment has raised c->irq (the JIT polls it on block entry).
// External beats DEC; with EE=0 they stay pending. MSR.POW sleeps until an interrupt.
void ppc_irq(CPU *c) {
    c->irq = 0;
    uint32_t p = pending(c, 0);
    while (c->msr & MSR_POW && c->spr[1008] & (ppc_7447a(c)?0x00600000:0x00E00000) && !(p && c->msr & MSR_EE)) {   // 7447A bit 8 is HIGH_BAT_EN, not DOZE
        if (c->env_idle) c->env_idle(c);
        else if (c->dec_armed && c->msr & MSR_EE) {                                    // sleep until DEC fires
            int64_t w = ((int64_t)c->decv - (int64_t)(tbnow(c) - c->dec0) + 1) * 40;
            if (w > 0) { struct timespec ts = {(time_t)(w / 1000000000), (long)(w % 1000000000)}; nanosleep(&ts, 0); } }
        else { fprintf(stderr, "ptolemy: MSR.POW without a wake source (EE=0 or DEC unarmed) @%08x HID0 %08x MSR %08x\n", c->pc, c->spr[1008], c->msr); c->halted = 1; return; }
        p = pending(c, 0);
        if (c->halted) return;
    }
    c->pend_masked = p && !(c->msr & MSR_EE);
    if (!p || !(c->msr & MSR_EE)) return;
    if (p & PEND_EXT) { ppc_exception(c, 0x500, c->pc, 0);                          // the environment holds the level;
                        c->pend_masked = !!(p & PEND_DEC); return; }                 // DEC now waits for EE: JIT rfi/mtmsr must exit
    pending(c, PEND_DEC); ppc_exception(c, 0x900, c->pc, 0);
}

// The memory window an instruction stores to (for the PT_CHECK comparison); 0: not a store.
int ppc_store_window(const CPU *c, uint32_t in, uint32_t *ea) {
    int op = in >> 26, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, xo = in >> 1 & 0x3ff;
    uint32_t base = a ? c->gpr[a] : 0;
    switch (op) {
    case 36: case 37: *ea = base + (int16_t)in; return 4;  case 38: case 39: *ea = base + (int16_t)in; return 1;
    case 44: case 45: *ea = base + (int16_t)in; return 2;  case 47: *ea = base + (int16_t)in; return 4 * (32 - d);
    case 52: case 53: *ea = base + (int16_t)in; return 4;  case 54: case 55: *ea = base + (int16_t)in; return 8;
    case 31: *ea = base + c->gpr[b];
        switch (xo) { case 151: case 183: case 662: case 150: case 663: case 695: case 983: return 4;
                      case 215: case 247: return 1; case 407: case 439: case 918: return 2; case 727: case 759: return 8;
                      case 135: return 1; case 167: *ea &= ~1u; return 2; case 199: *ea &= ~3u; return 4;
                      case 231: case 487: *ea &= ~15u; return 16;
                      case 1014: *ea &= ~31u; return 32; }
    }
    return 0;
}

/* FPU instruction (op 59/63) in: 0 executed, -1 not implemented. Split out of interp_step for ppc_fp_settle, which
 * replays a JIT fast operation from its recorded inputs. */
static int fp_exec(CPU *c, uint32_t in) {
    int op = in >> 26, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, rc = in & 1;
    int C = in >> 6 & 31, xo5 = in >> 1 & 31, xo = in >> 1 & 0x3ff, sgl = op == 59;
    uint64_t ab = fbits(c, a), bb = fbits(c, b), cb = fbits(c, C), r;
    double fa = b2d(ab), fb = b2d(bb), fc = b2d(cb);
    // A-form: which operands take part (PowerPC NaN precedence: A, B, C)
    int ua = 0, ub = 0, uc = 0; fp2 fn = 0; double x1 = 0, x2 = 0, x3 = 0; uint32_t inv = 0; int neg = 0;
    switch (xo5) {
    case 21: ua = ub = 1; fn = op_add; x1 = fa; x2 = fb; if (isinf(fa) && isinf(fb) && signbit(fa) != signbit(fb)) inv = VXISI; break;
    case 20: ua = ub = 1; fn = op_sub; x1 = fa; x2 = fb; if (isinf(fa) && isinf(fb) && signbit(fa) == signbit(fb)) inv = VXISI; break;
    case 25: ua = uc = 1; fn = op_mul; x1 = fa; x2 = fc; if ((isinf(fa) && fc == 0) || (fa == 0 && isinf(fc))) inv = VXIMZ; break;
    case 18: ua = ub = 1; fn = op_div; x1 = fa; x2 = fb; if (isinf(fa) && isinf(fb)) inv = VXIDI; else if (fa == 0 && fb == 0) inv = VXZDZ; break;
    case 22: return -1;                                                                   // fsqrt(s): not on the 750/7400/7450
    case 24: if (!sgl) return -1; ub = 1; fn = op_rec; x2 = fb; break;                    // fres (more precise than the hardware estimate); no fre
    case 26: if (sgl) return -1; ub = 1; fn = op_rsq; x2 = fb; if (fb < 0) inv = VXSQRT; break; // frsqrte (more precise than the estimate)
    case 28: case 29: case 30: case 31: {                                                            // fmsub fmadd fnmsub fnmadd
        ua = ub = uc = 1; fn = op_fma; x1 = fa; x2 = fc; x3 = xo5 & 1 ? fb : -fb; neg = xo5 >= 30;
        if ((isinf(fa) && fc == 0) || (fa == 0 && isinf(fc))) inv = VXIMZ;
        else if ((isinf(fa) || isinf(fc)) && isinf(fb) && (signbit(fa) ^ signbit(fc)) != (int)signbit(x3)) inv = VXISI;
        break; }
    case 23: fsetb(c, d, !is_nan(ab) && fa >= 0.0 ? cb : bb); goto frc;                             // fsel
    }
    if (fn) {
        uint64_t nan = 0; int snan = 0;
        if (ua && is_nan(ab)) nan = ab; else if (ub && is_nan(bb)) nan = bb; else if (uc && is_nan(cb)) nan = cb;
        snan = (ua && is_snan(ab)) || (ub && is_snan(bb)) || (uc && is_snan(cb));
        if (nan) {
            if (snan) fp_raise(c, VXSNAN);
            c->fpscr &= ~(FR | FI);                                                                  // Book I: FR = FI = 0 also when VE=1
            if (snan && c->fpscr & VE) goto frc;                                                     // VE=1: FRT unchanged
            r = nan | 0x8000000000000ull;
            if (sgl) r &= ~0x1FFFFFFFull;
            c->fpscr &= ~(FR | FI);
        } else if (inv) {
            fp_raise(c, inv); c->fpscr &= ~(FR | FI);
            if (c->fpscr & VE) goto frc;
            r = 0x7FF8000000000000ull;
        } else {
            r = fp_compute(c, fn, x1, x2, x3, sgl);
            if (xo5 == 24 || xo5 == 26) c->fpscr &= ~FR;                                             // estimates: FR undefined, 0
            if ((xo5 == 18 || xo5 == 24 || xo5 == 26) && fb == 0 && c->fpscr & ZE) goto frc;        // ZE=1: FRT unchanged
            if (neg) r ^= 1ull << 63;
        }
        fsetb(c, d, r); set_fprf(c, r, sgl);
        goto frc;                                                                                    // OE/UE=1: single scaled (to_single); double mul/div only with PT_FP_RANGE; no FE0/FE1 traps
    }
    if (sgl) return -1;
    switch (xo) {
    case 0: case 32: fcmp(c, d >> 2, ab, bb, xo == 32); return 0;
    case 12: {                                                                                        // frsp
        if (is_nan(bb)) { if (is_snan(bb)) fp_raise(c, VXSNAN); c->fpscr &= ~(FR | FI); if (is_snan(bb) && c->fpscr & VE) goto frc;
                          r = (bb | 0x8000000000000ull) & ~0x1FFFFFFFull; c->fpscr &= ~(FR | FI); }
        else r = to_single(c, fb, 0);
        fsetb(c, d, r); set_fprf(c, r, 1); goto frc; }
    case 14: case 15: { uint32_t v = fctiw(c, bb, xo == 15);
                        if (!(fct_inv && c->fpscr & VE)) fsetb(c, d, 0xFFF8000000000000ull | v);   // VE=1: FRT unchanged
                        goto frc; }
    case 40: fsetb(c, d, bb ^ 1ull << 63); goto frc;                                                 // fneg
    case 72: fsetb(c, d, bb); goto frc;                                                              // fmr
    case 136: fsetb(c, d, bb | 1ull << 63); goto frc;                                                // fnabs
    case 264: fsetb(c, d, bb & ~(1ull << 63)); goto frc;                                             // fabs
    case 583: fsetb(c, d, 0xFFF8000000000000ull | c->fpscr); goto frc;                              // mffs
    case 711: { int fm = in >> 17 & 0xff; uint32_t v = (uint32_t)bb, m = 0;                          // mtfsf
                for (int i = 0; i < 8; i++) if (fm & (0x80 >> i)) m |= 0xFu << (28 - 4 * i);
                m &= ~(FEX | VX); c->fpscr = (c->fpscr & ~m) | (v & m); fp_summ(c); goto frc; }
    case 134: { int f = d >> 2; uint32_t im = b >> 1 & 15, m = 0xFu << (28 - 4 * f) & ~(FEX | VX);   // mtfsfi
                c->fpscr = (c->fpscr & ~m) | (im << (28 - 4 * f) & m); fp_summ(c); goto frc; }
    case 38: if (d != 1 && d != 2) { uint32_t bit = 0x80000000u >> d; if (bit & EXC) fp_raise(c, bit); else c->fpscr |= bit; fp_summ(c); } goto frc; // mtfsb1
    case 70: if (d != 1 && d != 2) { c->fpscr &= ~(0x80000000u >> d); fp_summ(c); } goto frc;       // mtfsb0
    case 64: { int s4 = 28 - 4 * (a >> 2); cr_set_field(c, d >> 2, c->fpscr >> s4 & 15);              // mcrfs: copies and clears
               c->fpscr &= ~((0xFu << s4) & (FX | EXC)); fp_summ(c); return 0; }
    default: return -1;
    }
frc: if (rc) cr_set_field(c, 1, c->fpscr >> 28);
    return 0;
}
/* The FPSCR status the JIT owes for its last fast FP operation (fs_kind, ppc.h): the interpreter's own arithmetic on
 * the recorded inputs gives FR, FI and FPRF (C only, when fcmpu has written FPCC since). Sticky bits cannot change:
 * the fast path ran with XX = 1 and without exceptions. Every C reader of fpscr calls this first. */
void ppc_fp_settle(CPU *c) {
    unsigned k = c->fs_kind & 0xff; if (!k) return;
    CPU s; s.fpscr = c->fpscr; memcpy(s.fpr, c->fs_x, 3 * sizeof(double));
    if (fp_exec(&s, (k & 0x20 ? 59u : 63u) << 26 | 3u << 21 | 0u << 16 | 1u << 11 | 2u << 6 | (k & 31) << 1)) abort();
    uint32_t keep = FR | FI | (c->fs_kind >> 8 ? 0x10000u : FPRF);
    c->fpscr = (c->fpscr & ~keep) | (s.fpscr & keep); c->fs_kind = 0;
}
int interp_step(CPU *c) {
    if (c->irq) { ppc_irq(c); if (c->halted) return 1; }
    uint32_t pc = c->pc, in, npc = pc + 4, ea;
    if (ppc_ifetch(c, pc, &in)) return 0;                                                            // ISI
    int op = in >> 26, d = in >> 21 & 31, a = in >> 16 & 31, b = in >> 11 & 31, rc = in & 1;
    int32_t simm = (int16_t)in; uint32_t uimm = in & 0xffff;
    c->icount++;
    if (c->sys && !(c->msr & MSR_FP) && ((op >= 48 && op <= 55) || op == 59 || op == 63 ||
        (op == 31 && ((in >> 1 & 0x3ff) == 983 || ((in >> 1 & 0x21f) == 0x217)))))  { ppc_exception(c, 0x800, pc, 0); return 0; }   // FP unavailable
    switch (op) {
    case 0:
        if(c->env_hypercall && c->env_hypercall(c,in))break;
        return unimpl(c,in);
    case 4:
        if(!ppc_has_vec(c))return unimpl(c,in);
        if(c->sys && !(c->msr&MSR_VEC)){ppc_exception(c,0xf20,pc,0);return 0;}
        if(!ppc_altivec(c,in))return unimpl(c,in);break;
    case 3:  if (trap_if(d, R(a), (uint32_t)simm)) return program(c, in, 0x20000); break;                    // twi
    case 7:  R(d) = R(a) * (uint32_t)simm; break;                                                    // mulli (modulo 2^32, no UB)
    case 8:  { uint64_t w = (uint64_t)(~R(a)) + (uint32_t)simm + 1; R(d) = (uint32_t)w; c->ca = w >> 32; break; } // subfic
    case 10: cmp_u(c, d >> 2, R(a), uimm); break;                                                   // cmpli
    case 11: cmp_s(c, d >> 2, R(a), (uint32_t)simm); break;                                         // cmpi
    case 12: case 13: { uint64_t w = (uint64_t)R(a) + (uint32_t)simm; R(d) = (uint32_t)w; c->ca = w >> 32; if (op == 13) cr0(c, R(d)); break; } // addic(.)
    case 14: R(d) = RA0(a) + simm; break;                                                            // addi
    case 15: R(d) = RA0(a) + (uimm << 16); break;                                                    // addis
    case 16: case 18: case 19: {                                                                     // branches
        int bo = d, bi = a, lk = in & 1; uint32_t tgt;
        if (op == 18) { int32_t li = (int32_t)(in << 6) >> 6 & ~3; tgt = (in & 2 ? 0 : pc) + li; }
        else {
            if (op == 19) {
                int xo = in >> 1 & 0x3ff;
                if (xo == 16) tgt = c->lr & ~3u;                // bclr
                else if (xo == 528) tgt = c->ctr & ~3u;         // bcctr
                else {                                          // CR logic, mcrf, isync
                    int x = cr_bit(c, a), y = cr_bit(c, b), r;
                    switch (xo) {
                    case 0: cr_set_field(c, d >> 2, cr_field(c, a >> 2)); goto next;   // mcrf
                    case 150: csync(c); goto next;                                      // isync
                    case 50: PRIV(); setmsr(c, c->spr[SPR_SRR1] & ~MSR_POW); npc = c->spr[SPR_SRR0] & ~3u; csync(c); goto next;   // rfi
                    case 257: r = x & y; break;  case 449: r = x | y; break;  case 193: r = x ^ y; break;
                    case 225: r = !(x & y); break; case 33: r = !(x | y); break; case 289: r = x == y; break;
                    case 129: r = x & !y; break; case 417: r = x | !y; break;
                    default: return unimpl(c, in);
                    }
                    cr_set_bit(c, d, r); goto next;
                }
            } else tgt = (in & 2 ? 0 : pc) + (int32_t)(int16_t)(in & 0xfffc);
            if (!(bo & 4)) c->ctr--;
            int ok = ((bo & 4) || ((c->ctr != 0) ^ (bo >> 1 & 1))) && ((bo & 16) || cr_bit(c, bi) == (bo >> 3 & 1));
            if (!ok) tgt = npc;
        }
        if (lk) c->lr = npc;
        if (c->sys == 1 && op == 18 && tgt == pc && !lk) { c->halted = 1; return 1; }                // stand: `b .` ends the test
        npc = tgt; break;
    }
    case 17: c->pc = npc; return sys_call(c);                                                        // sc
    case 20: { uint32_t m = ppc_mask(in >> 6 & 31, in >> 1 & 31); R(a) = (rotl(R(d), b) & m) | (R(a) & ~m); if (rc) cr0(c, R(a)); break; } // rlwimi
    case 21: R(a) = rotl(R(d), b) & ppc_mask(in >> 6 & 31, in >> 1 & 31); if (rc) cr0(c, R(a)); break;            // rlwinm
    case 23: R(a) = rotl(R(d), R(b) & 31) & ppc_mask(in >> 6 & 31, in >> 1 & 31); if (rc) cr0(c, R(a)); break;    // rlwnm
    case 24: R(a) = R(d) | uimm; break;
    case 25: R(a) = R(d) | uimm << 16; break;
    case 26: R(a) = R(d) ^ uimm; break;
    case 27: R(a) = R(d) ^ uimm << 16; break;
    case 28: R(a) = R(d) & uimm; cr0(c, R(a)); break;
    case 29: R(a) = R(d) & uimm << 16; cr0(c, R(a)); break;
    case 32: case 33: ea = RA0(a) + simm; R(d) = LD(4, ea); if (op & 1) R(a) = ea; break;            // lwz(u)
    case 34: case 35: ea = RA0(a) + simm; R(d) = LD(1, ea); if (op & 1) R(a) = ea; break;            // lbz(u)
    case 36: case 37: ea = RA0(a) + simm; ST(4, ea, R(d)); if (op & 1) R(a) = ea; break;             // stw(u)
    case 38: case 39: ea = RA0(a) + simm; ST(1, ea, R(d)); if (op & 1) R(a) = ea; break;             // stb(u)
    case 40: case 41: ea = RA0(a) + simm; R(d) = LD(2, ea); if (op & 1) R(a) = ea; break;            // lhz(u)
    case 42: case 43: ea = RA0(a) + simm; R(d) = (uint32_t)(int16_t)LD(2, ea); if (op & 1) R(a) = ea; break; // lha(u)
    case 44: case 45: ea = RA0(a) + simm; ST(2, ea, R(d)); if (op & 1) R(a) = ea; break;             // sth(u)
    case 46: ea = RA0(a) + simm; for (int r = d; r < 32; r++, ea += 4) R(r) = LD(4, ea); break;    // lmw
    case 47: ea = RA0(a) + simm; for (int r = d; r < 32; r++, ea += 4) ST(4, ea, R(r)); break;     // stmw
    case 48: case 49: ea = RA0(a) + simm; fsetb(c, d, sgl2dbl(LD(4, ea))); if (op & 1) R(a) = ea; break;   // lfs(u)
    case 50: case 51: ea = RA0(a) + simm; fsetb(c, d, LD(8, ea)); if (op & 1) R(a) = ea; break;     // lfd(u)
    case 52: case 53: ea = RA0(a) + simm; ST(4, ea, dbl2sgl(fbits(c, d))); if (op & 1) R(a) = ea; break;   // stfs(u)
    case 54: case 55: ea = RA0(a) + simm; ST(8, ea, fbits(c, d)); if (op & 1) R(a) = ea; break;     // stfd(u)
    case 31: {
        int xo = in >> 1 & 0x3ff;
        int vec=ppc_altivec_mem(c,in);if(vec>=0){if(vec)return 0;break;}
        if (arith(c, in, d, a, b)) break;
        uint32_t s = R(d), r;
        switch (xo) {
        case 0:   cmp_s(c, d >> 2, R(a), R(b)); break;
        case 32:  cmp_u(c, d >> 2, R(a), R(b)); break;
        case 4:   if (trap_if(d, R(a), R(b))) return program(c, in, 0x20000); break;                           // tw
        case 83:  PRIV(); R(d) = c->msr; break;                                                           // mfmsr
        case 146: PRIV(); setmsr(c, s); break;                                                            // mtmsr
        case 595: PRIV(); R(d) = c->sr[a & 15]; break;                                                    // mfsr
        case 659: PRIV(); R(d) = c->sr[R(b) >> 28]; break;                                                // mfsrin
        case 210: PRIV(); if (c->sr[a & 15] != s) { c->sr[a & 15] = s; c->srgen++; } break;              // mtsr: the JIT switches the segment
        case 242: PRIV(); if (c->sr[R(b) >> 28] != s) { c->sr[R(b) >> 28] = s; c->srgen++; } break;      // mtsrin
        case 306: PRIV(); ppc_tlb_invalidate(c,R(b)); if (c->tlbie_n < 512) c->tlbie_ea[c->tlbie_n] = R(b); c->tlbie_n++; break;
        case 978: case 1010:
            if(!ppc_7447a(c))return unimpl(c,in);
            PRIV();ppc_tlb_load(c,R(b),xo==1010);break;
        case 310: case 438: { /* EAR selects a separate external-control bus. */
            uint32_t v=R(d);if(ppc_external(c,RA0(a)+R(b),xo==438,&v))return 0;
            if(xo==310)R(d)=v;break;
        }
        case 566: case 470: PRIV(); break;                                                                // tlbsync, dcbi
        case 28:  r = s & R(b); goto logic;   case 60:  r = s & ~R(b); goto logic;
        case 444: r = s | R(b); goto logic;   case 412: r = s | ~R(b); goto logic;
        case 316: r = s ^ R(b); goto logic;   case 476: r = ~(s & R(b)); goto logic;
        case 124: r = ~(s | R(b)); goto logic; case 284: r = ~(s ^ R(b)); goto logic;
        case 954: r = (uint32_t)(int8_t)s; goto logic;  case 922: r = (uint32_t)(int16_t)s; goto logic;
        case 26:  r = s ? __builtin_clz(s) : 32; goto logic;
        case 24:  r = R(b) & 32 ? 0 : s << (R(b) & 31); goto logic;                               // slw
        case 536: r = R(b) & 32 ? 0 : s >> (R(b) & 31); goto logic;                               // srw
        case 792: { int n = R(b) & 63; if (n > 31) { r = (int32_t)s >> 31; c->ca = s >> 31; }      // sraw
                    else { r = (uint32_t)((int32_t)s >> n); c->ca = (int32_t)s < 0 && n && (s << (32 - n)); } goto logic; }
        case 824: { int n = b; r = (uint32_t)((int32_t)s >> n); c->ca = (int32_t)s < 0 && n && (s << (32 - n)); goto logic; } // srawi
        logic: R(a) = r; if (rc) cr0(c, r); break;
        case 23: case 55: ea = RA0(a) + R(b); R(d) = LD(4, ea); if (xo & 32) R(a) = ea; break;            // lwzx/lwzux
        case 87: case 119: ea = RA0(a) + R(b); R(d) = LD(1, ea); if (xo & 32) R(a) = ea; break;           // lbzx/lbzux
        case 279: case 311: ea = RA0(a) + R(b); R(d) = LD(2, ea); if (xo & 32) R(a) = ea; break;          // lhzx/lhzux
        case 343: case 375: ea = RA0(a) + R(b); R(d) = (uint32_t)(int16_t)LD(2, ea); if (xo & 32) R(a) = ea; break; // lhax/lhaux
        case 151: case 183: ea = RA0(a) + R(b); ST(4, ea, s); if (xo & 32) R(a) = ea; break;              // stwx/stwux
        case 215: case 247: ea = RA0(a) + R(b); ST(1, ea, s); if (xo & 32) R(a) = ea; break;              // stbx/stbux
        case 407: case 439: ea = RA0(a) + R(b); ST(2, ea, s); if (xo & 32) R(a) = ea; break;              // sthx/sthux
        case 534: R(d) = __builtin_bswap32((uint32_t)LD(4, RA0(a) + R(b))); break;                        // lwbrx
        case 597: case 533: {                                // lswi, lswx: bytes into registers from the high byte, r0 follows r31
            int n = xo == 597 ? (b ? b : 32) : (int)c->xer_bc; uint8_t m[128];
            ea = RA0(a) + (xo == 533 ? R(b) : 0);
            for (int i = 0; i < n; i++) m[i] = (uint8_t)LD(1, ea + i);                   // read everything first: a DSI leaves no trace
            for (int i = 0; i < n; i += 4) { uint32_t v = 0; for (int k = 0; k < 4; k++) v = v << 8 | (i + k < n ? m[i + k] : 0);
                                             c->gpr[(d + i / 4) & 31] = v; }
            break; }
        case 725: case 661: {                                // stswi, stswx
            int n = xo == 725 ? (b ? b : 32) : (int)c->xer_bc;
            ea = RA0(a) + (xo == 661 ? R(b) : 0);
            for (int i = 0; i < n; i++) ST(1, ea + i, c->gpr[(d + i / 4) & 31] >> (24 - 8 * (i & 3)) & 0xff);
            break; }
        case 662: ST(4, RA0(a) + R(b), __builtin_bswap32(s)); break;                                      // stwbrx
        case 790: R(d) = __builtin_bswap16((uint16_t)LD(2, RA0(a) + R(b))); break;                        // lhbrx
        case 918: ST(2, RA0(a) + R(b), __builtin_bswap16((uint16_t)s)); break;                            // sthbrx
        case 20:  ea = RA0(a) + R(b); R(d) = LD(4, ea); c->reserve = 1; c->reserve_addr = ea; break;      // lwarx
        case 150: ea = RA0(a) + R(b); { int ok = c->reserve && c->reserve_addr == ea; if (ok) ST(4, ea, s);
                  c->reserve = 0; cr_set_field(c, 0, (ok ? 2 : 0) | c->so); } break;                     // stwcx.
        case 535: case 567: ea = RA0(a) + R(b); fsetb(c, d, sgl2dbl(LD(4, ea))); if (xo & 32) R(a) = ea; break;   // lfsx/lfsux
        case 599: case 631: ea = RA0(a) + R(b); fsetb(c, d, LD(8, ea)); if (xo & 32) R(a) = ea; break;   // lfdx/lfdux
        case 663: case 695: ea = RA0(a) + R(b); ST(4, ea, dbl2sgl(fbits(c, d))); if (xo & 32) R(a) = ea; break;   // stfsx/stfsux
        case 727: case 759: ea = RA0(a) + R(b); ST(8, ea, fbits(c, d)); if (xo & 32) R(a) = ea; break;
        case 983: ST(4, RA0(a) + R(b), (uint32_t)fbits(c, d)); break;                                     // stfiwx
        case 1014: ea = (RA0(a) + R(b)) & ~31u; for (int i = 0; i < 32; i += 8) ST(8, ea + i, 0); break;  // dcbz
        case 54: case 86: case 278: case 246: case 598: case 854: break;                                  // cache/barriers
        case 982: { ea = RA0(a) + R(b); uint32_t ra = ea; int wr;                                       // icbi: the physical page goes to the list,
                  c->icbi_lea = (ea & ~0xFFFu) | (c->msr & MSR_DR) >> 3 | 1;                                  // the same EA page again: the JIT skips it;
                  if (c->msr & MSR_DR && pt_xlate(c, ea, 0, !!(c->msr & MSR_PR), &ra, &wr)) { c->icbi_n = 65; break; }   // isync drops the listed pages' code
                  if (c->icbi_n && c->icbi_n <= 64 && !((c->icbi_ea[c->icbi_n - 1] ^ ra) >> 12)) break;              // (no translation: all code)
                  if (c->icbi_n < 64) c->icbi_ea[c->icbi_n] = ra; c->icbi_n++; break; }
        case 19:  R(d) = cr_get(c); break;                                                                // mfcr
        case 144: { int crm = in >> 12 & 0xff; for (int f = 0; f < 8; f++) if (crm & (0x80 >> f)) cr_set_field(c, f, s >> (28 - 4 * f) & 15); break; } // mtcrf
        case 512: cr_set_field(c, d >> 2, xer_get(c) >> 28); c->so = c->ov = c->ca = 0; break;           // mcrxr
        case 339: case 371: { int spr = a | b << 5;                                                       // mfspr/mftb
            if (xo == 339 && spr & 16) PRIV();                                                            // bit 4 of the number: privileged
            if (xo == 371 && spr != 268 && spr != 269) return unimpl(c, in);
            if (!spr_ok(c,spr)) { if (c->msr & MSR_PR) return unimpl(c, in); break; }      // not on this CPU: a no-op in supervisor mode
            switch (spr) { case 1: R(d) = xer_get(c); break; case 8: R(d) = c->lr; break; case 9: R(d) = c->ctr; break;
            case 268: case 269: case SPR_DEC: R(d) = tspr(c, spr, 0, 0); break;
            default: R(d) = c->spr[ppc_has_vec(c) && (spr==935 || (spr>=928 && spr<=930) || (spr>=936 && spr<=942))?spr+16:spr]; } break; }
        case 467: { int spr = a | b << 5;                                                                 // mtspr
            if (spr & 16) PRIV();
            if (!spr_ok(c,spr)) { if (c->msr & MSR_PR) return unimpl(c, in); break; }
            if (spr == 268 || spr == 269 || spr == 287 || (spr >= 936 && spr <= 942) || (ppc_has_vec(c) && (spr==935 || (spr>=928 && spr<=930))) || (ppc_7447a(c) && spr==980)) return unimpl(c, in);
            switch (spr) { case 1: xer_set(c, s); break; case 8: c->lr = s; break; case 9: c->ctr = s; break;
            case SPR_DEC: case 284: case 285: tspr(c, spr, 1, s); break;
            default: if(ppc_7447a(c) && spr==1008) {
                         if((c->spr[spr]^s)&(HID0_HIGH_BAT|HID0_XBSEN|HID0_STEN))c->xgen++;
                         c->xbsen|=s&HID0_XBSEN; /* XBL survives clearing XBSEN after startup. */
                     }
                     if(ppc_7447a(c) && !(spr&1) && ((spr>=528 && spr<=543) || (spr>=560 && spr<=575)) && !c->xbsen)s&=~0x1e000u;
                     if(ppc_7447a(c) && spr>=560 && spr<=575 && c->spr[spr]!=s)c->xgen++;
                     /* The 7447A has no L3, so L3CR reads 0 (XNU init745X reads it right after
                      * L2CR into the same register); L2CR's invalidate (L2I), hardware flush (L2HWF) and L2IP finish at once,
                      * as XNU cacheInit polls them. */
                     if(ppc_7447a(c) && spr==1018)break;
                     if(ppc_7447a(c) && spr==1017)s&=~0x00200801u;
                     if ((spr == SPR_SDR1 || (spr >= 528 && spr <= 535)) && c->spr[spr] != s) c->xgen++;     // SDR1, IBAT: flush everything
                     else if (spr >= 536 && spr <= 543 && c->spr[spr] != s) dbat_inv(c, spr, s);
                     c->spr[spr] = s; } break; }
        default: return unimpl(c, in);
        }
        break;
    }
    case 59: case 63: ppc_fp_settle(c); if (fp_exec(c, in)) return unimpl(c, in); break;   // FPU
    default: return unimpl(c, in);
    }
next:
    c->pc = npc;
    return c->halted;
}

/*
 *             .-"""""""""-.
 *            /             \
 *            |  [o]---[o]  |
 *            |      >      |
 *             \    \_/    /
 *              '-._____.-'
 *           _____|     |_____
 *          /     \\ _ //     \
 *         /  /|     o     |\  \
 *        /  / |     o  [= | \  \
 *       (__(  |     o     |  )__)
 *             |_____|_____|
 *             |     |     |
 *             |_____|_____|
 *               | |   | |
 *               | |   | |
 *              _|x|   |x|_
 *             (___|   |___)
 *
 *     By Apple nerd for Apple nerds
 */
