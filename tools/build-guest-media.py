#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Ptolemy Tools medium (HFS+ disk image) from an already built Ptolemy Agent.app (--binaries), or a local
development medium that builds it inside Tiger (--devkit). A release medium never includes Apple software.
--script creates an optional visible Finder bootstrap for --input-script.
Use --at only after Setup Assistant is completed and Finder is ready, US layout.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile
import tempfile

def add_devkit(tf,devkit,wrapper):
    sdk_library=devkit/'Developer/SDKs/MacOSX10.4.0.sdk/usr/lib/libgcc_s.1.dylib'
    if not sdk_library.is_file():raise ValueError('Devkit lacks the PPC SDK libgcc_s.1.dylib')
    manifest=None;marker=devkit/'toolchain-manifest.json'
    if marker.is_file():
        document=json.loads(marker.read_text())
        document['files_sha256']['bin/ppc-cc']=hashlib.sha256(wrapper.read_bytes()).hexdigest()
        document['media_normalizations']=['current packet ppc-cc','SDK libgcc_s symlink','archive mtime for .a']
        manifest=(json.dumps(document,indent=2)+'\n').encode()
    def normalize(info):
        rel=info.name.removeprefix('./')
        if rel=='bin/ppc-cc':return None # use this packet's accepted GCC-4061 wrapper below
        if rel=='toolchain-manifest.json' and manifest is not None:return None
        if info.isfile() and rel.endswith('.a'):info.mtime=946684800
        if info.issym() and rel=='usr/lib/gcc/powerpc-apple-darwin8/4.0.0/libgcc_s.dylib':
            info.linkname=os.path.relpath(sdk_library,(devkit/rel).parent)
        return info
    tf.add(devkit,arcname='.',filter=normalize)
    tf.add(wrapper,arcname='./bin/ppc-cc')
    if manifest is not None:
        info=tarfile.TarInfo('./toolchain-manifest.json');info.size=len(manifest);info.mode=0o644
        tf.addfile(info,io.BytesIO(manifest))

def macho(path):
    b=path.read_bytes()[:12]
    if len(b)<12 or struct.unpack('>II',b[:8])!=(0xfeedface,18):
        raise ValueError('Expected a real 32-bit big-endian PPC Mach-O: '+str(path))

def bootstrap(at):
    t=at;lines=['# Finder ready, US keyboard. Installs only our user-mode helpers.']
    def key(usage,pressed):
        nonlocal t
        lines.append(f'{t:.2f} key {usage} {pressed}');t+=0.12
    def tap(usage):key(usage,1);key(usage,0)
    key(227,1);tap(17);key(227,0);t+=3 # Finder new window (Cmd+N)
    key(227,1);key(225,1);tap(10);key(225,0);key(227,0);t+=2
    for ch in '/Volumes/ptolemytools':
        shift=ch.isupper()
        if shift:key(225,1)
        tap(56 if ch=='/' else 4+ord(ch.lower())-ord('a'))
        if shift:key(225,0)
    tap(40);t+=3 # Go to volume, then type-select install.command, Cmd+O.
    for ch in 'install.command':tap(55 if ch=='.' else 4+ord(ch)-ord('a'))
    t+=0.5;key(227,1);tap(18);key(227,0)
    return '\n'.join(lines)+'\n'

def main():
    root=Path(__file__).resolve().parents[1]
    p=argparse.ArgumentParser(description=__doc__);source=p.add_mutually_exclusive_group(required=True)
    source.add_argument('--binaries',type=Path);source.add_argument('--devkit',type=Path,help='LOCAL ONLY: extracted user-supplied Apple devkit; build automatically inside Tiger')
    p.add_argument('--out',type=Path,required=True);p.add_argument('--script',type=Path)
    p.add_argument('--at',type=float,default=45);a=p.parse_args()
    if not 0<=a.at<=3500:p.error('--at must be 0..3500 seconds')
    if a.out.suffix.lower()!='.dmg':p.error('--out must end in .dmg (flat UDRW HFS+ medium)')
    if a.out.exists() or (a.script and a.script.exists()):p.error('outputs must be new paths')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    names=['PtolemyAgent.app']
    if a.binaries:
        for name in names:
            src=a.binaries/name
            macho(src/'Contents/MacOS/PtolemyAgent');macho(src/'Contents/Resources/ptolemy-agent-worker')
    elif not (a.devkit/'bin/ppc-cc').is_file():p.error('--devkit must be an extracted devkit with bin/ppc-cc')
    with tempfile.TemporaryDirectory(prefix='ptolemy-tools-') as tmp:
        media=Path(tmp)/'media';media.mkdir();payload=media/'payload';payload.mkdir()
        for name in names if a.binaries else []:
            src=a.binaries/name
            rel='Ptolemy Agent.app'
            dest=payload/rel
            dest.parent.mkdir(parents=True,exist_ok=True)
            if src.is_dir():shutil.copytree(src,dest,symlinks=True)
            else:shutil.copyfile(src,dest)
        entries=[]
        for path in sorted(payload.rglob('*')):
            if path.is_symlink():raise ValueError('Symlink not allowed in guest payload: '+str(path))
            if not path.is_file():continue
            rel=path.relative_to(payload).as_posix()
            executable='/MacOS/' in rel or rel.endswith('/ptolemy-agent-worker')
            mode=0o755 if executable else 0o644;path.chmod(mode)
            entries.append(f'{hashlib.sha1(path.read_bytes()).hexdigest()} {mode:04o} {rel}\n')
        (media/'manifest.sha1').write_text(''.join(entries))
        for name in ['install.py','install.command']:
            shutil.copyfile(root/'guest/tools'/name,media/name);(media/name).chmod(0o755)
        if a.devkit:
            # Apple content stays on this user's local development medium.
            # Source/release archives NEVER contain this tarball.
            with tarfile.open(media/'devkit.tar.gz','w:gz',format=tarfile.GNU_FORMAT) as tf:
                add_devkit(tf,a.devkit,root/'guest/toolchain/ppc-cc')
            with tarfile.open(media/'source.tar.gz','w:gz',format=tarfile.GNU_FORMAT) as tf:
                tf.add(root/'guest',arcname='guest');tf.add(root/'include',arcname='include')
                for name in ('diagnostics.c','diagnostics.h','guest/agent_builtin.h'):
                    tf.add(root/'src/optics'/name,arcname='src/optics/'+name)
            shutil.copyfile(root/'guest/tools/develop.command',media/'install.command');(media/'install.command').chmod(0o755)
            (media/'LOCAL-ONLY.txt').write_text('Contains the user-supplied Apple compiler/SDK. Do not upload or redistribute.\nRelease builds use --binaries instead.\n')
        (media/'.metadata_never_index').touch()
        note='LOCAL development medium: contains your Apple compiler/SDK; do not upload.\n' if a.devkit else 'Release medium: Ptolemy programs only, no Apple binaries.\n'
        (media/'README.txt').write_text('Ptolemy Tools\n'+note+'Open install.command after the first Tiger login. No administrator password is needed.\nThe agent starts on; Stop stays visible in its menu.\n')
        subprocess.run(['hdiutil','create','-srcfolder',str(media),'-volname','ptolemytools','-fs','HFS+','-layout','SPUD','-format','UDRW',str(a.out)],check=True)
    if a.script:a.script.write_text(bootstrap(a.at))
    print('Created the Ptolemy Tools medium.')
if __name__=='__main__':main()
