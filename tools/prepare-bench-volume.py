#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""APFS-clone a quiescent raw HL volume and replace its existing run.command.
Only the new clone is written; existing destinations are never overwritten.
The catalog, extents and logical file size stay unchanged (space padding).
"""
import ctypes,errno,importlib.util,os,struct,sys
from pathlib import Path
root=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('pt_hfs',root/'hfs-read.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
if len(sys.argv)!=3:raise SystemExit('usage: prepare-bench-volume.py QUIESCENT-RAW-HL NEW-CLONE')
source=Path(sys.argv[1]).resolve(strict=True);out=Path(sys.argv[2]).absolute()
libc=ctypes.CDLL(None,use_errno=True);clone=libc.clonefile;clone.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int];clone.restype=ctypes.c_int
if clone(os.fsencode(source),os.fsencode(out),0):raise OSError(ctypes.get_errno(),os.strerror(ctypes.get_errno()),str(out))
h=m.HFS(out)
try:
 old=h.file('/run.command');script=(root/'bench-native.command').read_bytes()
 res=os.environ.get('PT_BENCH_RES')  # WxH instead of 800x600 (high-resolution measurements)
 if res:w,hh=(int(v) for v in res.lower().split('x'));script=script.replace(b'-width 800 -height 600',b'-width %d -height %d'%(w,hh))
 aa=os.environ.get('PT_BENCH_MSAA')  # test-only gl_msaa_samples (FCVAR_GLCONFIG: opengl.cfg is read before video init)
 if aa:script=script.replace(b'"$D/Half-Life.app/',b'{ grep -v "^gl_msaa_samples" "$D/valve/opengl.cfg"; echo \'gl_msaa_samples "%d"\'; } > "$D/o.tmp"; mv "$D/o.tmp" "$D/valve/opengl.cfg"\n"$D/Half-Life.app/'%int(aa),1)
 if not old.startswith(b'#!/bin/sh') or len(script)>len(old):raise ValueError('existing shell script cannot contain the replacement')
 kind,body=h.entries[2,'run.command'];fork=body[88:168];data=script+b' '*(len(old)-len(script));done=0
 fd=os.open(out,os.O_RDWR)
 try:
  for i in range(8):
   start,count=struct.unpack_from('>II',fork,16+8*i);n=min(count*h.block,len(data)-done)
   if n:
    if os.pwrite(fd,data[done:done+n],h.base+start*h.block)!=n:raise OSError('short clone write')
    done+=n
   if done==len(data):break
  if done!=len(data):raise ValueError('extent overflow is unsupported')
  os.fsync(fd)
 finally:os.close(fd)
 if h.file('/run.command')!=data:raise ValueError('readback mismatch')
 print(f'{out}: installed {len(script)} bytes in {len(old)}-byte script; source untouched')
finally:h.close()
