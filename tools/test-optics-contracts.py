#!/usr/bin/env python3
"""Permanent, bounded native contracts. No guest boot, images or benchmarks."""
import argparse,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',type=Path);p.add_argument('--sanitize',action='store_true');a=p.parse_args()
s=Path(__file__).resolve().parents[1];src=s/'src/optics';lib=(a.library or s/'build/optics/liboptics.a').resolve();assert lib.is_file(),lib
b=s/'build/contracts';b.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,OPTICS_EXACT_PITCH='0',OPTICS_DEPTH_CAPACITY='0',OPTICS_DEPTH_IO='0',OPTICS_2D_CONTRACT='0',OPTICS_BITBLT_MULTI='1',OPTICS_RENDER_OWNER='0',OPTICS_PASS_REASONS='0',OPTICS_DIRECT_VRAM='0',OPTICS_RAW_VERTICES='0',OPTICS_RAW_STREAMS='0',OPTICS_VAP_NORM2='0',OPTICS_GPU_VTE='0',OPTICS_STRICT='0',PT_PIC_ACTIVITY='0',MTL_DEBUG_LAYER='1',ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
cc=subprocess.check_output(['xcrun','--find','clang'],text=True).strip();flags=[cc,'-arch','arm64','-O2','-g','-Wall','-Wextra','-Werror','-D_DARWIN_C_SOURCE','-I'+str(src),'-I'+str(src/'foundation'),'-iquote'+str(s/'include')]
if a.sanitize:flags+=['-fsanitize=address,undefined']
records=[];passed=False

def run(cmd,extra=None,negative=False):
 e=env.copy();e.update(extra or {});x=subprocess.run(list(map(str,cmd)),env=e,capture_output=True,text=True,timeout=120)
 records.append(dict(argv=list(map(str,cmd)),environment=extra or {},returncode=x.returncode,expected_failure=negative,stdout=x.stdout,stderr=x.stderr))
 if negative:assert x.returncode==-6 and 'Assertion failed:' in x.stderr,x.stdout+x.stderr
 else:assert x.returncode==0,x.stdout+x.stderr

def test(name,extra=None,negative=None,args=(),cpp=False):
 exe=b/name;f=flags+(['-std=c++17','-fobjc-arc'] if cpp else ['-std=c11'])
 run(f+[s/('tests/'+name+('.mm' if cpp else '.c')),b/'platform.o',lib,'-lc++','-framework','Metal','-framework','Foundation','-o',exe])
 run([exe,*args],extra)
 if negative is not None:run([exe,*args],negative,True)
 return exe

try:
 run(flags+['-std=c11','-c',s/'src/platform.c','-o',b/'platform.o'])
 test('optics-color-pitch',{'OPTICS_EXACT_PITCH':'1'},{'OPTICS_EXACT_PITCH':'0'})
 test('optics-draw-cleanup')
 test('optics-draw-decode')
 test('optics-2d-contract',{'OPTICS_2D_CONTRACT':'1'},{'OPTICS_2D_CONTRACT':'0'})
 test('optics-blit-accounting')
 test('optics-display-mode')
 test('optics-cpu-view',{'OPTICS_DIRECT_VRAM':'1'},{'OPTICS_DIRECT_VRAM':'0'})
 test('optics-bitblt-multi',{'OPTICS_BITBLT_MULTI':'1'},{'OPTICS_BITBLT_MULTI':'0'})
 test('optics-byte-matrix')
 test('optics-owner-memory-policy',{'OPTICS_DIRECT_VRAM':'1','OPTICS_RENDER_OWNER':'1'},{'OPTICS_DIRECT_VRAM':'1','OPTICS_RENDER_OWNER':'0'})
 test('optics-depth-metal')
 test('optics-mip-levels')
 test('optics-dxt')
 test('optics-colour-formats')
 test('optics-lines-blend')
 test('optics-pvs-math',cpp=True)
 test('optics-depth-growth',{'OPTICS_DEPTH_CAPACITY':'1'},{'OPTICS_DEPTH_CAPACITY':'0'})
 test('optics-depth-io',{'OPTICS_DEPTH_IO':'1'},{'OPTICS_DEPTH_IO':'0'})
 # The complete depth contract also runs with FIFO Metal ownership enabled.
 run([b/'optics-depth-io'],{'OPTICS_DEPTH_IO':'1','OPTICS_RENDER_OWNER':'1'})
 exe=test('optics-render-owner',{'OPTICS_RENDER_OWNER':'1','OPTICS_PASS_REASONS':'1'},{'OPTICS_RENDER_OWNER':'0'},cpp=True)
 run([exe],{'OPTICS_RENDER_OWNER':'1','OPTICS_PASS_REASONS':'0'},True)
 exe=test('optics-raw-vertices',{'OPTICS_RAW_VERTICES':'1'},{'OPTICS_RAW_VERTICES':'0'})
 run([exe,'normalized'],{'OPTICS_RAW_VERTICES':'0'})
 exe=test('optics-raw-vertices-metal')
 run([exe],{'OPTICS_RENDER_OWNER':'1'})
 test('openpic-contract',{'PT_PIC_ACTIVITY':'1'},{'PT_PIC_ACTIVITY':'0'})
 run(['python3',s/'tests/fp-range-oracle.py'])
 run(['python3',s/'tools/check_VAP.py','--library',lib]+(['--sanitize'] if a.sanitize else []))
 passed=True
finally:
 (b/'results.json').write_text(json.dumps(dict(passed=passed,library=str(lib),test_sanitizers=a.sanitize,checks=records),indent=2)+'\n')
print('PASS: permanent native Optics/CPU contracts;',sum(x['expected_failure'] for x in records),'assertion-based negative controls')
