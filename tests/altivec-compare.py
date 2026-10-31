#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare classic VMX registers, SAT/NJ and CR6 against an independent 7400.
Estimates have ISA-defined approximation freedom, so their finite results
are compared with an error bound rather than with the reference's low bits.
Reference: Unicorn's 7400 model (external test tool, no downloads).
"""
import json,random,struct,subprocess,sys,math
from pathlib import Path
from unicorn import Uc,UC_ARCH_PPC,UC_MODE_PPC32,UC_MODE_BIG_ENDIAN
import unicorn.ppc_const as P
root=Path(__file__).resolve().parent
cases=json.loads((root/'altivec-cases.json').read_text());rng=random.Random(0x74009700)
n=int(sys.argv[2]) if len(sys.argv)>2 else 64
be=lambda x:struct.pack('>I',x&0xffffffff)
X=lambda xo,t,a,b:31<<26|t<<21|a<<16|b<<11|xo<<1
words=[14<<26|4<<21|512,X(103,0,3,4),0x10000644]
for i in range(32):words += [14<<26|4<<21|16*i,X(103,i,3,4)]
where=len(words);words += [0]
for i in range(32):words += [14<<26|4<<21|16*i,X(231,i,5,4)]
words += [0x10000604,14<<26|4<<21|512,X(231,0,5,4)]
u=Uc(UC_ARCH_PPC,UC_MODE_PPC32|UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(P.UC_CPU_PPC32_7400_V2_9);u.mem_map(0x10000,0x30000)
records=[];expected=[];spec=[]
edges=[0,0x80000000,1,0x007fffff,0x00800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xff812345,0x3f000000,0x4f800000]
for name,xo in cases.items():
 for iteration in range(n):
    d,a,b,c=[rng.randrange(32) for _ in range(4)]
    if name in ['vrefp','vrsqrtefp','vexptefp','vlogefp','vrfin','vrfiz','vrfim','vrfip','vupkhsb','vupkhsh','vupklsb','vupklsh','vupkhpx','vupklpx']:a=0
    if name=='mfvscr':a=b=0
    if name=='mtvscr':a=d=0
    if name=='vsldoi':c &=15
    ins=4<<26|d<<21|a<<16|b<<11| (c<<6|xo if xo in range(32,48) else xo)
    # Those VX opcodes happen to be numerically small too; only VA names use C.
    va=name in ['vmhaddshs','vmhraddshs','vmladduhm','vmsumubm','vmsummbm','vmsumuhm','vmsumuhs','vmsumshm','vmsumshs','vsel','vperm','vsldoi','vmaddfp','vnmsubfp']
    ins=4<<26|d<<21|a<<16|b<<11|(c<<6|xo if va else xo)
    data=bytearray(rng.getrandbits(8) for _ in range(512))
    if iteration<16:
        for off in range(0,512,4):data[off:off+4]=be(rng.choice(edges))
    if name in ['vsl','vsr']:
        sh=rng.randrange(8)
        for j in range(16):data[b*16+j]=(data[b*16+j]&0xf8)|sh
    vscr=rng.choice([0,1,0x10000,0x10001]);cr=rng.randrange(16)
    if name=="mtvscr":data[16*b+12:16*b+16]=be(rng.choice([0,1,0x10000,0x10001]))
    records.append(be(ins)+be(0x000c0209)+be(vscr)+be(cr)+data)
    words[where]=ins;code=b''.join(map(be,words));u.mem_write(0x10000,code);u.ctl_remove_cache(0x10000,0x10000+len(code))
    u.mem_write(0x20000,bytes(data)+bytes(12)+be(vscr));u.reg_write(P.UC_PPC_REG_3,0x20000);u.reg_write(P.UC_PPC_REG_5,0x30000)
    u.reg_write(P.UC_PPC_REG_CR,cr<<4);u.reg_write(P.UC_PPC_REG_MSR,0x02000000)
    u.emu_start(0x10000,0x10000+len(code),count=len(words))
    out=bytes(u.mem_read(0x30000,528));expected.append(out[:512]+out[524:528]+be((u.reg_read(P.UC_PPC_REG_CR)>>4)&15))
    spec.append((name,iteration,ins,d))
proc=subprocess.run([sys.argv[1]],input=b''.join(records),stdout=subprocess.PIPE,check=True)
if len(proc.stdout)!=524*len(expected):raise SystemExit('native result length mismatch')
failures=[];oracle_exp2_differences=0
for i,(ref,(name,it,ins,d)) in enumerate(zip(expected,spec)):
    block=proc.stdout[i*524:(i+1)*524];status=struct.unpack('>I',block[:4])[0];actual=block[4:]
    if status!=1:failures.append((name,it,f'unsupported {ins:08x}'));continue
    if name=='vexptefp':
        # The reference's softfloat float32_exp2 uses a Taylor polynomial without
        # range reduction: e.g. exp2(-FLT_MAX) becomes -Inf. Compare this
        # estimate to its mathematical definition, including NJ, instead.
        rec=records[i];b=ins>>11&31;nj=bool(struct.unpack('>I',rec[8:12])[0]&0x10000)
        corrected=bytearray(ref)
        for lane in range(4):
            bits=struct.unpack('>I',rec[16+16*b+4*lane:20+16*b+4*lane])[0]
            if nj and not(bits&0x7f800000):bits &=0x80000000
            x=struct.unpack('>f',be(bits))[0]
            if math.isnan(x):value=bits|0x400000
            elif x>=128:value=0x7f800000
            elif x<=-150:value=0
            else:
                value=struct.unpack('>I',struct.pack('>f',2.0**x))[0]
                if nj and not(value&0x7f800000):value=0
            corrected[16*d+4*lane:16*d+4*lane+4]=be(value)
        if corrected!=ref:oracle_exp2_differences+=1
        ref=bytes(corrected)
    if actual!=ref:
        estimate=name in ['vrefp','vrsqrtefp','vexptefp','vlogefp']
        ok=estimate and actual[:16*d]==ref[:16*d] and actual[16*(d+1):]==ref[16*(d+1):]
        if ok:
            for off in range(16*d,16*d+16,4):
                af=struct.unpack('>f',actual[off:off+4])[0];rf=struct.unpack('>f',ref[off:off+4])[0]
                if not ((math.isnan(af) and math.isnan(rf)) or af==rf or (math.isfinite(af) and math.isfinite(rf) and abs(af-rf)<=max(abs(rf)*2**-12,2**-145))):ok=False
        if not ok:
            off=next(k for k,(x,y) in enumerate(zip(actual,ref)) if x!=y)
            failures.append((name,it,f'{ins:08x} byte{off}: native {actual[max(0,off//4*4):off//4*4+4].hex()} reference {ref[max(0,off//4*4):off//4*4+4].hex()}'))
if failures:
    for f in failures[:35]:print(*f)
    print(f'{len(failures)} failures / {len(expected)}; operations:',sorted(set(x[0] for x in failures)));sys.exit(1)
if oracle_exp2_differences:print(f'vexptefp: mathematical oracle used; {oracle_exp2_differences} reference estimate discrepancies (softfloat range reduction)')
print(f'VMX 7400: {len(cases)} opcodes, {len(expected)} cases, vector registers + VSCR + CR6: PASS')
