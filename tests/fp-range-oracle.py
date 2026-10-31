#!/usr/bin/env python3
"""Exact rational double multiply/divide OE/UE oracle; no guest boot."""
import argparse
from fractions import Fraction as Q
import json
import os
from pathlib import Path
import random
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path);a=p.parse_args()
root=Path(__file__).resolve().parents[1];stand=(a.source or root).resolve()
build=root/'build/contracts/fp-range';build.mkdir(parents=True,exist_ok=True);exe=build/'fp-range'
env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
records=[]
def run(cmd,extra=None,data=None):
    e=env.copy();e.update(extra or {});x=subprocess.run(list(map(str,cmd)),input=data,text=True,capture_output=True,env=e,timeout=90)
    records.append(dict(argv=list(map(str,cmd)),environment=extra or {},returncode=x.returncode,stderr=x.stderr))
    if x.returncode:raise RuntimeError(x.stderr)
    return x.stdout
def power(n):return Q(1<<n) if n>=0 else Q(1,1<<-n)
def value(u):
    m=u&((1<<52)-1);e=(u>>52)&2047
    return (-1 if u>>63 else 1)*Q(m+(1<<52) if e else m)*power(e-1075 if e else -1074)
def rounded(q,rn):
    neg=q<0;x=abs(q);n,d=x.numerator,x.denominator;e=n.bit_length()-d.bit_length()
    if x<power(e):e-=1
    scale=52-e;t=x*power(scale);m,rem=divmod(t.numerator,t.denominator)
    inc=bool(rem) and (rn==0 and (2*rem>t.denominator or 2*rem==t.denominator and m&1) or rn==2 and not neg or rn==3 and neg)
    m+=inc
    if m==1<<53:m>>=1;e+=1
    return neg,e,m,bool(rem),bool(inc)
def expected(op,control,a,b):
    q=value(a)*value(b) if op==25 else value(a)/value(b);rn=control&3
    over=rounded(q,rn)[1]>1023
    tiny=abs(q)<power(-1022)
    cause=(1<<28) if over and control&64 else (1<<27) if tiny and control&32 else 0
    if not cause:return None
    q*=power(-1536 if cause==1<<28 else 1536)
    neg,e,m,inexact,increment=rounded(q,rn);assert -1022<=e<=1023
    bits=(int(neg)<<63)|((e+1023)<<52)|(m- (1<<52))
    fpscr=control|(1<<31)|(1<<30)|cause|((8 if neg else 4)<<12)
    if inexact:fpscr|=(1<<25)|(1<<17)
    if increment:fpscr|=1<<18
    return bits,fpscr,fpscr>>28
rng=random.Random(0x7450);operands=[]
for _ in range(400):
    # Include normal and subnormal operands near both extreme exponent boundaries.
    ea=rng.choice([0,1,2,50,100,511,1022,1023,1534,1990,2045,2046])
    eb=rng.choice([0,1,2,50,100,511,1022,1023,1534,1990,2045,2046])
    operands.append(((ea<<52)|rng.getrandbits(52)|1,(eb<<52)|rng.getrandbits(52)|1))
operands += [(1,1),(1,0x7fefffffffffffff),(0x7fefffffffffffff,1),
             (0x0010000000000000,0x3fefffffffffffff),(0x7fefffffffffffff,0x4000000000000000),
             (0x0010000000000000,0x3fe0000000000000),(0x0010000000000000,0x4000000000000000)]
cases=[]
for op in (18,25):
    for a0,b0 in operands:
        for sign in range(4):
            a1=a0|((sign&1)<<63);b1=b0|((sign>>1)<<63)
            for rn in range(4):
                for enables in (32,64,96):
                    control=rn|enables;want=expected(op,control,a1,b1)
                    if want:cases.append((op,control,a1,b1,want))
data=''.join(f'{op} {c:x} {a:x} {b:x}\n' for op,c,a,b,_ in cases)
passed=False
try:
    run(['xcrun','clang','-arch','arm64','-std=gnu11','-D_DARWIN_C_SOURCE','-O2','-g',
         '-fsanitize=address,undefined','-I'+str(stand/'epicycle'),stand/'tests/fp-range.c',
         stand/'epicycle/interp.c',stand/'epicycle/mmu.c',stand/'epicycle/altivec.c','-o',exe])
    for flag in ('1','0'):
        lines=run([exe],{'PT_FP_RANGE':flag},data).splitlines();assert len(lines)==len(cases)
        bad=[]
        for case,line in zip(cases,lines):
            got=tuple(int(x,16) for x in line.split())
            if got!=case[-1]:bad.append(dict(input=case[:4],expected=case[-1],actual=got))
        records.append(dict(flag=flag,cases=len(cases),mismatches=len(bad),examples=bad[:6],expected_failure=flag=='0'))
        assert not bad if flag=='1' else len(bad)>100
    passed=True
finally:
    (build/'results.json').write_text(json.dumps(dict(passed=passed,checks=records),indent=2)+'\n')
print(f'S5 range contracts passed: {len(cases)} exact cases; default-off negative control detected')
