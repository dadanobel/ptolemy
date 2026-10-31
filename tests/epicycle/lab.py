#!/usr/bin/env python3
"""Counterexample lab: random PowerPC programs, an independent oracle (Unicorn, 750 model), the final state compared
with the engine (interpreter and optimizing JIT), mismatches minimized.

    python3 lab.py ENGINE [N [SEED]]      ENGINE: path to a stand binary (pt or ptv); 200 programs by default
Exit 0: no mismatches, 1: mismatches; an oracle failure raises an exception (it never counts as success).

What the oracle does not check (where Unicorn departs from Book I, the lab works around it instead of masking it):
  * fnmadd/fnmsub: Unicorn rounds the already negated result; with RN=+inf/-inf that is a different bit. Before fnm*: mtfsb0 30 (RN in {0,1}).
  * enabled VE/ZE: Unicorn still writes FRT and FPRF (Book I leaves them unchanged). VE/ZE stay disabled; separate tests cover those engine paths.
  * Unicorn clears FI in fcmp*/mtfs*/mcrfs; the oracle restores FI after them (hook).
  The other oracle corrections are in book1(), each naming the Unicorn deviation from Book I.
Inputs of single operations are rounded with frsp (otherwise Book I leaves the result undefined), so SNaN inputs of single operations are not tested.
Architecturally undefined: the high word of FRT after fctiw*/mffs and FPRF after fctiw* (both hidden from the comparison), the fres/frsqrte result (a hardware estimate).
FR is not tested either (Unicorn has none; the engine runs with PT_FP_FR=0 unless LAB_FR=1)."""
import math, os, random, struct, subprocess, sys, tempfile
from fractions import Fraction
from unicorn import Uc, UC_ARCH_PPC, UC_MODE_PPC32, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
import unicorn.ppc_const as P
import capstone
DIS = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)

BASE, DATA, DUMP = 0x10000000, 0x10020000, 0x10021000
MEM, LOG, LOGP = 0x400, 0x200, 0x3FC       # compared memory DATA..+MEM; journal of fctiw/mffs low words from LOG, its pointer at LOGP
EDGE = [0, 1, 0xffffffff, 0x80000000, 0x7fffffff, 0x80000001, 0xfffffffe, 0x7ffffffe, 0x8000, 0x7fff, 0xffff, 0x10000, 0xffff8000]
D = lambda op, t, a, i: op << 26 | t << 21 | a << 16 | (i & 0xffff)
X = lambda xo, t, a, b, rc=0: 31 << 26 | t << 21 | a << 16 | b << 11 | xo << 1 | rc
XO = lambda xo, t, a, b, oe, rc: 31 << 26 | t << 21 | a << 16 | b << 11 | oe << 10 | xo << 1 | rc
M = lambda op, s, a, sh, mb, me, rc: op << 26 | s << 21 | a << 16 | sh << 11 | mb << 6 | me << 1 | rc
FA = lambda op, t, a, b, c, xo, rc: op << 26 | t << 21 | a << 16 | b << 11 | c << 6 | xo << 1 | rc
FX = lambda t, a, b, xo, rc=0: 63 << 26 | t << 21 | a << 16 | b << 11 | xo << 1 | rc
def UNDEF_HI(ft, fb, tmp):                # FRT's high word is undefined: the low word goes to the journal (stfiwx), then FRT <- fmr ft,fb
    return [D(32, tmp, 31, LOGP), X(983, ft, 31, tmp), D(14, tmp, tmp, 4), D(36, tmp, 31, LOGP), FX(ft, 0, fb if fb != ft else ft ^ 1, 72)]
def li32(r, v): return [D(15, r, 0, v >> 16), D(24, r, r, v)]
def bc(bo, bi, off, lk=0): return 16 << 26 | bo << 21 | bi << 16 | (off & 0xfffc) | lk
ARITH = [266, 40, 104, 235, 75, 11, 10, 8, 138, 136, 202, 200, 234, 232]                     # divw/divwu: separately (item)
LOGIC = [28, 60, 444, 412, 316, 476, 124, 284, 24, 536, 792, 954, 922, 26]
SPECIAL = [0.0, -0.0, 1.0, -1.0, 1.9, -2.5, 0.5, 3.0, 1e308, -1e308, 1e-310, -1e-310, 1.17549435e-38, 1e-40, 3.4e38, 7e38,
           float('inf'), float('-inf'), 2147483647.5, -2147483648.75, 1e10, 0.1, 1/3]
QNAN, SNAN, SNANS = 0x7FF8000000000123, 0x7FF0000000000456, 0x7FF0000020000000   # QNaN, SNaN, SNaN with a payload that survives conversion to single

def dbl_bits(r):
    k = r.random()
    if k < 0.08: return QNAN | (r.getrandbits(1) << 63)
    if k < 0.16: return r.choice([SNAN, SNANS]) | (r.getrandbits(1) << 63)
    if k < 0.6: return struct.unpack('>Q', struct.pack('>d', r.choice(SPECIAL)))[0]
    if k < 0.8: return struct.unpack('>Q', struct.pack('>d', struct.unpack('>f', struct.pack('>I', r.getrandbits(32)))[0]))[0]
    return r.getrandbits(64)

def arith(r, t, a, b, oe):                # reserved fields = 0, otherwise an "invalid form" (Unicorn raises a program exception)
    xo = r.choice(ARITH)
    return XO(xo, t, a, 0 if xo in (104, 202, 234, 200, 232) else b, 0 if xo in (75, 11) else int(r.random() < oe), r.randrange(2))

def simple_op(r):                         # one safe instruction without branches or memory access
    t, a, b = r.randrange(29), r.randrange(32), r.randrange(32)
    k = r.randrange(5)
    if k == 0: return arith(r, t, a, b, .2)
    if k == 1: xo = r.choice(LOGIC); return X(xo, a, t, 0 if xo in (954, 922, 26) else b, r.randrange(2))
    if k == 2: return M(r.choice([21, 23]), a, t, r.randrange(32), r.randrange(32), r.randrange(32), r.randrange(2))
    if k == 3: return D(r.choice([7, 8, 12, 13, 14, 15]), t, a, r.getrandbits(16))
    return D(r.choice([24, 25, 26, 27, 28, 29]), a, t, r.getrandbits(16))

def item(r):                              # a position-independent fragment (list of words)
    k = r.randrange(100); t = r.randrange(29)
    if k < 25: return [simple_op(r)]
    if k < 28:                                                                                                         # edge values: 0, +-1, 0x80000000...
        a, b = r.randrange(29), r.randrange(29)
        return li32(a, r.choice(EDGE)) + li32(b, r.choice(EDGE)) + [arith(r, t, a, b, .5)]
    if k < 30:                                                                                                         # divw/divwu: odd dividend and divisor,
        a, b = r.randrange(29), r.randrange(29)                                                                       # so no /0 and no 0x80000000/-1 (rD undefined)
        return [D(24, a, a, 1), D(24, b, b, 1), XO(r.choice([491, 459]), t, a, b, int(r.random() < .2), r.randrange(2))]
    if k < 34: return [M(20, r.randrange(32), t, r.randrange(32), r.randrange(32), r.randrange(32), r.randrange(2))]     # rlwimi
    if k < 38: return [X(824, r.randrange(32), t, r.randrange(32), r.randrange(2))]                                    # srawi
    if k < 44:                                                                                                         # compare
        f = r.randrange(8)
        return [r.choice([X(0, f << 2, r.randrange(32), r.randrange(32)), X(32, f << 2, r.randrange(32), r.randrange(32)),
                          D(11, f << 2, r.randrange(32), r.getrandbits(16)), D(10, f << 2, r.randrange(32), r.getrandbits(16))])]
    if k < 54:                                                                                                         # forward branch
        n = r.randrange(1, 4); f = r.randrange(8)
        bo = r.choice([4, 5, 12, 13, 16, 18, 20, 0, 2, 8, 10]); bi = r.randrange(32)
        pre = [r.choice([X(0, f << 2, r.randrange(32), r.randrange(32)), D(11, f << 2, r.randrange(32), r.getrandbits(16))])]
        return pre + [bc(bo, bi, 4 * (n + 1))] + [simple_op(r) for _ in range(n)]
    if k < 57: return [18 << 26 | 9, 18 << 26 | 12, simple_op(r), 19 << 26 | 20 << 21 | 16 << 1]                    # bl +8; b +12; op; blr
    if k < 59: return [18 << 26 | 5, X(339, 28, 8, 0), D(14, 28, 28, 16), X(467, 28, 9, 0), 19 << 26 | 20 << 21 | 528 << 1]  # bl+4; mflr; addi; mtctr; bctr
    if k < 64:                                                                                                         # CR logic
        c = r.randrange(4)
        if c == 0: return [19 << 26 | r.randrange(32) << 21 | r.randrange(32) << 16 | r.randrange(32) << 11 | r.choice([257, 449, 193, 225, 33, 289, 129, 417]) << 1]
        if c == 1: return [19 << 26 | r.randrange(8) << 23 | r.randrange(8) << 18]                                    # mcrf
        if c == 2: return [X(19, t, 0, 0)]                                                                            # mfcr
        return [31 << 26 | r.randrange(32) << 21 | r.randrange(256) << 12 | 144 << 1]                                # mtcrf
    if k < 68:                                                                                                         # SPR
        c = r.randrange(5)
        if c == 0: return [X(339, t, r.choice([8, 9, 1]), 0)]
        if c == 1: return [X(467, r.randrange(32), r.choice([8, 9]), 0)]
        if c == 2: return li32(t, r.getrandbits(3) << 29 | r.randrange(128)) + [X(467, t, 1, 0)]                    # mtxer
        if c == 3: return [31 << 26 | r.randrange(8) << 23 | 512 << 1]                                               # mcrxr
        return [X(339, t, 1, 0)]
    if k < 80:                                                                                                         # memory
        c = r.randrange(9); dd = r.randrange(0, 240)
        if c == 0: return [D(r.choice([32, 34, 40, 42]), t, 31, dd)]
        if c == 1: return [D(r.choice([36, 38, 44]), r.randrange(32), 31, dd)]
        if c == 2: return [X(r.choice([23, 87, 279, 343, 534, 790]), t, 31, 30)]
        if c == 3: return [X(r.choice([151, 215, 407, 662, 918]), r.randrange(32), 31, 30)]
        if c == 4:                                                                                                     # update forms through r29
            return [D(r.choice([33, 35, 41, 43, 37, 39, 45]), t, 29, r.randrange(-64, 64)), X(444, 29, t if t != 29 else 0, 29), D(14, 29, 31, 0x80)]
        if c == 5: return [D(47, r.randrange(24, 32), 31, r.randrange(0, 200) & ~3)]                                # stmw
        if c == 6: return li32(24, DATA) + [D(46, r.randrange(25, 29), 24, r.randrange(0, 200) & ~3)] + li32(31, DATA) + [D(14, 30, 0, 0x80), D(14, 29, 31, 0x80)]  # lmw (base outside the range) + restore
        if c == 7: return [X(1014, 0, 31, 30)]                                                                         # dcbz
        return [X(r.choice([151, 183]), r.randrange(32), 29, 30), D(14, 29, 31, 0x80)]                                # stwx/stwux
    if k < 96:                                                                                                         # FPU
        ft, fa, fb, fc = (r.randrange(32) for _ in range(4)); rc = int(r.random() < .3)
        c = r.randrange(9)
        if c < 4:                                                                                                      # the 750 has no fsqrt or fre
            x = r.choice([18, 20, 21, 25, 28, 29, 30, 31, 24, 26]); op = 59 if x == 24 else 63 if x == 26 else r.choice([59, 63])
            if x in (18, 20, 21, 24, 26): fc = 0                                                                      # reserved fields
            if x in (24, 26): fa = 0
            if x == 25: fb = 0
            fix = [FX(30, 0, 0, 70)] if x in (30, 31) else []                                                       # mtfsb0 30: RN in {0,1}; Unicorn rounds fnm* after negating
            if op == 59 and x != 24:                                                                                   # inputs of single operations are singles (otherwise undefined)
                fix += [FX(n, 0, n, 12) for n in sorted({fa, fb if x != 25 else fa, fc if x in (25, 28, 29, 30, 31) else fa})]
            return fix + [FA(op, ft, fa, fb, fc, x, rc)]
        if c == 4:
            x = r.choice([12, 14, 15, 40, 72, 136, 264])
            return [FX(ft, 0, fb, x, rc)] + (UNDEF_HI(ft, fb, t) + [63 << 26 | 4 << 23 | 134 << 1, FX(15, 0, 0, 70)] if x in (14, 15) else [])  # fctiw*: FPRF undefined
        if c == 5: return [FX(r.randrange(8) << 2, fa, fb, r.choice([0, 32]))]                                       # fcmpu/fcmpo
        if c == 6: return [FA(63, ft, fa, fb, fc, 23, rc)]                                                             # fsel
        if c == 7:                                                                                                     # FPSCR
            s = r.choice([0, 2, 3, 4])                                                                                # VE/ZE stay disabled: see the docstring
            if s == 0: return [63 << 26 | 7 << 23 | (r.randrange(4) | r.choice([0, 8]) ) << 12 | 134 << 1]           # mtfsfi 7: RN, XE (NI=0)
            if s == 2: return [63 << 26 | r.choice(range(3, 13)) << 21 | r.choice([38, 70]) << 1]                    # mtfsb1/mtfsb0
            if s == 3: return [63 << 26 | r.randrange(8) << 23 | r.randrange(8) << 18 | 64 << 1]                      # mcrfs
            return [FX(ft, 0, 0, 583)] + UNDEF_HI(ft, fb, t)                                                              # mffs
        return [D(r.choice([48, 50, 52, 54]), ft, 31, r.randrange(0, 240) & ~7), X(983, ft, 31, 30)][r.randrange(2):][:1]
    return [simple_op(r) for _ in range(2)]

def build(items, r):
    code = li32(31, DATA) + [D(14, 30, 0, 0x80), D(14, 29, 31, 0x80)]
    code += [D(50, i, 31, 8 * i) for i in range(32)]                                                                    # f0..f31
    code += [D(50, 0, 31, 0x1F8), 63 << 26 | 0xFF << 17 | 0 << 11 | 711 << 1, D(50, 0, 31, 0)]                    # FPSCR
    for g in range(29): code += li32(g, r.choice(EDGE) if r.random() < .25 else r.getrandbits(32))
    code += li32(3, r.getrandbits(32)) + [31 << 26 | 3 << 21 | 0xFF << 12 | 144 << 1]                              # CR
    code += li32(3, (r.random() < .1) << 31 | r.getrandbits(2) << 29 | r.randrange(128)) + [X(467, 3, 1, 0)]        # XER
    code += li32(3, r.randrange(1, 30)) + [X(467, 3, 9, 0)] + li32(3, r.getrandbits(32) & ~3) + [X(467, 3, 8, 0)]    # CTR, LR
    code += li32(3, r.getrandbits(32))
    lo = len(code)
    for it in items: code += it
    hi = len(code)
    code += li32(31, DUMP) + [D(47, 0, 31, 0)]                                                                          # stmw r0..r31
    code += [X(19, 3, 0, 0), D(36, 3, 31, 128), X(339, 3, 1, 0), D(36, 3, 31, 132), X(339, 3, 8, 0), D(36, 3, 31, 136), X(339, 3, 9, 0), D(36, 3, 31, 140)]
    code += [D(54, i, 31, 144 + 8 * i) for i in range(32)] + [FX(0, 0, 0, 583), D(54, 0, 31, 400)]
    code += [D(14, 0, 0, 4), D(14, 3, 0, 1)] + li32(4, DUMP) + [D(14, 5, 0, 408), 17 << 26 | 2]
    code += [D(14, 0, 0, 4), D(14, 3, 0, 1)] + li32(4, DATA) + [D(14, 5, 0, MEM), 17 << 26 | 2]
    code += [D(14, 0, 0, 1), D(14, 3, 0, 0), 17 << 26 | 2]
    first_sc = BASE + 4 * (len(code) - 3 - 8)                                                                           # the first sc
    fpscr0 = r.randrange(4) | (r.random() < .1) << 3                                                                   # RN, XE
    data = (b''.join(struct.pack('>Q', dbl_bits(r)) for _ in range(32)) + bytes(r.getrandbits(8) for _ in range(0x1F8 - 256)) + struct.pack('>Q', fpscr0))
    data += bytes(LOGP - len(data)) + struct.pack('>I', LOG)
    return code, data, first_sc, (lo, hi)

def elf(code, data):
    img = bytearray(0x22000)
    img[0:4 * len(code)] = b''.join(struct.pack('>I', w) for w in code)
    img[0x20000:0x20000 + len(data)] = data
    hdr = b'\x7fELF\x01\x02\x01' + bytes(9) + struct.pack('>HHIIIIIHHHHHH', 2, 20, 1, BASE, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    ph = struct.pack('>IIIIIIII', 1, 0x1000, BASE, BASE, len(img), 0x30000, 7, 0x1000)
    return (hdr + ph).ljust(0x1000, b'\0') + bytes(img)

FI, FXS, FEX, VX, ZX, CBIT = 1 << 17, 1 << 31, 1 << 30, 1 << 29, 1 << 26, 1 << 16
EXC, VXALL = 0x1FF80700, 0x01F80700                                                     # OX..VXVC, VXSOFT, VXSQRT, VXCVI; VX*
VXISI, VXIMZ, FL, FG, UX = 1 << 23, 1 << 20, 1 << 15, 1 << 14, 1 << 27
b2d = lambda v: struct.unpack('>d', struct.pack('>Q', v))[0]
d2b = lambda x: struct.unpack('>Q', struct.pack('>d', x))[0]
fpr = lambda uc, n: uc.reg_read(getattr(P, 'UC_PPC_REG_FPR%d' % n))
isnan = lambda v: v >> 52 & 0x7ff == 0x7ff and v & (1 << 52) - 1
iszero = lambda v: not v & ~(1 << 63)
isinf = lambda v: v & ~(1 << 63) == 0x7ff << 52
def book1(uc, w, f0, fa, fb0, fc):         # Unicorn -> Book I corrections for the just executed w (FPSCR, FRA/FRB/FRC: the values before it)
    f = uc.reg_read(P.UC_PPC_REG_FPSCR); xo = w >> 1 & 0x3ff
    if xo & 31 >= 28:                                                                                            # f(n)madd/f(n)msub
        nan = isnan(fa) or isnan(fb0) or isnan(fc)
        if nan or (iszero(fa) and isinf(fc)) or (isinf(fa) and iszero(fc)): f = f & ~VXISI | f0 & VXISI           # Unicorn: 0*inf +- inf also sets VXISI
        if nan: f = f & ~VXIMZ | f0 & VXIMZ                                                                      # Unicorn: 0*inf + QNaN sets VXIMZ (Unverified: the 750/G4 does not)
    if w >> 26 == 63 and xo in (14, 15) and not isnan(fb0) and not fb0 >> 63:                                    # fctiw*(>2^31-1): unicorn 0x80000000, Book I 0x7FFFFFFF
        t = fpr(uc, w >> 21 & 31)
        if t & 0xffffffff == 0x80000000: uc.reg_write(getattr(P, 'UC_PPC_REG_FPR%d' % (w >> 21 & 31)), t & ~0xffffffff | 0x7fffffff)
    if w >> 26 == 63 and xo in (14, 15) and not isnan(fb0):                                                      # fctiw*: FI = inexactness of this operation
        v = struct.unpack('>d', struct.pack('>Q', fb0))[0]                                                       # (Unicorn keeps the old FI when exact)
        if math.isinf(v): ri = v
        else: ri = math.trunc(v) if xo == 15 else [round, math.trunc, math.ceil, math.floor][f0 & 3](v)
        f = f & ~FI | (FI if -2 ** 31 <= ri < 2 ** 31 and ri != v else 0)
    if w >> 26 == 59 and xo & 31 >= 28 and not f0 & 3 and all(math.isfinite(b2d(v)) for v in (fa, fb0, fc)):    # single FMA, RN=0: Unicorn rounds
        p, q = Fraction(b2d(fa)) * Fraction(b2d(fc)), Fraction(b2d(fb0))                                         # twice (double, then single)
        ex = p + q if xo & 1 else p - q
        if ex:
            d = float(ex)
            if abs(Fraction(d)) > abs(ex): d = math.nextafter(d, 0)
            if Fraction(d) != ex: d = b2d(d2b(d) | 1)                                                            # round to odd, then to single
            try: s = struct.unpack('>f', struct.pack('>f', d))[0]
            except OverflowError: s = None
            if s is not None: uc.reg_write(getattr(P, 'UC_PPC_REG_FPR%d' % (w >> 21 & 31)), d2b(-s if xo & 31 >= 30 else s))
            if s is not None and abs(ex) < Fraction(2) ** -126 and Fraction(s) != ex: f |= UX                     # tiny and inexact in single -> UX
    if (w >> 26 == 59 and xo & 31 >= 18 or w >> 26 == 63 and xo == 12) and f & 0x1F000 in (FL, FG):           # single result: classified as a single
        x = abs(struct.unpack('>d', struct.pack('>Q', fpr(uc, w >> 21 & 31)))[0])
        if 0 < x < 2 ** -126: f |= CBIT                                                                          # Unicorn classifies it as a normal double
    keep = FI | CBIT if w >> 26 == 63 and xo in (0, 32) else FI if keeps_fi(w) else 0                         # fcmp*: Unicorn sets FI <- 0, and C <- 1 with a NaN
    f = f & ~keep | f0 & keep
    if xo & 31 in (24, 26) and not fb0 & ~(1 << 63): f |= ZX                                                     # fres/frsqrte(±0): ZX
    if not (w >> 26 == 63 and xo in (38, 70, 64, 134, 711)):                                                     # not mtfs*/mcrfs: FX only on a 0 -> 1 transition
        f = f & ~FXS | f0 & FXS | (FXS if f & ~f0 & EXC else 0)
    f = f & ~(VX | FEX) | (VX if f & VXALL else 0)
    f |= FEX if f >> 22 & f & 0xF8 else 0
    uc.reg_write(P.UC_PPC_REG_FPSCR, f)
    if w & 1 and not (w >> 26 == 63 and xo in (0, 32, 64)):                                                      # Rc=1: CR1 ← FPSCR[0:3]
        uc.reg_write(P.UC_PPC_REG_CR, uc.reg_read(P.UC_PPC_REG_CR) & ~0x0F000000 | f >> 28 << 24)

def keeps_fi(w):
    if w >> 26 != 63: return False
    xo = w >> 1 & 0x3ff
    if xo in (0, 32, 64): return True                                                                           # fcmpu fcmpo mcrfs
    if xo in (38, 70): return (w >> 21 & 31) != 14                                                              # mtfsb1/0, except of FI itself
    if xo == 134: return (w >> 23) != (63 << 3 | 3)                                                             # mtfsfi, except field 3
    return xo == 711 and not (w >> 17 & 0xff) & 0x10                                                            # mtfsf, except field 3

def oracle(code, data, first_sc, body=(0, 0), show=False):
    lo, hi = BASE + 4 * body[0], BASE + 4 * body[1]
    u = Uc(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(P.UC_CPU_PPC32_750_V3_1)
    u.mem_map(BASE, 0x30000)
    u.mem_write(BASE, b''.join(struct.pack('>I', w) for w in code)); u.mem_write(DATA, data)
    u.reg_write(P.UC_PPC_REG_MSR, 0x2000)                                                                                # MSR.FP
    last, pend = [BASE], [None]
    def hook(uc, a, s, d):
        last[0] = a
        if pend[0]: book1(uc, *pend[0]); pend[0] = None
        w = code[(a - BASE) >> 2] if BASE <= a < BASE + 4 * len(code) else 0
        if w >> 26 in (59, 63): pend[0] = (w, uc.reg_read(P.UC_PPC_REG_FPSCR), fpr(uc, w >> 16 & 31), fpr(uc, w >> 11 & 31), fpr(uc, w >> 6 & 31))
        if show and lo <= a < hi:
            ins = list(DIS.disasm(struct.pack('>I', w), a)); f = w >> 26 in (59, 63)
            regs = ' '.join(f'{"f" if f else "r"}{n}={uc.reg_read(getattr(P, ("UC_PPC_REG_FPR%d" if f else "UC_PPC_REG_%d") % n)):#x}' for n in sorted({w >> 21 & 31, w >> 16 & 31, w >> 11 & 31} | ({w >> 6 & 31} if f and w >> 1 & 31 >= 23 else set())))
            print(f'    {a:#x}: {ins[0].mnemonic + " " + ins[0].op_str if ins else "%08x" % w:32} fpscr={uc.reg_read(P.UC_PPC_REG_FPSCR):08x} cr={uc.reg_read(P.UC_PPC_REG_CR):08x} {regs}')
    u.hook_add(UC_HOOK_CODE, hook)
    try: u.emu_start(BASE, first_sc, count=2_000_000)
    except Exception as e: raise RuntimeError(f'reference: {e} near {last[0]:#x}')
    return bytes(u.mem_read(DUMP, 408)) + bytes(u.mem_read(DATA, MEM))

FIELDS = [('r%d' % i, 4 * i, 4) for i in range(32)] + [('cr', 128, 4), ('xer', 132, 4), ('lr', 136, 4), ('ctr', 140, 4)] + \
         [('f%d' % i, 144 + 8 * i, 8) for i in range(32)] + [('fpscr', 404, 4)] + [('mem+%#x' % (4 * i), 408 + 4 * i, 4) for i in range(MEM // 4)]
def diffs(a, b):
    out = []
    for name, off, n in FIELDS:
        x, y = int.from_bytes(a[off:off + n], 'big'), int.from_bytes(b[off:off + n], 'big')
        if x != y: out.append((name, x, y))
    return out

def run_engine(eng, path, mode):
    args = [eng] + (['-i'] if mode == 'interp' else []) + [path]
    env = dict(os.environ); env.pop('PT_CHECK', None)
    if os.environ.get('LAB_FR') != '1': env['PT_FP_FR'] = '0'   # Unicorn has no FR (mffs shows it too); tests/fp-oracle.py checks FR
    if mode == 'check': env['PT_CHECK'] = '1'
    p = subprocess.run(args, capture_output=True, timeout=60, env=env)
    return p.stdout, p.returncode, p.stderr.decode(errors='replace')

def mismatch(eng, items, r_state, mode, tmp, show=False):
    r = random.Random(); r.setstate(r_state)
    code, data, sc, body = build(items, r)
    want = oracle(code, data, sc, body, show)                          # an oracle failure is a run error, not "no mismatches"
    with open(tmp, 'wb') as f: f.write(elf(code, data))
    out, rc, err = run_engine(eng, tmp, mode)
    if rc != 0 or len(out) != 408 + MEM: return [('exit', rc, err[:200])]
    return diffs(out, want) or None

def minimize(eng, items, st, mode, tmp):                  # ddmin over fragments
    n = 2
    while len(items) >= 2:
        chunk = max(1, len(items) // n); done = False
        for i in range(0, len(items), chunk):
            cand = items[:i] + items[i + chunk:]
            if cand and mismatch(eng, cand, st, mode, tmp):
                items, n, done = cand, max(n - 1, 2), True; break
        if not done:
            if chunk == 1: break
            n = min(len(items), 2 * n)
    return items

def main():
    eng = os.path.abspath(sys.argv[1]); N = int(sys.argv[2]) if len(sys.argv) > 2 else 200; seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    td = tempfile.TemporaryDirectory(); tmp = os.path.join(td.name, 'lab.ppc'); bad = 0
    for k in range(N):
        g = random.Random(seed * 1000003 + k)
        items = [item(g) for _ in range(g.randrange(20, 200))]
        st = g.getstate()
        for mode in ('interp', 'jit'):
            d = mismatch(eng, items, st, mode, tmp)
            if not d: continue
            bad += 1
            small = minimize(eng, items, st, mode, tmp)
            print(f'#{k} seed={seed} mode={mode}: mismatch, {len(small)} fragments after minimizing:')
            for name, x, y in (mismatch(eng, small, st, mode, tmp, show=True) or [])[:8]: print(f'    {name}: engine {x:#x}  reference {y:#x}')
            break
    print(f'programs: {N}, with mismatches: {bad}')
    return 1 if bad else 0

if __name__ == '__main__': sys.exit(main())
