#!/usr/bin/env python3
"""Bounded VAP contracts. CPU-only mode never opens a Metal device or starts Tiger."""
import argparse,json,os,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',type=Path);p.add_argument('--cpu-only',action='store_true');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
s=Path(__file__).resolve().parents[1];src=s/'src/optics';lib=(a.library or s/'build/optics/liboptics.a').resolve()
if not a.cpu_only:assert lib.is_file(),lib
b=s/'build/vap-contracts';b.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,OPTICS_RAW_VERTICES='0',OPTICS_RAW_STREAMS='0',OPTICS_VAP_NORM2='0',OPTICS_GPU_VTE='0',OPTICS_RENDER_OWNER='0',OPTICS_DIRECT_VRAM='0',OPTICS_DEPTH_CAPACITY='0',OPTICS_DEPTH_IO='0',OPTICS_STRICT='0',OPTICS_MSL_CACHE='',OPTICS_SHADER_FAILURES='',OPTICS_TEST_WRONG_SWAP='',MTL_DEBUG_LAYER='1',ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
env.pop('OPTICS_TEST_WRONG_SWAP',None);records=[];passed=False
flags=['-O1','-g','-Wall','-Wextra','-Werror','-D_DARWIN_C_SOURCE','-I'+str(src),'-iquote'+str(s/'include')]
if a.sanitize:flags+=['-fsanitize=address,undefined']
def run(cmd,extra=None,negative=False):
 e=env.copy();e.update(extra or {});r=subprocess.run(list(map(str,cmd)),env=e,capture_output=True,text=True,timeout=180)
 records.append(dict(argv=list(map(str,cmd)),environment=extra or {},returncode=r.returncode,expected_failure=negative,stdout=r.stdout,stderr=r.stderr))
 if negative:assert r.returncode==-6 and 'Assertion failed:' in r.stderr,r.stdout+r.stderr
 else:assert r.returncode==0,r.stdout+r.stderr
 return r

def compile_test(name,cpp=False):
 exe=b/name
 cmd=['clang++' if cpp else 'clang','-std=c++17' if cpp else '-std=c11',*flags]
 if cpp:cmd+=['-fobjc-arc']
 cmd +=[s/('tests/'+name+('.mm' if cpp else '.c'))]
 if name=='optics-vap-streams':cmd +=[src/n for n in ['draw.c','r300_state.c','pixel.c','foundation/pm4.c','foundation/shader_decode.c','foundation/shader_eval.c']]
 elif name=='optics-shader-failure':cmd +=[src/'foundation/shader_decode.c']
 else:
  cmd +=[b/'platform.o',lib]
  if not cpp:cmd+=['-lc++']
  cmd +=['-framework','Metal','-framework','Foundation']
 run(cmd+['-o',exe]);return exe

try:
 run(['clang','-std=c11',*flags,'-c',s/'src/platform.c','-o',b/'platform.o'])
 exe=compile_test('optics-vap-streams')
 for norm in ['0','1']:
  for raw in ['0','1']:
   run([exe]+(['decoded'] if raw=='0' else []),{'OPTICS_RAW_VERTICES':'1','OPTICS_RAW_STREAMS':raw,'OPTICS_VAP_NORM2':norm})
 run([exe],{'OPTICS_RAW_VERTICES':'0','OPTICS_RAW_STREAMS':'1'},True)
 run([compile_test('optics-shader-failure')])
 if not a.cpu_only:
  exe=compile_test('optics-vap-input-metal',True);run([exe]);run([exe],{'OPTICS_TEST_WRONG_SWAP':'1'},True)
  exe=compile_test('optics-vte-metal')
  for owner in ['0','1']:
   for vte in ['0','1']:
    r=run([exe],{'OPTICS_RENDER_OWNER':owner,'OPTICS_GPU_VTE':vte});m=re.search(r'full GPU VTE draws (\d+)',r.stderr)
    assert m and int(m[1])==(512 if vte=='1' else 0),r.stderr
 passed=True
finally:
 (b/'results.json').write_text(json.dumps(dict(passed=passed,cpu_only=a.cpu_only,sanitizers=a.sanitize,checks=records),indent=2)+'\n')
print('PASS: VAP contracts; GPU skipped' if a.cpu_only else 'PASS: native VAP input and full VTE contracts')
