#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build and exercise device contracts, without a guest, ROM binary or GPU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

UNITS = ('platform block dma scsi atapi ata hid input usb_msc ohci audio audio_macos pmu nvram board devices').split()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    ap.add_argument('--output',type=Path)
    args=ap.parse_args();root=args.root.resolve();out=(args.output or root/'build/s4-check').resolve();out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    env['ASAN_OPTIONS']='halt_on_error=1:abort_on_error=1'
    env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
    results=[]
    def run(argv,overrides=None,negative=False,expected=None):
        e=env.copy();e.update(overrides or {})
        p=subprocess.run([str(x) for x in argv],cwd=root,env=e,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        results.append({'command':[str(x) for x in argv],'flags':overrides or {},'returncode':p.returncode,'negative':negative,'output':p.stdout})
        if (p.returncode==0)==negative:
            print(p.stdout,flush=True);raise RuntimeError('negative control passed unexpectedly' if negative else 'command failed: '+str(argv[0]))
        if negative and expected and expected not in p.stdout:raise RuntimeError('negative control failed for an unrelated reason: '+p.stdout)
        print(p.stdout if p.returncode==0 else 'PASS: negative control rejected',end='' if p.stdout.endswith('\n') and not negative else '\n',flush=True)
    cc=['clang','-std=c11','-D_DARWIN_C_SOURCE','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'include')]
    frameworks=['-framework','AudioUnit','-framework','AudioToolbox','-framework','CoreAudio','-pthread']
    try:
        objects=[]
        for unit in UNITS:
            obj=out/(unit+'.o');run(cc+['-c',root/'src'/f'{unit}.c','-o',obj,'-pthread']);objects.append(obj)
        archive=out/'devices.a';archive.unlink(missing_ok=True);run(['ar','rcs',archive]+objects)
        tests=['s4-native','s4-audio','s4-storage','s4-pci','peripherals','product','platform-v3','pmu-faults','optical','media-lifetime']
        for test in tests:run(cc+[root/'tests'/f'{test}.c',archive,'-o',out/test]+frameworks)
        for test in tests:run([out/test])
        # Every device access to guest RAM is announced; RAM is read-only/owned in this test, so a bypass crashes.
        run(cc+[root/'tests/s2-writers.c',root/'src/net_rtl.c',archive,'-o',out/'s2-writers']+frameworks);run([out/'s2-writers'])
        macio=root/'firmware/openbios/drivers/macio.c'
        if not macio.is_file():macio=root.parents[1]/'sources/firmware/openbios/drivers/macio.c'   # the development layout
        source=macio.read_text();start=source.index('static void macio_ptolemy_sound_init(void)');begin=source.index('{',start);depth=1;end=begin+1
        while depth:
            depth+=(source[end]=='{')-(source[end]=='}');end+=1
        (out/'s4-rom-function.inc').write_text(source[start:end]+'\n')
        run(cc+['-I'+str(out),root/'tests/s4-rom.c','-o',out/'s4-rom'])
        run([out/'s4-rom'])
        # Mutation controls change observable protocol facts, not the assertions.
        with tempfile.TemporaryDirectory(prefix='s4-mutation-',dir=out) as tmp:
            tmp=Path(tmp)
            path=tmp/'hid.c';s=(root/'src/hid.c').read_text();assert '0x020e' in s;path.write_text(s.replace('0x020e','0x020f'))
            run(cc+['-c',path,'-o',tmp/'hid.o','-pthread'])
            run(cc+[root/'tests/s4-native.c',tmp/'hid.o',archive,'-o',tmp/'bad-hid']+frameworks)
            run([tmp/'bad-hid'],negative=True,expected='pt_read_le(data+10,2)==0x020e')
            # A changed RX DMA offset must be caught without compiling a ROM.
            path=out/'s4-rom-function.inc';saved=path.read_text();assert '0x8900' in saved;path.write_text(saved.replace('0x8900','0x8880'))
            run(cc+['-I'+str(out),root/'tests/s4-rom.c','-o',tmp/'bad-rom'])
            run([tmp/'bad-rom'],negative=True,expected='reg[k]');path.write_text(saved)
            path.write_text(saved.replace('port-connection 2 ','port-connection 0 '))
            run(cc+['-I'+str(out),root/'tests/s4-rom.c','-o',tmp/'bad-audio-route'])
            run([tmp/'bad-audio-route'],negative=True,expected='port-connection 2');path.write_text(saved)
            path=tmp/'audio.c';text=(root/'src/audio.c').read_text();assert 's->codec[data>>1&7]<<4' in text
            path.write_text(text.replace('s->codec[data>>1&7]<<4','s->codec[data>>1&7]'))
            run(cc+['-c',path,'-o',tmp/'audio.o','-pthread'])
            run(cc+[root/'tests/s4-audio.c',tmp/'audio.o',archive,'-o',tmp/'bad-readback']+frameworks)
            run([tmp/'bad-readback'],negative=True,expected='0x155')
            # An ATA DMA copy that skips the announcement must be caught by the writer test (s2-writers.c).
            path=tmp/'ata.c';text=(root/'src/ata.c').read_text();old='int busy;uint8_t *ram=pt_ram_use(a->dma->ram,slice.address,take,!a->native_write,&busy);'
            assert old in text;path.write_text(text.replace(old,'int busy=0;uint8_t *ram=pt_ram(a->dma->ram,slice.address,take);'))
            run(cc+['-I'+str(root/'src'),'-c',path,'-o',tmp/'ata.o','-pthread'])
            run(cc+[root/'tests/s2-writers.c',root/'src/net_rtl.c',tmp/'ata.o',archive,'-o',tmp/'bad-s2']+frameworks)
            run([tmp/'bad-s2'],negative=True,expected='on unknown address')   # SIGSEGV on Linux, SIGBUS on macOS
        print('PASS: S4 host contracts; live Tiger acceptance remains separate',flush=True)
    finally:
        sources={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'src'/f'{u}.c' for u in UNITS if (root/'src'/f'{u}.c').exists()]+sorted(q for f in ['include','tests'] for q in (root/f).glob('**/*') if q.is_file())}
        (out/'results.json').write_text(json.dumps({'sources':sources,'runs':results},indent=2)+'\n')

if __name__=='__main__':main()
