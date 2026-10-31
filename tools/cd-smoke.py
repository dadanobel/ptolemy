#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
import pathlib,subprocess,selectors,time,sys,json,os
kit=pathlib.Path(__file__).resolve().parent.parent
rom,cd,out=map(pathlib.Path,sys.argv[1:4]);out.mkdir(exist_ok=False)
cmd=[str(kit/'build/ptolemy-engine'),'--rom',str(rom),'--cd',str(cd),'--ram-mb','512','--seconds','8']
p=subprocess.Popen(cmd,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
data=bytearray();sent=False;started=time.monotonic()
with selectors.DefaultSelector() as poll:
    poll.register(p.stdout,selectors.EVENT_READ)
    while p.poll() is None and time.monotonic()-started<12:
        for key,_ in poll.select(.1):
            chunk=key.fileobj.read1(65536)
            if not chunk:poll.unregister(key.fileobj);continue
            data.extend(chunk)
            if not sent and b'0 > ' in data:
                p.stdin.write(os.environ.get('PT_OF_COMMAND',r'1 2 + .|show-devs|dir cd:,\\').replace('|','\r').encode()+b'\r');p.stdin.flush();sent=True
    if p.poll() is None:p.terminate()
    tail,_=p.communicate(timeout=3);data.extend(tail)
(out/'console.txt').write_bytes(data)
(out/'result.json').write_text(json.dumps({'command':cmd,'returncode':p.returncode,'scope':'OpenBIOS optical enumeration and HFS directory; no installation'},indent=2)+'\n')
print(data.decode(errors='replace')[-6500:])
