#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Unpack user-supplied Xcode 2 C tools/SDK. Never run installer scripts.

The result runs IN Tiger PPC, not on the Apple Silicon host. Apple binaries
and headers are local build products, not part of this source distribution.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile

SDK = 'Developer/SDKs/MacOSX10.4.0.sdk'
TOOLS = {'ld', 'ar', 'nm', 'ranlib', 'libtool', 'otool', 'strip'}


def take(f, n):
    b = f.read(n)
    if len(b) != n:
        raise ValueError('truncated odc cpio archive')
    return b


def members(f):
    """Apple's Archive.pax.gz is POSIX odc CPIO (070707), NOT a tarball."""
    while True:
        h = take(f, 76)
        if h[:6] != b'070707':
            raise ValueError('expected 070707 odc cpio header')
        dev, ino, mode, uid, gid, nlink, rdev = (int(h[i:i+6], 8) for i in range(6, 48, 6))
        nname, size = int(h[59:65], 8), int(h[65:76], 8)
        if not 1 <= nname <= 4096 or size > 512*1024*1024:
            raise ValueError('oversized cpio member')
        raw = take(f, nname)
        if raw[-1:] != b'\0' or b'\0' in raw[:-1]:
            raise ValueError('invalid cpio name')
        name = raw[:-1].decode('utf-8')
        if name == 'TRAILER!!!':
            if size:
                raise ValueError('invalid cpio trailer')
            return
        p = PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts:
            raise ValueError('unsafe cpio path: ' + name)
        yield str(p), mode, (dev, ino), nlink, take(f, size)


def wanted(pkg, name):
    if pkg == 'gcc4.0':
        return name.startswith(('usr/bin/', 'usr/lib/', 'usr/libexec/', 'usr/include/'))
    if pkg == 'DeveloperTools':
        return name in {'usr/bin/'+n for n in TOOLS} or name == 'usr/libexec/gcc/darwin/ppc/as'
    return name == SDK or name.startswith(SDK + '/')


def extract(archive, pkg, root):
    # No symlink is installed until all regular files have been extracted.
    # In particular an archive cannot redirect subsequent writes via a symlink.
    links, hardlinks, pending = {}, {}, []
    with gzip.open(archive, 'rb') as f:
        for name, mode, inode, nlink, data in members(f):
            selected = wanted(pkg, name)
            kind = stat.S_IFMT(mode)
            if kind == stat.S_IFREG and nlink > 1 and data:
                hardlinks[inode] = data
            if not selected:
                continue
            out = root / name
            if kind == stat.S_IFDIR:
                out.mkdir(parents=True, exist_ok=True)
            elif kind == stat.S_IFLNK:
                links[name] = data.decode('utf-8')
            elif kind == stat.S_IFREG:
                if not data and nlink > 1:
                    pending.append((name, mode, inode))
                    continue
                write_file(out, data, mode)
            else:
                raise ValueError('special file in selected payload: ' + name)
        for name, mode, inode in pending:
            # These Apple payloads report nlink=8 even on an isolated empty
            # file (install-tools/macro_list). No data-bearing peer means empty.
            write_file(root / name, hardlinks.get(inode, b''), mode)
    for name, target in links.items():
        if not target or '\0' in target:
            raise ValueError('invalid symlink: ' + name)
        out = root / name
        if target.startswith('/'):
            # An SDK absolute link denotes the SDK's /, never the host's /.
            anchor = root / SDK if name.startswith(SDK + '/') else root
            target = os.path.relpath(anchor / target.lstrip('/'), out.parent)
        resolved = (out.parent / target).resolve()
        if not resolved.is_relative_to(root.resolve()):
            raise ValueError('symlink escapes toolchain: ' + name)
        out.parent.mkdir(parents=True, exist_ok=True)
        if out.exists() or out.is_symlink():
            raise ValueError('duplicate selected file: ' + name)
        out.symlink_to(target)


def write_file(path, data, mode):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as f:
        f.write(data)
    path.chmod(0o755 if mode & 0o111 else 0o644)


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024*1024), b''):
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('xcode_tools', type=Path)
    ap.add_argument('output', type=Path, help='NEW directory; never merged into an existing tree')
    ap.add_argument('--tar', type=Path, help='NEW uncompressed guest-readable tar file')
    a = ap.parse_args()
    packages = a.xcode_tools / 'Packages'
    archives = {n: packages/(n+'.pkg/Contents/Archive.pax.gz')
                for n in ('gcc4.0', 'DeveloperTools', 'MacOSX10.4')}
    for p in archives.values():
        if not p.is_file():
            ap.error('missing package payload: ' + str(p))
    if a.tar and a.tar.exists():
        ap.error('--tar output already exists')
    a.output.mkdir(parents=True, exist_ok=False)
    root = a.output.resolve()
    # On failure preserve the incomplete NEW tree for diagnosis; no user files
    # are removed. The manifest is the successful extraction marker.
    sources = {}
    for n, p in archives.items():
        print('Extracting ' + n, flush=True)
        sources[n] = {'archive': str(p.resolve()), 'sha256': sha(p)}
        extract(p, n, root)
    bindir = root / 'bin'
    bindir.mkdir()
    shutil.copyfile(Path(__file__).resolve().parents[1]/'guest/toolchain/ppc-cc', bindir/'ppc-cc')
    (bindir/'ppc-cc').chmod(0o755)
    # GCC must use the actual PPC assembler, not Apple's /usr/bin/as dispatcher
    # whose search for /usr/libexec/... is not relocatable.
    (bindir/'as').symlink_to('../usr/libexec/gcc/darwin/ppc/as')
    for n in TOOLS:
        (bindir/n).symlink_to('../usr/bin/'+n)
    required = ['usr/bin/powerpc-apple-darwin8-gcc-4.0.0',
                'usr/libexec/gcc/powerpc-apple-darwin8/4.0.0/cc1',
                'usr/libexec/gcc/powerpc-apple-darwin8/4.0.0/collect2',
                'usr/lib/gcc/powerpc-apple-darwin8/4.0.0/crt2.o',
                SDK+'/usr/lib/libSystem.dylib', SDK+'/usr/lib/bundle1.o']
    for rel in required:
        if not (root/rel).is_file():
            raise ValueError('missing required toolchain file: ' + rel)
    files = {str(p.relative_to(root)): sha(p) for p in sorted(root.rglob('*'))
             if p.is_file() and not p.is_symlink()}
    manifest = {'format': 1, 'runs_on': 'Darwin 8 PPC32', 'sdk': 'MacOSX10.4.0.sdk',
                'source_archives': sources, 'files_sha256': files}
    (root/'toolchain-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    if a.tar:
        # GNU long-link records are understood by Tiger tar; SDK framework
        # symlinks can exceed ustar's 100-byte linkname limit.
        with a.tar.open('xb') as f, tarfile.open(fileobj=f, mode='w', format=tarfile.GNU_FORMAT) as t:
            t.add(root, arcname=root.name, recursive=True)
    print('Ready: '+str(root/'bin/ppc-cc'))


if __name__ == '__main__':
    main()
