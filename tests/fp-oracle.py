#!/usr/bin/env python3
"""Independent FPSCR/result oracle for PPC scalar FP (Book I semantics, exact rational arithmetic; no
host FPU, no code from interp.c/jit.c). Writes tests/fp-fr-vectors.h for tests/fpscr-fr.c.

Each vector: instruction word, FPSCR in, FRA/FRB/FRC bits, expected FRT bits, expected FPSCR, origin.
Origins: 'isa' = Book I rule; 'ptolemy' = a case Book I leaves undefined, fixed by Ptolemy's convention:
  - FR after a disabled overflow and after fres/frsqrte: 0;
  - single-precision ops with inputs not representable in single: the exact result rounded once to single;
  - fctiw* FRT[0:31] = 0xFFF80000, FPRF unchanged.
Covered: OE=1/UE=1 exponent adjustment for single precision and frsp. Not covered:
OE=1/UE=1 for double precision, FE0/FE1 program interrupts, NI=1, tininess exactly at 2^Emin with UE=0.
Usage: tests/fp-oracle.py > tests/fp-fr-vectors.h"""
from fractions import Fraction as Q
import random, struct, sys

D, S = (53, -1022, 1023), (24, -126, 127)
FX, FEX, VX, OX, UX, ZX, XX = 1 << 31, 1 << 30, 1 << 29, 1 << 28, 1 << 27, 1 << 26, 1 << 25
VXSNAN, VXISI, VXIDI, VXZDZ, VXIMZ, VXVC = 1 << 24, 1 << 23, 1 << 22, 1 << 21, 1 << 20, 1 << 19
FR, FI, FPRF = 1 << 18, 1 << 17, 0x1F << 12
VXSOFT, VXSQRT, VXCVI, VE, OE, UE, ZE, XE = 1 << 10, 1 << 9, 1 << 8, 1 << 7, 1 << 6, 1 << 5, 1 << 4, 1 << 3
VXALL = VXSNAN | VXISI | VXIDI | VXZDZ | VXIMZ | VXVC | VXSOFT | VXSQRT | VXCVI
EXC = OX | UX | ZX | XX | VXALL
INF = 'inf'

def bits(x):  # double bits of a float
    return struct.unpack('>Q', struct.pack('>d', x))[0]

def decode(b):  # -> (kind, sign, value Fraction)
    s, e, f = b >> 63, b >> 52 & 0x7ff, b & ((1 << 52) - 1)
    if e == 0x7ff: return ('nan' if f else 'inf', s, None)
    v = Q(f, 1 << 52) * Q(2) ** -1022 if e == 0 else (1 + Q(f, 1 << 52)) * Q(2) ** (e - 1023)
    return ('zero' if v == 0 else 'num', s, -v if s else v)

def encode_double(v):  # exact Fraction representable as double (possibly denormal) -> bits
    if v == 0: raise ValueError
    s = 1 if v < 0 else 0; m = abs(v); e = m.numerator.bit_length() - m.denominator.bit_length()
    if Q(2) ** e > m: e -= 1
    if e < -1022:
        f = m / Q(2) ** -1074; assert f.denominator == 1; return s << 63 | f.numerator
    f = (m / Q(2) ** e - 1) * (1 << 52); assert f.denominator == 1
    return s << 63 | (e + 1023) << 52 | f.numerator

class Boundary(Exception): pass

def is_single(b):
    kind, _, v = decode(b)
    if kind != 'num': return True
    try: return Q(struct.unpack('>f', struct.pack('>f', float(v)))[0]) == v
    except OverflowError: return False

def ilog2(m):
    e = m.numerator.bit_length() - m.denominator.bit_length()
    return e - 1 if Q(2) ** e > m else e

def rnd(v, fmt, rn):
    """Round a nonzero exact v. Returns (bits or ('inf', sign), inexact, fr, ox, ux)."""
    p, emin, emax = fmt; neg = v < 0; m = abs(v); e = ilog2(m)
    tiny = e < emin
    q = Q(2) ** ((emin if tiny else e) - p + 1)
    n = m / q; lo = n.numerator // n.denominator; frac = n - lo
    if frac == 0: up = 0
    elif rn == 0: up = 1 if frac > Q(1, 2) or (frac == Q(1, 2) and lo & 1) else 0
    elif rn == 1: up = 0
    elif rn == 2: up = 0 if neg else 1
    else: up = 1 if neg else 0
    r = (lo + up) * q; inexact = frac != 0
    if tiny and inexact and r == Q(2) ** emin: raise Boundary   # tininess before vs after rounding: not covered
    if r >= Q(2) ** (emax + 1):            # overflow (disabled): the rounding mode picks Inf or the largest number
        big = (2 - Q(2) ** (1 - p)) * Q(2) ** emax
        to_inf = rn == 0 or (rn == 2 and not neg) or (rn == 3 and neg)
        return ((INF, neg) if to_inf else encode_double(-big if neg else big)), 1, 0, 1, 0
    if r == 0: return ('zero', neg), inexact, 0, 0, tiny and inexact
    return encode_double(-r if neg else r), inexact, up, 0, tiny and inexact

def rnd_s(v, fmt, fpscr):
    """rnd() plus Book I's enabled exceptions for single precision: UE=1 and tiny before rounding, or OE=1 and an
    overflow: the exponent adjusted by +192 / -192 and rounded; UX / OX set. Returns rnd's tuple and 'adjusted'."""
    rn = fpscr & 3
    if fmt == S and fpscr & UE and v != 0 and abs(v) < Q(2) ** -126:
        r, inexact, up, _, _ = rnd(v * Q(2) ** 192, S, rn); return r, inexact, up, 0, 1, True
    r, inexact, up, ox, ux = rnd(v, fmt, rn)
    if fmt == S and ox and fpscr & OE:
        r2, inexact2, up2, ox2, _ = rnd(v * Q(2) ** -192, S, rn)
        if not ox2: return r2, inexact2, up2, 1, 0, True   # still over (a huge frsp input): undefined, as disabled
    return r, inexact, up, ox, ux, False

def fprf(b, fmt):
    s, e, f = b >> 63, b >> 52 & 0x7ff, b & ((1 << 52) - 1)
    if e == 0x7ff: return 0x11 if f else (0x09 if s else 0x05)
    if e == 0 and f == 0: return 0x12 if s else 0x02
    small = (e == 0) if fmt == D else (e < 1023 - 126)
    return (0x18 if s else 0x14) if small else (0x08 if s else 0x04)

def raise_(f, b):
    if b & ~f & EXC: f |= FX
    return f | b

def summ(f):
    f &= ~(VX | FEX)
    if f & VXALL: f |= VX
    if (f & VX and f & VE) or (f & OX and f & OE) or (f & UX and f & UE) or (f & ZX and f & ZE) or (f & XX and f & XE): f |= FEX
    return f

QNAN = 0x7FF8000000000000
def is_nan(b): return (b >> 52 & 0x7ff) == 0x7ff and b & ((1 << 52) - 1)
def is_snan(b): return is_nan(b) and not b & (1 << 51)

def arith(xo, sgl, fpscr, a, b, c):
    """A-form. Returns (frt or None for 'unchanged', fpscr, origin)."""
    fmt = S if sgl else D; rn = fpscr & 3; origin = 'isa'
    use = {21: 'ab', 20: 'ab', 25: 'ac', 18: 'ab', 28: 'acb', 29: 'acb', 30: 'acb', 31: 'acb'}[xo]
    ops = {'a': a, 'b': b, 'c': c}
    if sgl:
        if not all(is_single(ops[k]) for k in use): origin = 'ptolemy'
    f = fpscr & ~(FR | FI)
    nans = [ops[k] for k in 'abc' if k in use and is_nan(ops[k])]
    if nans:
        if any(is_snan(ops[k]) for k in use): f = raise_(f, VXSNAN)
        f = summ(f)
        if any(is_snan(ops[k]) for k in use) and f & VE: return None, f, origin
        r = nans[0] | (1 << 51)
        if sgl: r &= ~0x1FFFFFFF
        return r, summ((f & ~FPRF) | fprf(r, fmt) << 12), origin
    A, B, C = (decode(x) for x in (a, b, c))
    def inf(x): return x[0] == 'inf'
    def zero(x): return x[0] == 'zero'
    inv = 0
    if xo == 21 and inf(A) and inf(B) and A[1] != B[1]: inv = VXISI
    if xo == 20 and inf(A) and inf(B) and A[1] == B[1]: inv = VXISI
    if xo == 25 and ((inf(A) and zero(C)) or (zero(A) and inf(C))): inv = VXIMZ
    if xo == 18 and inf(A) and inf(B): inv = VXIDI
    if xo == 18 and zero(A) and zero(B): inv = VXZDZ
    if xo >= 28:
        bs = B[1] ^ (0 if xo & 1 else 1)
        if (inf(A) and zero(C)) or (zero(A) and inf(C)): inv = VXIMZ
        elif (inf(A) or inf(C)) and inf(B) and (A[1] ^ C[1]) != bs: inv = VXISI
    if inv:
        f = summ(raise_(f, inv))
        if f & VE: return None, f, origin
        return QNAN, summ((f & ~FPRF) | 0x11 << 12), origin
    neg = xo >= 30
    def finish(r, f):
        if neg: r ^= 1 << 63
        return r, summ((f & ~FPRF) | fprf(r, fmt) << 12), origin
    def infbits(s): return (s << 63) | 0x7FF0000000000000
    # Exact special results (Inf operands, zero divide): no rounding, FR = FI = 0.
    if xo in (20, 21):
        bs = B[1] ^ (1 if xo == 20 else 0)
        if inf(A): return finish(infbits(A[1]), f)
        if inf(B): return finish(infbits(bs), f)
        x, y = A[2] if not zero(A) else Q(0), (B[2] if not zero(B) else Q(0)) * (-1 if xo == 20 else 1)
        if x + y == 0:
            zs = (A[1] & bs) if zero(A) and zero(B) else 0
            if zero(A) and zero(B) and A[1] != bs: zs = 1 if rn == 3 else 0
            if not (zero(A) and zero(B)): zs = 1 if rn == 3 else 0
            return finish(zs << 63, f)
        exact = x + y
    elif xo == 25:
        s = A[1] ^ C[1]
        if inf(A) or inf(C): return finish(infbits(s), f)
        if zero(A) or zero(C): return finish(s << 63, f)
        exact = A[2] * C[2]
    elif xo == 18:
        s = A[1] ^ B[1]
        if inf(A): return finish(infbits(s), f)
        if inf(B): return finish(s << 63, f)
        if zero(B):
            f = summ(raise_(f, ZX))
            if f & ZE: return None, f, origin
            return finish(infbits(s), f)
        if zero(A): return finish(s << 63, f)
        exact = A[2] / B[2]
    else:
        bs = B[1] ^ (0 if xo & 1 else 1); ps = A[1] ^ C[1]
        if inf(A) or inf(C): return finish(infbits(ps), f)
        if inf(B): return finish(infbits(bs), f)
        prod = Q(0) if zero(A) or zero(C) else A[2] * C[2]
        add = Q(0) if zero(B) else (B[2] if xo & 1 else -B[2])
        exact = prod + add
        if exact == 0:
            if prod == 0 and add == 0: zs = (ps & bs) if ps == bs else (1 if rn == 3 else 0)
            else: zs = 1 if rn == 3 else 0
            return finish(zs << 63, f)
    r, inexact, up, ox, ux, adj = rnd_s(exact, fmt, f)
    if ox and not adj: origin = 'ptolemy' if origin == 'isa' else origin   # FR after a disabled overflow: undefined, 0
    if r == ('zero', 0) or r == ('zero', 1): r = r[1] << 63
    elif isinstance(r, tuple): r = infbits(1 if r[1] else 0)
    b_ = (OX if ox else 0) | (UX if ux else 0) | (XX if inexact else 0)
    f = raise_(f, b_)
    if inexact: f |= FI
    if up and (adj or not ox): f |= FR
    return finish(r, f)

def frsp(fpscr, b):
    f = fpscr & ~(FR | FI); rn = fpscr & 3
    if is_nan(b):
        if is_snan(b):
            f = summ(raise_(f, VXSNAN))
            if f & VE: return None, f, 'isa'
        r = (b | 1 << 51) & ~0x1FFFFFFF
        return r, summ((f & ~FPRF) | 0x11 << 12), 'isa'
    kind, s, v = decode(b)
    if kind != 'num': return b, summ((f & ~FPRF) | fprf(b, S) << 12), 'isa'
    r, inexact, up, ox, ux, adj = rnd_s(v, S, f)
    if isinstance(r, tuple): r = (s << 63) if r[0] == 'zero' else ((1 if r[1] else 0) << 63 | 0x7FF0000000000000)
    f = raise_(f, (OX if ox else 0) | (UX if ux else 0) | (XX if inexact else 0))
    if inexact: f |= FI
    if up and (adj or not ox): f |= FR
    return r, summ((f & ~FPRF) | fprf(r, S) << 12), 'ptolemy' if ox and not adj else 'isa'

def fctiw(fpscr, b, z):
    f = fpscr & ~(FR | FI); rn = 1 if z else fpscr & 3; hi = 0xFFF8000000000000
    if is_nan(b):
        f = summ(raise_(f, VXCVI | (VXSNAN if is_snan(b) else 0)))
        return (None if f & VE else hi | 0x80000000), f, 'ptolemy'
    kind, s, v = decode(b)
    if kind == 'inf':
        f = summ(raise_(f, VXCVI)); return (None if f & VE else hi | (0x80000000 if s else 0x7FFFFFFF)), f, 'ptolemy'
    if kind == 'zero': return hi, summ(f), 'ptolemy'
    n = v.numerator // v.denominator if v > 0 else -((-v).numerator // (-v).denominator)   # toward zero
    frac = abs(v - n); up = 0
    if frac:
        if rn == 0: up = 1 if frac > Q(1, 2) or (frac == Q(1, 2) and n & 1) else 0
        elif rn == 2: up = 0 if v < 0 else 1
        elif rn == 3: up = 1 if v < 0 else 0
    r = n + (up if v > 0 else -up)
    if r > 2 ** 31 - 1 or r < -2 ** 31:
        f = summ(raise_(f, VXCVI)); return (None if f & VE else hi | (0x7FFFFFFF if r > 0 else 0x80000000)), f, 'ptolemy'
    if frac: f = raise_(f, XX) | FI | (FR if up else 0)
    return hi | (r & 0xFFFFFFFF), summ(f), 'ptolemy'

# ---------------------------------------------------------------- inputs
rng = random.Random(20261025)
def dbl(x): return bits(x)
def rsingle(lo=-20, hi=20):
    m = rng.getrandbits(23) | 1 << 23
    return bits(rng.choice((1, -1)) * m * 2.0 ** (rng.randint(lo, hi) - 23))
def rdouble(lo=-20, hi=20):
    m = rng.getrandbits(52) | 1 << 52
    return bits(rng.choice((1, -1)) * m * 2.0 ** (rng.randint(lo, hi) - 52))
def fl(b): return struct.unpack('>d', struct.pack('>Q', b))[0]
SPECIAL = [0, 1 << 63, bits(float('inf')), bits(float('-inf')), 0x7FF8000000000001, 0xFFF4000000000123,
           0x7FF0000000000001, 1, 0x8000000000000005, bits(2.0 ** -1022), bits(-(2.0 ** -1022)), bits(2.0 ** -149),
           bits(2.0 ** -150), bits(1e308), bits(-1e308), bits(3.4028234663852886e38), bits(1.0), bits(-1.0)]

def ties_add(sgl):
    """a + b exactly halfway between two neighbours of the target format: even/odd tie, both signs."""
    p = 24 if sgl else 53
    m = rng.getrandbits(p - 1) | 1 << (p - 1); e = rng.randint(-5, 5)
    a = m * 2.0 ** (e - p + 1); half = 2.0 ** (e - p)            # half an ulp of a
    if sgl: a = float(struct.unpack('>f', struct.pack('>f', a))[0])
    s = rng.choice((1, -1)); return bits(s * a), bits(s * half if rng.random() < .5 else s * 3 * half)

def fpscr_in(sgl=0, jit=0):   # sticky history | stale FR/FI/FPRF (must not survive) | enables | RN; jit: RN = 0, no enables
    f = rng.choice((0, 0, FX | XX, FX | OX | UX | XX, VXSNAN | VX | FX))
    if rng.random() < .3: f |= FR | FI | 0x14000
    if jit: return summ(f)
    for bit in (VE, ZE, XE) + ((OE, UE) if sgl else ()):   # OE/UE scaling: single precision only (see interp.c)
        if rng.random() < (.35 if bit in (OE, UE) else .2): f |= bit
    return summ(f | rng.randrange(4))

def A(sgl, xo, rc=0, d=1, a=2, b=3, c=4):
    if xo in (18, 20, 21): c = 0
    if xo == 25: b = 0
    return (59 if sgl else 63) << 26 | d << 21 | a << 16 | b << 11 | c << 6 | xo << 1 | rc
def X(xo, rc=0, d=1, b=3):
    return 63 << 26 | d << 21 | b << 11 | xo << 1 | rc

vec = []
def add_arith(sgl, xo, a, b, c, f, tag):
    try: r, fo, origin = arith(xo, sgl, f, a, b, c)
    except Boundary: return
    rc = rng.random() < 0.25
    vec.append((A(sgl, xo, rc), f, a, b, c, r, fo, origin, tag))

for sgl in (0, 1):
    for xo in (18, 20, 21, 25, 28, 29, 30, 31):
        gen = rsingle if sgl else rdouble
        for _ in range(60): add_arith(sgl, xo, gen(), gen(), gen(), fpscr_in(), 'random')
        for _ in range(12): add_arith(sgl, xo, gen(-1, 1), gen(-1, 1), gen(-1, 1), fpscr_in(), 'near')
        for _ in range(12):                                                  # big exponent difference
            add_arith(sgl, xo, gen(30, 40), gen(-40, -30), gen(-3, 3), fpscr_in(), 'expdiff')
        for _ in range(10):                                                  # cancellation
            x = gen(); y = bits(-fl(x) * (1 + 2.0 ** -(23 if sgl else 52) * rng.randint(1, 5)))
            add_arith(sgl, xo, x, y if xo != 20 else x ^ 0, gen(), fpscr_in(), 'cancel')
        for _ in range(8): add_arith(sgl, xo, rng.choice(SPECIAL), rng.choice(SPECIAL + [gen()]), rng.choice(SPECIAL + [gen()]), fpscr_in(), 'special')
        for _ in range(6): add_arith(sgl, xo, gen(500, 520) if not sgl else gen(60, 70), gen(500, 520) if not sgl else gen(60, 70), gen(500, 520) if not sgl else gen(60, 70), fpscr_in(sgl), 'overflow')
        for _ in range(6): add_arith(sgl, xo, gen(-540, -520) if not sgl else gen(-80, -70), gen(-540, -520) if not sgl else gen(-80, -70), gen(-540, -520) if not sgl else gen(-80, -70), fpscr_in(sgl), 'underflow')
        if sgl:
            for _ in range(10): add_arith(1, xo, rdouble(), rsingle(), rdouble(), fpscr_in(), 'mixed')   # PT_FP_MIXED inputs
            for _ in range(12): add_arith(1, xo, rsingle(60, 70), rsingle(55, 70), rsingle(60, 70), fpscr_in(1) | OE, 'OE')
            for _ in range(12): add_arith(1, xo, rsingle(-80, -66), rsingle(-80, -66), rsingle(-80, -66), fpscr_in(1) | UE, 'UE')
    for xo in (20, 21):
        for _ in range(20):
            x, y = ties_add(sgl)
            add_arith(sgl, 21, x, y, 0, fpscr_in(), 'tie')
            add_arith(sgl, 20, x, y ^ 1 << 63, 0, fpscr_in(), 'tie')
for _ in range(80):
    f = fpscr_in(1); b = rng.choice([rdouble(), rdouble(-130, -120), rdouble(120, 130), rdouble(-160, -140), rsingle(), rng.choice(SPECIAL)])
    try: r, fo, origin = frsp(f, b)
    except Boundary: continue
    vec.append((X(12, rng.random() < .25), f, 0, b, 0, r, fo, origin, 'frsp'))
for z in (0, 1):
    for _ in range(80):
        f = fpscr_in(); v = rng.choice([rng.uniform(-1e6, 1e6), rng.randint(-50, 50) + 0.5, rng.uniform(-3e9, 3e9),
                                        2147483647.5, -2147483648.5, -2147483648.25, 0.75, -0.75])
        b = rng.choice([bits(v), bits(v), rng.choice(SPECIAL)])
        r, fo, origin = fctiw(f, b, z); vec.append((X(15 if z else 14, rng.random() < .25), f, 0, b, 0, r, fo, origin, 'fctiwz' if z else 'fctiw'))
# FPSCRs the JIT takes itself (RN = 0, no enables), so its paths meet the oracle too; single
# operations also with double (non-single) inputs. Appended: the vectors above keep their random stream.
for sgl in (0, 1):
    for xo in (18, 20, 21, 25, 28, 29, 30, 31):
        gen = rsingle if sgl else rdouble
        for _ in range(20): add_arith(sgl, xo, gen(), gen(), gen(), fpscr_in(jit=1), 'jit')
        for _ in range(6): add_arith(sgl, xo, gen(-1, 1), gen(-1, 1), gen(-1, 1), fpscr_in(jit=1), 'jit-near')
        for _ in range(4): add_arith(sgl, xo, rng.choice(SPECIAL), rng.choice(SPECIAL + [gen()]), rng.choice(SPECIAL + [gen()]), fpscr_in(jit=1), 'jit-special')
        if sgl:
            for _ in range(20): add_arith(1, xo, rdouble(), rdouble(), rdouble(), fpscr_in(jit=1), 'jit-double-in')
            for _ in range(6): add_arith(1, xo, rdouble(), rsingle(), rdouble(), fpscr_in(jit=1), 'jit-mixed')
for _ in range(40):
    f = fpscr_in(jit=1); b = rng.choice([rdouble(), rdouble(-130, -120), rdouble(120, 130), rsingle(), rng.choice(SPECIAL)])
    try: r, fo, origin = frsp(f, b)
    except Boundary: continue
    vec.append((X(12, rng.random() < .25), f, 0, b, 0, r, fo, origin, 'jit-frsp'))
for z in (0, 1):
    for _ in range(40):
        f = fpscr_in(jit=1); v = rng.choice([rng.uniform(-1e6, 1e6), rng.randint(-50, 50) + 0.5, rng.uniform(-3e9, 3e9), 0.75, -0.75])
        b = rng.choice([bits(v), bits(v), rng.choice(SPECIAL)])
        r, fo, origin = fctiw(f, b, z); vec.append((X(15 if z else 14, rng.random() < .25), f, 0, b, 0, r, fo, origin, 'jit-fctiwz' if z else 'jit-fctiw'))

out = sys.stdout
out.write('/* Generated by tests/fp-oracle.py (seed 20261025): exact rational arithmetic, Book I rules. Do not edit. */\n')
out.write('typedef struct { uint32_t in, fpscr; uint64_t a, b, c; int keep; uint64_t t; uint32_t fpscr_out; const char *origin, *tag; } FpVector;\n')
out.write('static const FpVector fp_vectors[] = {\n')
for ins, f, a, b, c, r, fo, origin, tag in vec:
    out.write('    {0x%08x, 0x%08x, 0x%016xull, 0x%016xull, 0x%016xull, %d, 0x%016xull, 0x%08x, "%s", "%s"},\n' % (
        ins, f, a, b, c, r is None, r or 0, fo, origin, tag))
out.write('};\n')
print('fp-oracle: %d vectors, %d FR=1, %d ptolemy-convention' % (len(vec), sum(1 for v in vec if v[6] & FR),
      sum(1 for v in vec if v[7] == 'ptolemy')), file=sys.stderr)
