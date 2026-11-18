#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ptolemy Tools with Antiochus, checked on the Mac (or any host with Python 3), without a guest:
1. tools/build-guest-media.py antiochus(): Astra's programs enter the payload only as her SHA256SUMS lists them; the
   demonstration becomes rosettast.app with its executable's name kept; the shipped record names every binary.
2. guest/tools/install.py: install, install again, update without Antiochus, earlier Tools' links, someone else's
   application or link in our places, and `remove` - in a made-up home folder.
install.py is Python 2.3 for Tiger. It runs here through a textual translation of the few 2.x forms it uses, with
launchctl, open and ps replaced: this checks its logic, not that Tiger's Python accepts its syntax."""

import hashlib
import importlib.util
import io
import os
from pathlib import Path
import plistlib
import re
import shutil
import sys
import tempfile
import types

ROOT = Path(__file__).resolve().parents[1]
MACHO = b'\xfe\xed\xfa\xce\x00\x00\x00\x12' + b'\0' * 24


def media_builder():
    spec = importlib.util.spec_from_file_location('media', ROOT / 'tools/build-guest-media.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def astra_delivery(top):
    """A stand-in for Astra's build/ folder and SHA256SUMS."""
    build = top / 'build'
    files = {
        'Antiochus.app/Contents/MacOS/Antiochus': MACHO + b'antiochus',
        'Antiochus.app/Contents/Resources/antiochus-worker': MACHO + b'worker',
        'Antiochus.app/Contents/Resources/build-manifest.txt': b'Antiochus 1.0-test / antiochus-score-4\nflags\n',
        'Antiochus.app/Contents/Info.plist': plistlib.dumps({'CFBundleExecutable': 'Antiochus', 'CFBundleShortVersionString': '1.0-test'}),
        'Rosetta Stone.app/Contents/MacOS/Rosetta': MACHO + b'worker',
        'Rosetta Stone.app/Contents/Info.plist': plistlib.dumps(
            {'CFBundleExecutable': 'Rosetta', 'CFBundleName': 'Rosetta Stone', 'CFBundleDisplayName': 'Rosetta Stone',
             'CFBundleIdentifier': 'org.antiochus.RosettaStone', 'CFBundleShortVersionString': '1.0-test'}),
    }
    sums = []
    for name, data in files.items():
        path = build / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        sums.append(f'{hashlib.sha256(data).hexdigest()}  build/{name}\n')
    (build / 'Antiochus.app/.DS_Store').write_bytes(b'finder')  # not hers, not shipped
    (top / 'SHA256SUMS').write_text(''.join(sums))
    return build / 'Antiochus.app', build / 'Rosetta Stone.app', top / 'SHA256SUMS'


def check_media(tmp):
    media = media_builder()
    app, stone, sums = astra_delivery(tmp / 'astra')
    payload = tmp / 'payload'
    payload.mkdir()
    media.antiochus(payload, app, stone, sums)
    home = payload / 'Antiochus'
    assert sorted(p.name for p in home.iterdir()) == ['Antiochus.app', 'MANIFEST.txt', 'rosettast.app']
    assert not list(payload.rglob('.DS_Store'))
    for name in ('Antiochus.app/Contents/MacOS/Antiochus', 'Antiochus.app/Contents/Resources/antiochus-worker'):
        assert (home / name).read_bytes() == (app.parent / name).read_bytes()
    assert (home / 'rosettast.app/Contents/MacOS/Rosetta').read_bytes() == (stone / 'Contents/MacOS/Rosetta').read_bytes()
    info = plistlib.loads((home / 'rosettast.app/Contents/Info.plist').read_bytes())
    assert info['CFBundleName'] == info['CFBundleDisplayName'] == 'rosettast' and info['CFBundleExecutable'] == 'Rosetta'
    assert info['CFBundleIdentifier'] == 'org.antiochus.RosettaStone'
    record = (home / 'MANIFEST.txt').read_text()
    worker = MACHO + b'worker'
    assert 'Antiochus 1.0-test / antiochus-score-4' in record and 'rosettast.app 1.0-test' in record
    assert f'{hashlib.sha256(worker).hexdigest()} {hashlib.sha1(worker).hexdigest()} rosettast.app/Contents/MacOS/Rosetta' in record
    assert 'Unchanged' in record and 'rosettast.app/Contents/Info.plist: CFBundleName' in record
    assert ' rosettast.app/Contents/Info.plist\n' not in record  # changed, so not among the unchanged
    print('media: both programs as listed, rosettast packaged, record written')

    def refused(change, what):
        top = tmp / what
        a, s, sums = astra_delivery(top)
        change(a, s, sums)
        out = top / 'payload'
        out.mkdir()
        try:
            media.antiochus(out, a, s, sums)
        except ValueError as e:
            print(f'media: refused, {what}: {e}')
            return
        raise AssertionError('accepted: ' + what)

    refused(lambda a, s, sums: (a / 'Contents/Resources/antiochus-worker').write_bytes(MACHO + b'rebuilt'), 'a rebuilt worker')
    refused(lambda a, s, sums: (s / 'Contents/MacOS/Rosetta').write_bytes(MACHO + b'other'), 'another demonstration binary')
    refused(lambda a, s, sums: (a / 'Contents/Resources/extra').write_bytes(b'x'), 'a file she does not list')
    refused(
        lambda a, s, sums: [
            (s / 'Contents/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable': 'rosettast', 'CFBundleShortVersionString': '1'})),
            sums.write_text(
                sums.read_text()
                + hashlib.sha256((s / 'Contents/Info.plist').read_bytes()).hexdigest()
                + '  build/Rosetta Stone.app/Contents/Info.plist\n'),
        ],
        'a renamed executable',
    )
    return payload


class Installer:
    """guest/tools/install.py as Python 3, in a home folder of its own."""

    def __init__(self, home):
        text = (ROOT / 'guest/tools/install.py').read_text()
        text = re.sub(r'^(\s*)print >>sys\.stderr, (.+)$', r'\1print(\2, file=sys.stderr)', text, flags=re.M)
        text = re.sub(r'^(\s*)print (.+)$', r'\1print(\2)', text, flags=re.M)
        text = re.sub(r'except (\w+), (\w+):', r'except \1 as \2:', text)
        text = re.sub(r'(?<=[ (,])0([0-7]{3})(?=[),])', r'0o\1', text)
        text = text.replace('if os.getuid() == 0:', 'if False:').replace("import os, sys, sha, stat,", "import os, sys, stat,")
        self.home, self.spawned = home, []
        sha = types.SimpleNamespace(new=lambda data='': _Sha(data))
        self.ns = {'__name__': 'installer', 'sha': sha, 'open': _open}
        exec(compile(text, 'install.py', 'exec'), self.ns)
        self.ns['spawn'] = lambda args: self.spawned.append(args) or 0
        self.ns['os'].popen = lambda command: io.StringIO('')  # ps: no agent runs here

    def run(self, media=None, remove=False):
        saved = os.environ.get('HOME'), sys.platform, sys.argv
        os.environ['HOME'], sys.platform, sys.argv = str(self.home), 'darwin', ['install.py'] + (['remove'] if remove else [])
        self.ns['__file__'] = str((media or self.home) / 'install.py')
        try:
            return self.ns['main']()
        finally:
            os.environ['HOME'], sys.platform, sys.argv = saved


class _Sha:
    def __init__(self, data=''):
        self.h = hashlib.sha1(data.encode('latin-1'))

    def update(self, data):
        self.h.update(data.encode('latin-1'))

    def hexdigest(self):
        return self.h.hexdigest()


def _open(path, mode='r'):  # Python 2 strings are bytes: read and write them as Latin-1 text
    return io.open(path, mode.replace('b', ''), encoding='latin-1', newline='')


def medium(top, payload_antiochus, tag):
    """A Tools medium: the agent (its content differs by tag, so the version does) and, if given, payload/Antiochus."""
    payload = top / 'payload'
    files = {'Ptolemy Agent.app/Contents/MacOS/PtolemyAgent': b'agent ' + tag, 'Ptolemy Agent.app/Contents/Info.plist': b'plist'}
    for name, data in files.items():
        (payload / name).parent.mkdir(parents=True, exist_ok=True)
        (payload / name).write_bytes(data)
    if payload_antiochus:
        shutil.copytree(payload_antiochus, payload / 'Antiochus')
    lines = []
    for path in sorted(payload.rglob('*')):
        if path.is_file():
            rel = path.relative_to(payload).as_posix()
            executable = '/MacOS/' in rel or rel.endswith('/antiochus-worker')  # as build-guest-media.py
            lines.append(f"{hashlib.sha1(path.read_bytes()).hexdigest()} {'0755' if executable else '0644'} {rel}\n")
    (top / 'manifest.sha1').write_text(''.join(lines))
    return top


def tree(top):
    out = []
    for path in sorted(top.rglob('*')):
        rel = path.relative_to(top).as_posix()
        out.append(rel + (' -> ' + os.readlink(path) if path.is_symlink() else ''))
    return out


def refused(installer, media, what):
    """The installer stops before it installs anything: ~/Applications is as it was, no version, no agent at login."""
    before = tree(installer.home / 'Applications')
    try:
        installer.run(media)
    except RuntimeError as e:
        after = tree(installer.home)
        assert tree(installer.home / 'Applications') == before, what
        assert not [x for x in after if '/versions/' in x or x.endswith('/current') or 'LaunchAgents/' in x], after
        print(f'install: refused, {what}: {e}')
        return
    raise AssertionError('installed over ' + what)


def check_install(tmp, antiochus_payload):
    with_tools = medium(tmp / 'medium-1', antiochus_payload, b'1')
    agent_only = medium(tmp / 'medium-2', None, b'2')
    home = tmp / 'home'
    home.mkdir()
    it = Installer(home)
    apps, support = home / 'Applications', home / 'Library/Application Support/Ptolemy'
    folder = apps / 'Ptolemy Tools/Antiochus'
    # earlier Tools' links in ~/Applications: one of ours for each program, and someone's own real application elsewhere
    apps.mkdir()
    (apps / 'Antiochus.app').symlink_to(support / 'versions' / ('1' * 20) / 'Antiochus.app')
    (apps / 'Rosetta Stone.app').symlink_to(support / 'versions' / ('1' * 20) / 'Rosetta Stone.app')
    (apps / 'Mine.app').mkdir()
    (apps / 'Mine.app/owner').write_text('keep')
    it.run(with_tools)
    version = os.readlink(support / 'current').split('/')[1]
    for name in ('Antiochus.app', 'rosettast.app'):
        assert os.readlink(folder / name) == str(support / 'versions' / version / 'Antiochus' / name)
    assert (folder / 'rosettast.app/Contents/MacOS/Rosetta').read_bytes().startswith(MACHO)
    assert (folder / 'Antiochus.app/Contents/Resources/antiochus-worker').stat().st_mode & 0o111
    assert not os.path.lexists(apps / 'Antiochus.app') and not os.path.lexists(apps / 'Rosetta Stone.app')
    assert (apps / 'Mine.app/owner').read_text() == 'keep'
    assert it.spawned[-1] == ['/usr/bin/open', str(apps / 'Ptolemy Agent.app')]
    print('install: both programs linked from Ptolemy Tools/Antiochus; earlier links of ours gone; another application kept')
    first = tree(home)
    it.run(with_tools)
    assert tree(home) == first
    print('install again: nothing added, nothing changed')
    it.run(agent_only)
    newer = os.readlink(support / 'current').split('/')[1]
    assert newer != version and os.readlink(folder / 'Antiochus.app').split('/')[-3] == version
    assert (folder / 'rosettast.app/Contents/MacOS/Rosetta').is_file()
    print('update without Antiochus: its links stay at the version that has it')
    # someone else's things in our places
    for what, make in (
        ("someone's application at Antiochus's place", lambda p: (p.mkdir(), (p / 'owner').write_text('keep'))),
        ("someone's link at Antiochus's place", lambda p: p.symlink_to('/tmp/unrelated-app')),
    ):
        other = tmp / ('home-' + str(len(what)))
        (other / 'Applications/Ptolemy Tools/Antiochus').mkdir(parents=True)
        make(other / 'Applications/Ptolemy Tools/Antiochus/Antiochus.app')
        refused(Installer(other), with_tools, what)
    other = tmp / 'home-file'
    (other / 'Applications').mkdir(parents=True)
    (other / 'Applications/Ptolemy Tools').write_text('a file of that name')
    refused(Installer(other), with_tools, 'a file named Ptolemy Tools where the folder goes')
    other = tmp / 'home-old'
    (other / 'Applications/Antiochus.app').mkdir(parents=True)  # a real application under the old link's name
    (other / 'Applications/Rosetta Stone.app').parent.mkdir(exist_ok=True)
    (other / 'Applications/Rosetta Stone.app').symlink_to('/tmp/unrelated-app')
    Installer(other).run(with_tools)
    assert (other / 'Applications/Antiochus.app').is_dir() and os.readlink(other / 'Applications/Rosetta Stone.app') == '/tmp/unrelated-app'
    print("install: someone's Antiochus.app and Rosetta Stone.app in ~/Applications are not ours and stay")
    # removal
    (support / 'agent.log').write_text('commands')
    (apps / 'Ptolemy Tools/Mine.txt').write_text('keep')
    it.spawned.clear()
    it.run(remove=True)
    left = tree(home)
    assert it.spawned == [['/bin/launchctl', 'unload', str(home / 'Library/LaunchAgents/org.ptolemy.experimental.guest-agent.plist')]]
    assert 'Applications/Mine.app/owner' in left and 'Applications/Ptolemy Tools/Mine.txt' in left
    assert 'Library/Application Support/Ptolemy/agent.log' in left
    assert not [x for x in left if 'versions' in x or 'current' in x or 'Antiochus' in x or 'Ptolemy Agent' in x or 'LaunchAgents/' in x], left
    print('remove: links, versions and the LaunchAgent gone; the command log and other files kept')
    it.run(remove=True)
    assert tree(home) == left
    fresh = tmp / 'home-clean'
    fresh.mkdir()
    clean = Installer(fresh)
    clean.run(with_tools)
    clean.run(remove=True)
    assert tree(fresh) == ['Applications', 'Library', 'Library/Application Support', 'Library/LaunchAgents'], tree(fresh)
    print('install and remove in an empty home: only the standard folders remain')
    changed = tmp / 'home-agent'
    changed.mkdir()
    keep = Installer(changed)
    keep.run(with_tools)
    plist = changed / 'Library/LaunchAgents/org.ptolemy.experimental.guest-agent.plist'
    plist.write_text('edited by the user')
    keep.run(remove=True)
    assert plist.read_text() == 'edited by the user'
    print('remove: a LaunchAgent that is not the installer\'s stays')


def main():
    tmp = Path(tempfile.mkdtemp(prefix='ptolemy-tools-check-'))
    try:
        payload = check_media(tmp)
        check_install(tmp, payload / 'Antiochus')
    finally:
        shutil.rmtree(tmp)
    print('PASS')


if __name__ == '__main__':
    main()
