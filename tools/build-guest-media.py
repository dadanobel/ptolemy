#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Ptolemy Tools medium (HFS+ disk image) from an already built Ptolemy Agent.app (--binaries), or a local
development medium that builds it inside Tiger (--devkit). A release medium never includes Apple software.
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import tarfile
import tempfile


def add_devkit(tf, devkit, wrapper):
    sdk_library = devkit / 'Developer/SDKs/MacOSX10.4.0.sdk/usr/lib/libgcc_s.1.dylib'
    if not sdk_library.is_file():
        raise ValueError('Devkit lacks the PPC SDK libgcc_s.1.dylib')
    manifest = None
    marker = devkit / 'toolchain-manifest.json'
    if marker.is_file():
        document = json.loads(marker.read_text())
        document['files_sha256']['bin/ppc-cc'] = hashlib.sha256(wrapper.read_bytes()).hexdigest()
        document['media_normalizations'] = ['current packet ppc-cc', 'SDK libgcc_s symlink', 'archive mtime for .a']
        manifest = (json.dumps(document, indent=2) + '\n').encode()

    def normalize(info):
        rel = info.name.removeprefix('./')
        if rel == 'bin/ppc-cc':
            return None  # use this packet's accepted GCC-4061 wrapper below
        if rel == 'toolchain-manifest.json' and manifest is not None:
            return None
        if info.isfile() and rel.endswith('.a'):
            info.mtime = 946684800
        if info.issym() and rel == 'usr/lib/gcc/powerpc-apple-darwin8/4.0.0/libgcc_s.dylib':
            info.linkname = os.path.relpath(sdk_library, (devkit / rel).parent)
        return info

    tf.add(devkit, arcname='.', filter=normalize)
    tf.add(wrapper, arcname='./bin/ppc-cc')
    if manifest is not None:
        info = tarfile.TarInfo('./toolchain-manifest.json')
        info.size = len(manifest)
        info.mode = 0o644
        tf.addfile(info, io.BytesIO(manifest))


def macho(path):
    b = path.read_bytes()[:12]
    if len(b) < 12 or struct.unpack('>II', b[:8]) != (0xFEEDFACE, 18):
        raise ValueError('Expected a real 32-bit big-endian PPC Mach-O: ' + str(path))


def antiochus(payload, app, stone, sums):
    """Astra's Antiochus.app and 'Rosetta Stone.app' into payload/Antiochus, every file as her SHA256SUMS lists it: the
    benchmark is compared between betas, so it stays the binary she built and is never rebuilt here. Packaging only:
    the second bundle's folder and its CFBundleName/CFBundleDisplayName become rosettast. Its executable keeps the name
    Rosetta: the program chooses its mode by that name. MANIFEST.txt records what is shipped."""
    listed = {}
    for line in sums.read_text().splitlines():
        digest, _, name = line.partition('  ')
        listed[name] = digest
    home = payload / 'Antiochus'
    home.mkdir()
    kept = []
    for src, name in ((app, 'Antiochus.app'), (stone, 'rosettast.app')):
        if src is None:
            continue
        for path in sorted(src.rglob('*')):
            if path.is_symlink():
                raise ValueError('Symlink in ' + str(src))
            if not path.is_file() or path.name == '.DS_Store':
                continue
            rel = path.relative_to(src).as_posix()
            data = path.read_bytes()
            key = f'{src.parent.name}/{src.name}/{rel}'
            if listed.get(key) != hashlib.sha256(data).hexdigest():
                raise ValueError("Not the file of Astra's SHA256SUMS: " + key)
            kept.append((listed[key], hashlib.sha1(data).hexdigest(), f'{name}/{rel}'))
        shutil.copytree(src, home / name, ignore=shutil.ignore_patterns('.DS_Store'))
    macho(home / 'Antiochus.app/Contents/MacOS/Antiochus')
    macho(home / 'Antiochus.app/Contents/Resources/antiochus-worker')
    text = [
        'Antiochus in Ptolemy Tools: the programs as Astra built them.',
        (home / 'Antiochus.app/Contents/Resources/build-manifest.txt').read_text().splitlines()[0],
        "Astra's SHA256SUMS: sha256 " + hashlib.sha256(sums.read_bytes()).hexdigest(),
    ]
    changed = []
    if stone:
        macho(home / 'rosettast.app/Contents/MacOS/Rosetta')
        plist = home / 'rosettast.app/Contents/Info.plist'
        info = plistlib.loads(plist.read_bytes())
        if info.get('CFBundleExecutable') != 'Rosetta':
            raise ValueError('rosettast: the executable is not Rosetta, the name its mode depends on')
        info['CFBundleName'] = info['CFBundleDisplayName'] = 'rosettast'
        plist.write_bytes(plistlib.dumps(info))
        changed = [
            'Packaging changes (no code, no binary):',
            'rosettast.app: the bundle folder, renamed from "Rosetta Stone.app"',
            'rosettast.app/Contents/Info.plist: CFBundleName and CFBundleDisplayName "Rosetta Stone" -> "rosettast"; sha256 '
            + hashlib.sha256(plist.read_bytes()).hexdigest(),
        ]
        kept = [k for k in kept if k[2] != 'rosettast.app/Contents/Info.plist']
    for name in ('Antiochus.app', 'rosettast.app'):
        if (home / name).is_dir():
            info = plistlib.loads((home / name / 'Contents/Info.plist').read_bytes())
            text.append(f"{name} {info['CFBundleShortVersionString']}")
    text.append('Unchanged files (SHA-256, SHA-1, path):')
    text += [' '.join(k) for k in kept]
    (home / 'MANIFEST.txt').write_text('\n'.join(text + changed) + '\n')


def main():
    root = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument('--binaries', type=Path)
    source.add_argument(
        '--devkit', type=Path, help='LOCAL ONLY: extracted user-supplied Apple devkit; build automatically inside Tiger'
    )
    p.add_argument('--antiochus', type=Path, help="Astra's built Antiochus.app, as delivered; needs --antiochus-sums")
    p.add_argument('--rosettast', type=Path, help="Astra's built 'Rosetta Stone.app'; installed beside Antiochus as rosettast.app")
    p.add_argument('--antiochus-sums', type=Path, help="Astra's SHA256SUMS: every file of both applications must match it")
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    if a.out.suffix.lower() != '.dmg':
        p.error('--out must end in .dmg (flat UDRW HFS+ medium)')
    if a.out.exists():
        p.error('outputs must be new paths')
    a.out.parent.mkdir(parents=True, exist_ok=True)
    names = ['PtolemyAgent.app']
    if a.binaries:
        for name in names:
            src = a.binaries / name
            macho(src / 'Contents/MacOS/PtolemyAgent')
            macho(src / 'Contents/Resources/ptolemy-agent-worker')
    elif not (a.devkit / 'bin/ppc-cc').is_file():
        p.error('--devkit must be an extracted devkit with bin/ppc-cc')
    if a.rosettast and not a.antiochus:
        p.error('--rosettast is installed as a part of Antiochus: give --antiochus too')
    if a.antiochus and not a.antiochus_sums:
        p.error("--antiochus needs --antiochus-sums, Astra's SHA256SUMS")
    with tempfile.TemporaryDirectory(prefix='ptolemy-tools-') as tmp:
        media = Path(tmp) / 'media'
        media.mkdir()
        payload = media / 'payload'
        payload.mkdir()
        for name in names if a.binaries else []:
            src = a.binaries / name
            rel = 'Ptolemy Agent.app'
            dest = payload / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            if src.is_dir():
                shutil.copytree(src, dest, symlinks=True)
            else:
                shutil.copyfile(src, dest)
        if a.antiochus:
            antiochus(payload, a.antiochus, a.rosettast, a.antiochus_sums)
        # Antiochus's icon (supplied by the owner, unedited) for the agent and the medium.
        icon = root / 'assets/tools-icon.icns'
        agent = payload / 'Ptolemy Agent.app/Contents'
        if agent.is_dir():
            shutil.copyfile(icon, agent / 'Resources/PtolemyAgent.icns')
            info = plistlib.loads((agent / 'Info.plist').read_bytes())
            info['CFBundleIconFile'] = 'PtolemyAgent'
            (agent / 'Info.plist').write_bytes(plistlib.dumps(info))
        shutil.copyfile(icon, media / '.VolumeIcon.icns')
        entries = []
        for path in sorted(payload.rglob('*')):
            if path.is_symlink():
                raise ValueError('Symlink not allowed in guest payload: ' + str(path))
            if not path.is_file():
                continue
            rel = path.relative_to(payload).as_posix()
            executable = '/MacOS/' in rel or rel.endswith(('/ptolemy-agent-worker', '/antiochus-worker'))
            mode = 0o755 if executable else 0o644
            path.chmod(mode)
            entries.append(f'{hashlib.sha1(path.read_bytes()).hexdigest()} {mode:04o} {rel}\n')
        (media / 'manifest.sha1').write_text(''.join(entries))
        for name in ['install.py', 'install.command', 'uninstall.command']:
            shutil.copyfile(root / 'guest/tools' / name, media / name)
            (media / name).chmod(0o755)
        if a.devkit:
            # Apple content stays on this user's local development medium.
            # Source/release archives NEVER contain this tarball.
            with tarfile.open(media / 'devkit.tar.gz', 'w:gz', format=tarfile.GNU_FORMAT) as tf:
                add_devkit(tf, a.devkit, root / 'guest/toolchain/ppc-cc')
            with tarfile.open(media / 'source.tar.gz', 'w:gz', format=tarfile.GNU_FORMAT) as tf:
                tf.add(root / 'guest', arcname='guest')
                tf.add(root / 'include', arcname='include')
                for name in ('diagnostics.c', 'diagnostics.h', 'guest/agent_builtin.h'):
                    tf.add(root / 'src/optics' / name, arcname='src/optics/' + name)
            shutil.copyfile(root / 'guest/tools/develop.command', media / 'install.command')
            (media / 'install.command').chmod(0o755)
            (media / 'LOCAL-ONLY.txt').write_text(
                'Contains the user-supplied Apple compiler/SDK. Do not upload or redistribute.\nRelease builds use --binaries instead.\n'
            )
        (media / '.metadata_never_index').touch()
        note = (
            'LOCAL development medium: contains your Apple compiler/SDK; do not upload.\n'
            if a.devkit
            else 'Release medium: Ptolemy programs only, no Apple binaries.\n'
        )
        (media / 'README.txt').write_text(
            'Ptolemy Tools\n'
            + note
            + 'Open install.command after the first login in Tiger or Leopard. No administrator password is needed.\nThe agent starts on; Stop stays visible in its menu.\nuninstall.command removes what install.command installed.\n'
        )
        subprocess.run(
            [
                'hdiutil',
                'create',
                '-srcfolder',
                str(media),
                '-volname',
                'ptolemytools',
                '-fs',
                'HFS+',
                '-layout',
                'SPUD',
                '-format',
                'UDRW',
                str(a.out),
            ],
            check=True,
        )
        # The volume shows .VolumeIcon.icns when its root folder has the custom-icon Finder flag (0x0400).
        mount = Path(tmp) / 'mount'
        mount.mkdir()
        subprocess.run(['hdiutil', 'attach', '-nobrowse', '-noverify', '-mountpoint', str(mount), str(a.out)], check=True)
        try:
            subprocess.run(['xattr', '-wx', 'com.apple.FinderInfo', '0' * 16 + '0400' + '0' * 44, str(mount)], check=True)
        finally:
            subprocess.run(['hdiutil', 'detach', str(mount)], check=True)
    print('Created the Ptolemy Tools medium.')


if __name__ == '__main__':
    main()
