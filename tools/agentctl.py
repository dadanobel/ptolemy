#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Visible Tiger agent client. No shell expansion: pass an absolute guest executable.
Interrupting this client closes the connection and requests child-group cancellation.
Output is combined stdout/stderr, limited to 1MiB per command, not streamed live.
--put PATH [--name NAME] sends one host file into the guest's ~/Desktop (protocol 2: checked chunks, SHA-256 on both
sides, published by the guest only when complete, never over an existing file); a folder goes as NAME.tar, unpacked
by nobody. --clip FILE sets Tiger's text clipboard to the file's UTF-8 text (<= 1 MiB).
"""
import argparse
import os
from pathlib import Path
import socket
import struct
import sys
import tarfile
import tempfile

def exact(s,n):
    out=bytearray()
    while len(out)<n:
        b=s.recv(n-len(out))
        if not b:raise ConnectionError('VM disconnected; command outcome unknown')
        out.extend(b)
    return bytes(out)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--socket',required=True,type=Path)
    p.add_argument('--timeout',type=int,default=60);p.add_argument('--status',action='store_true')
    p.add_argument('--put',type=Path);p.add_argument('--name');p.add_argument('--clip',type=Path)
    p.add_argument('argv',nargs=argparse.REMAINDER);a=p.parse_args()
    argv=a.argv[1:] if a.argv[:1]==['--'] else a.argv
    if not 1<=a.timeout<=600:p.error('timeout must be 1..600 seconds')
    tmp=None
    if a.clip:
        if argv or a.status or a.put:p.error('--clip takes no command')
        body=a.clip.read_bytes();body.decode('utf-8');op=3
        if len(body)>1048576:p.error('clipboard text exceeds 1 MiB')
    elif a.put:
        if argv or a.status:p.error('--put takes no command')
        src=a.put.resolve();name=a.name or src.name+('.tar' if src.is_dir() else '')
        if src.is_dir():   # folders as one tar file, as the window drop does
            fd,tmp=tempfile.mkstemp(suffix='.tar');os.close(fd)
            with tarfile.open(tmp,'w',format=tarfile.GNU_FORMAT) as tf:tf.add(str(src),arcname=src.name)
            src=Path(tmp)
        path,nm=os.fsencode(str(src)),name.encode('utf-8')
        body=struct.pack('>I',len(path))+path+struct.pack('>I',len(nm))+nm;op=2
    elif a.status:
        if argv:p.error('--status does not accept a command')
        body=b'';op=0
    else:
        if not argv or not argv[0].startswith('/') or len(argv)>64:p.error('pass 1..64 arguments; first must be an absolute Tiger path')
        parts=[os.fsencode(v) for v in argv]
        if any(b'\0' in v for v in parts):p.error('NUL in argument')
        body=struct.pack('>I',len(parts))+b''.join(struct.pack('>I',len(v))+v for v in parts);op=1
        if len(body)>65536:p.error('command exceeds 64KiB')
    try:return exchange(a,op,body)
    finally:
        if tmp:os.unlink(tmp)
def exchange(a,op,body):
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
        s.settimeout(a.timeout+15);s.connect(str(a.socket))
        s.sendall(struct.pack('>4sIII',b'PTA1',op,len(body),a.timeout)+body)
        magic,kind,status,n=struct.unpack('>4sIII',exact(s,16))
        if magic!=b'PTA1' or kind>3 or n>1048576:raise ValueError('invalid agent response')
        out=exact(s,n);sys.stdout.buffer.write(out);sys.stdout.buffer.flush()
        if kind==2:print('Ptolemy agent rejected/aborted command.',file=sys.stderr);return 125
        if kind==3:print('\nPtolemy: OUTPUT TRUNCATED at 1MiB.',file=sys.stderr);return 124
        return min(status,255)
if __name__=='__main__':
    try:raise SystemExit(main())
    except KeyboardInterrupt:raise SystemExit(130)
    except (OSError,ValueError,ConnectionError) as e:print(str(e),file=sys.stderr);raise SystemExit(125)
