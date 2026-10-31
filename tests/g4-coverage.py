#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent NXP instruction inventory; scalar smoke is not a semantic oracle."""
import json,struct,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parent
vmx=json.loads((root/'g4-vmx-manifest.json').read_text())['instructions']
arithmetic=json.loads((root/'altivec-cases.json').read_text())
assert len(vmx)==162
memory={'dss','dssall','dst','dstt','dstst','dststt','lvebx','lvehx','lvewx','lvsl','lvsr','lvx','lvxl','stvebx','stvehx','stvewx','stvx','stvxl'}
assert {n for n,v in vmx.items() if v['major']==31}==memory
assert set(arithmetic)=={n for n,v in vmx.items() if v['major']==4}|{n+'.' for n in vmx if n.startswith('vcmp')}
for name,v in vmx.items():
    if v['major']==4:assert arithmetic[name]==v['xo']
cases=json.loads((root/'g4-scalar-manifest.json').read_text())['cases']
records=[];expected=[]
for pvr in [0x000c0209,0x80030102]:
    for c in cases:
        records.append(struct.pack('>II',int(c['instruction'],16),pvr))
        expected.append((pvr,c))
result=subprocess.run([sys.argv[1]],input=b''.join(records),stdout=subprocess.PIPE,check=True).stdout
assert len(result)==12*len(records)
for i,(pvr,c) in enumerate(expected):
    pc,srr1,halted=struct.unpack_from('>III',result,12*i)
    illegal=c['illegal'] or (pvr==0x000c0209 and c['name'] in ['tlbld','tlbli'])
    if halted or (pc==0x700 and bool(srr1&0x80000))!=illegal:
        raise SystemExit(f'{pvr:08x} {c}: pc={pc:x} SRR1={srr1:x} halted={halted}')
print(f'Classic VMX: all {len(vmx)} mnemonics inventoried; 157 arithmetic/record forms + 18 memory/stream forms covered')
print(f'G4 scalar decode: {len(records)} forms on 7400/7447A, including illegal fsqrt(s)/tlbia: PASS')
