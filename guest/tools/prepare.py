#!/usr/bin/python
# SPDX-License-Identifier: GPL-2.0-or-later
# Python 2.3. Stages the freshly compiled Ptolemy Agent.app and the installer; no Apple developer files.
import os,sys,struct,sha,shutil
def macho(path):
    f=open(path,'rb')
    try:b=f.read(8)
    finally:f.close()
    if len(b)!=8 or struct.unpack('>II',b)!=(4277009102,18):raise RuntimeError('Not PPC32 Mach-O: '+path)
def main():
    binaries,scripts,out=sys.argv[1:]
    os.mkdir(out);payload=os.path.join(out,'payload');os.mkdir(payload)
    app=os.path.join(binaries,'PtolemyAgent.app')
    macho(os.path.join(app,'Contents/MacOS/PtolemyAgent'));macho(os.path.join(app,'Contents/Resources/ptolemy-agent-worker'))
    shutil.copytree(app,os.path.join(payload,'Ptolemy Agent.app'))
    entries=[]
    for parent,dirs,files in os.walk(payload):
        for name in dirs+files:
            if os.path.islink(os.path.join(parent,name)):raise RuntimeError('Symlink in build output')
        for name in files:
            path=os.path.join(parent,name);rel=path[len(payload)+1:]
            mode=0644
            if '/MacOS/' in rel or rel.endswith('/ptolemy-agent-worker'):mode=0755
            os.chmod(path,mode);f=open(path,'rb')
            try:checksum=sha.new(f.read()).hexdigest()
            finally:f.close()
            entries.append('%s %04o %s\n'%(checksum,mode,rel))
    entries.sort();f=open(os.path.join(out,'manifest.sha1'),'wb');f.write(''.join(entries));f.close()
    for name in ['install.py','install.command']:shutil.copyfile(os.path.join(scripts,name),os.path.join(out,name));os.chmod(os.path.join(out,name),0755)
if __name__=='__main__':main()
