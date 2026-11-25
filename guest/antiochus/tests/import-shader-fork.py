#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the four pinned Piglit compatibility directories without executing their code."""
import hashlib
import json
import re
import sys
import tarfile
from pathlib import Path

VERSION = '31b4a47baad31cd8c596d5ae6a8b078ac44e2cfa29930b107f96fe811dbdb29d'
ROOTS = ('glsl-1.10', 'glsl-1.20', 'arb_vertex_program', 'arb_fragment_program')
# These five upstream files target historical compiler/driver crashes or hangs.
# The distributed profile contains functional rendering and language assertions.
OUTSIDE_PROFILE = {
    'tests/spec/arb_vertex_program/big-param.shader_test',
    'tests/spec/glsl-1.10/execution/vs-loop-complex-unroll-nested-break.shader_test',
    'tests/spec/glsl-1.10/linker/fs-loop-unrolled-divide-by-zero.shader_test',
    'tests/spec/glsl-1.20/linker/array-initializers-1.shader_test',
    'tests/spec/glsl-1.20/linker/double-indirect-1.shader_test',
}
STAGE = {'vertex shader':'VS','fragment shader':'FS','vertex program':'VP','fragment program':'FP',
         'vertex shader passthrough':'PASSVS'}
# Each entry describes an entire command. Any unrecognised syntax stops the import.
GRAMMAR = {
    'probe warn all rgba':('WARN4',4), 'probe all rgba':('ALL4',4), 'probe all rgb':('ALL3',3),
    'relative probe rgba':('REL4',6),'relative probe rgb':('REL3',(5,6)),'probe rgba':('PIX4',6),
    'probe rgb':('PIX3',5),'probe rect rgba':('BOX4',8),'probe depth':('ZPIX',3),
    'draw rect tex':('QUADT',8),'draw rect':('QUAD',4),'ortho':('ORTHO',(0,4)),
    'clear color':('CLEARCOLOR',4),'clear depth':('CLEARZ',1),'clear':('CLEAR',0),
    'color':('COLOR',4),'texcoord':('TCOORD',5),'clip plane':('PLANE',5),
    'link error':('LINKERR',0),'link success':('LINKOK',0),
    'texture checkerboard':('CHECKER',12),'texture miptree':('MIP',1),
    'texture rgbw 1D':('RGB1',1),'texture rgbw 3D':('RGB3',1),'texture rgbw':('RGB2',3),
    'texture shadow1D':('Z1',2),'texture shadow2D':('Z2',3)
}
ENUM = {'always','never','equal','greater','luminance','intensity','alpha'}
q = lambda text: json.dumps(text,ensure_ascii=True)


def instruction(line):
    tokens = line.rstrip(';').translate(str.maketrans('(),','   ')).split()
    word, arg, nums = 'NULL', '0', []
    for prefix,(op,count) in GRAMMAR.items():
        words = prefix.split()
        if tokens[:len(words)] == words:
            nums = tokens[len(words):]
            if len(nums) not in ((count,) if isinstance(count,int) else count): raise ValueError(line)
            break
    else:
        head = tokens[0]
        if head == 'uniform':
            kind, name, *nums = tokens[1:]
            widths={'int':1,'uint':1,'float':1,'vec2':2,'vec3':3,'vec4':4,'ivec2':2,'ivec4':4,'mat4':16,'mat2x2':4}
            assert len(nums)==widths[kind] and re.fullmatch(r'[\w.\[\]]+',name)
            op,word='U_'+kind.upper(),q(name)
        elif head == 'parameter':
            _,kind,*nums=tokens
            assert kind in ('env_vp','local_vp','local_fp','env_fp') and len(nums)==5
            op='P_'+kind.upper()
        elif head in ('enable','disable','hint'):
            assert len(tokens)==(3 if head=='hint' else 2)
            assert re.fullmatch(r'GL_(?:CLIP_PLANE[0-5]|DEPTH_TEST|FRAGMENT_SHADER_DERIVATIVE_HINT)',tokens[1])
            if head=='hint': assert tokens[2]=='GL_NICEST'
            op,arg=head.upper(),tokens[1]
        elif head=='texparameter':
            _,dim,p,v=tokens
            assert dim in ('1D','2D') and p in ('depth_mode','compare_func') and v in ENUM
            op='TEXPARAM'; arg='GL_TEXTURE_'+dim
            nums=['GL_DEPTH_TEXTURE_MODE' if p=='depth_mode' else 'GL_TEXTURE_COMPARE_FUNC','GL_'+v.upper()]
        elif tokens[:2]==['draw','arrays']:
            _,_,arg,*nums=tokens
            assert arg in ('GL_LINES','GL_POINTS','GL_TRIANGLE_STRIP') and len(nums)==2
            op='ARRAYS'
        else: raise ValueError('unknown command '+line)
    for n in nums:
        if op!='TEXPARAM' and n not in ('INF','-INF','NAN') and not re.fullmatch(r'[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?',n): raise ValueError(line)
    if op=='ORTHO' and not nums: op='ORTHOWINDOW'
    return op,arg,word,','.join(n.replace('INF','__builtin_inff()') for n in nums) or '0'


def generate(archive, output):
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=VERSION: raise ValueError('wrong Piglit source archive')
    stages, commands, catalog = [],[],[]
    with tarfile.open(archive) as tar:
        for path in sorted(tar.getnames()):
            bits=path.split('/')
            if len(bits)<4 or bits[:2]!=['tests','spec'] or bits[2] not in ROOTS or not path.endswith('.shader_test'): continue
            if path in OUTSIDE_PROFILE: continue
            raw=tar.extractfile(path).read()
            text=raw.decode(); sid='sr-'+hashlib.sha256(path.encode()).hexdigest()[:12]
            start_s,start_c=len(stages),len(commands)
            width=height=250; depth=version=gl=0; exts=[]; vertex_data=False
            headers=list(re.finditer(r'^\[([^\]\n]+)\][ \t]*\r?\n',text,re.M))
            for i,h in enumerate(headers):
                key=h[1]; body=text[h.end():headers[i+1].start() if i+1<len(headers) else len(text)]
                line0=text.count('\n',0,h.end())+1
                if key in STAGE:
                    stages.append((STAGE[key],body,line0))
                elif key=='vertex data':
                    assert body.split()==['in_pos/float/2','0','0','10','0','0','10','10','10']
                    vertex_data=True
                else:
                    assert key in ('require','test')
                    for offset,line in enumerate(body.splitlines()):
                        line=line.split('#')[0].strip()
                        if not line: continue
                        if key=='test':
                            commands.append((line0+offset,line,*instruction(line)))
                        elif line.startswith('GLSL >= '): version=int(line[-4:].replace('.',''))
                        elif line.startswith('GL >= '): gl=round(float(line.split()[-1])*10)
                        elif line=='depthbuffer': depth=1
                        elif line.startswith('SIZE '): width,height=map(int,line.split()[1:])
                        elif re.fullmatch(r'(?:GL_)?ARB_\w+',line):
                            # Two legacy preprocessor fixtures capitalize the extension name in [require].
                            name = 'GL_ARB_draw_buffers' if line=='GL_ARB_DRAW_BUFFERS' else line
                            exts.append(name if name.startswith('GL_') else 'GL_'+name)
                        else: raise ValueError((path,line))
            assert len(stages)>start_s and len(commands)>start_c and (version in (110,120) or gl or exts)
            catalog.append((sid,path,hashlib.sha256(raw).hexdigest(),' '.join(exts),version,gl,width,height,depth,int(vertex_data),start_s,len(stages)-start_s,start_c,len(commands)-start_c))
    assert len(catalog)==338 and len(set(c[0] for c in catalog))==len(catalog)
    with output.open('w') as out:
        print('/* SPDX-License-Identifier: MIT\n * Generated from Piglit 62ef6b0db. See THIRD-PARTY.md and tests/import-shader-fork.py. */',file=out)
        print('static const SrStage sr_stages[]={',file=out)
        for kind,body,line in stages:
            print('{SR_%s,%d,\n%s},'%(kind,line,'\n'.join(map(q,body.splitlines(True))) or '""'),file=out)
        print('};\nstatic const SrCommand sr_commands[]={',file=out)
        for line,raw,op,arg,word,nums in commands:
            print('{%d,%s,SR_%s,%s,%s,{%s}},'%(line,q(raw),op,arg,word,nums),file=out)
        print('};\nstatic const SrCase sr_cases[]={',file=out)
        for c in catalog: print('{'+','.join(q(n) if isinstance(n,str) else str(n) for n in c)+'},',file=out)
        print('};',file=out)

if __name__=='__main__': generate(Path(sys.argv[1]),Path(sys.argv[2]))
