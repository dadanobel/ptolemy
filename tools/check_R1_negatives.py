#!/usr/bin/env python3
"""Mutation controls: the directed tests must reject the broken behavior."""
import os, pathlib, resource, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
flags=['clang','-std=c11','-D_DARWIN_C_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
with tempfile.TemporaryDirectory(prefix='pt-r1-negative-') as td:
    tmp=pathlib.Path(td)
    cases=[('overwrite','src/export.c','linkat(e->dir,e->temporary,e->dir,e->name,0)','renameat(e->dir,e->temporary,e->dir,e->name)',['tests/r1-export.c'],'source'),
           ('no-affine','include/pointer_calibration.h','double v=p->valid?(value-p->bias[axis])/p->scale[axis]:value','double v=value;(void)p;(void)axis',['tests/r1-input.c','-framework','CoreFoundation'],'header'),
           ('wrong-yuv-order','src/optics/pixel.c','uyvy=(p.format-OPTICS_PIXEL_YUYV601_SIGNED)&1','uyvy=0',['tests/r1-yuv.c'],'source'),
           ('bad-ip-checksum','src/net_user.c','||sum(ip,ihl,0)||','||0||',['src/platform.c'],'network')]
    for name,path,before,after,args,kind in cases:
        source=(root/path).read_text();assert source.count(before)==1,(name,'mutation anchor');changed=source.replace(before,after)
        target=tmp/pathlib.Path(path).name;target.write_text(changed);test_args=args[:]
        if kind=='source':test_args.append(str(target))
        if kind=='network':
            test=tmp/'network-test.c';test.write_text((root/'tests/r1-network.c').read_text().replace('../src/net_user.c',str(target)));test_args.append(str(test))
        output=tmp/name
        subprocess.run(flags+['-I'+str(tmp),'-Iinclude','-Isrc/optics']+test_args+['-pthread','-o',str(output)],cwd=root,check=True,capture_output=True)
        env=dict(os.environ,ASAN_OPTIONS='halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
        run=subprocess.run([str(output)],cwd=root,env=env,capture_output=True)
        assert run.returncode!=0,(name,'broken behavior passed')
        print('PASS: negative mutation rejected:',name,flush=True)
