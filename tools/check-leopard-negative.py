#!/usr/bin/env python3
"""Assertion-based mutations of the closeout contracts; never modify the input tree."""

from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def anchor(text, s):  # the one span of text that s matches, whitespace aside (the sources are clang-formatted)
    hits = re.findall(r'\s*'.join(map(re.escape, ''.join(s.split()))), text)
    return hits[0] if len(hits) == 1 else None


def squash(s):  # assert() prints the expression as formatted: compare without whitespace
    return ''.join(s.split())


def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(cmd, cwd):
    return subprocess.run(
        cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120, preexec_fn=no_core
    )


def check(tree, name, edits, command, message):
    saved = {}
    try:
        for path, before, after in edits:
            p = tree / path
            text = p.read_text()
            saved.setdefault(p, text)
            span = anchor(text, before)
            if not span:
                raise AssertionError(f'{name}: mutation anchor is not unique: {path}')
            p.write_text(text.replace(span, after))
        result = run(['clang', *command, '-o', 'test-mutant'], tree)
        if result.returncode:
            raise AssertionError(f'{name}: compile failure is not a negative control\n{result.stdout}')
        result = run([str(tree / 'test-mutant')], tree)
        if not result.returncode or squash(message) not in squash(result.stdout):
            raise AssertionError(f'{name}: intended assertion did not reject mutation\n{result.stdout}')
        print(f'PASS negative: {name}', flush=True)
    finally:
        for p, text in saved.items():
            p.write_text(text)


def main():
    with tempfile.TemporaryDirectory(prefix='pt-closeout-negative-') as tmp:
        tree = Path(tmp)
        for directory in ('epicycle', 'include'):
            shutil.copytree(ROOT / directory, tree / directory, ignore=shutil.ignore_patterns('build', '__pycache__'))
        for path in (
            'src/nvram.c',
            'src/platform.c',
            'guest/agent/sleep_guard.h',
            'tests/jit-test.h',
            'tests/fp-directed-jit.c',
            'tests/fp-moves-jit.c',
            'tests/mmu-segment-key.c',
            'tests/nvram-flash.c',
            'tests/guest-sleep-guard.c',
        ):
            dest = tree / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / path, dest)
        fp = [
            '-std=gnu11',
            '-D_DARWIN_C_SOURCE',
            '-O2',
            '-Iepicycle',
            'tests/fp-directed-jit.c',
            'epicycle/interp.c',
            'epicycle/altivec.c',
            'epicycle/mmu.c',
            '-lm',
        ]
        check(
            tree,
            'PPC RN=1 must map to ARM toward-zero',
            [('epicycle/jit.c', 'movw(T2, 3u << 22);', 'movw(T2, 2u << 22);')],
            fp,
            'directed mismatch',
        )
        check(
            tree,
            'FPCR must be restored at the block boundary',
            [('epicycle/jit.c', 'E(0xD51B4400u | ZR);', 'E(0xD503201Fu);')],
            fp,
            'fpcr==0',
        )
        check(
            tree,
            'directed FR depends on sign and direction',
            [('epicycle/jit.c', '2u << 10 | T2 << 5);', '3u << 10 | T2 << 5);')],
            fp,
            'directed mismatch',
        )
        moves = [s.replace('fp-directed-jit.c', 'fp-moves-jit.c') for s in fp]
        check(
            tree,
            'fneg must preserve the cached source when destination is uncached',
            [('epicycle/jit.c', '(xo == 72 ? src : 1);', 'src;')],
            moves,
            'cached_bits',
        )
        check(
            tree,
            'same SR in different effective segments is not one MMU set',
            [
                ('epicycle/hvf.c', '(srv ^ seg << 24) *', 'srv *'),
                ('epicycle/hvf.c', 'st[i].srv == srv && st[i].seg == seg', 'st[i].srv == srv'),
            ],
            [
                '-O1',
                '-Wl,-dead_strip',
                '-ffunction-sections',
                '-fdata-sections',
                '-framework',
                'Hypervisor',
                'tests/mmu-segment-key.c',
            ],
            'store(1,6)!=store(1,1)',
        )
        nv = [
            '-std=c11',
            '-D_DARWIN_C_SOURCE',
            '-O1',
            '-Iinclude',
            'src/platform.c',
            'src/nvram.c',
            'tests/nvram-flash.c',
            '-pthread',
        ]
        check(
            tree,
            'Core99 must consume the erase confirmation, not store it as data',
            [('src/nvram.c', 'if(v==0xd0 && (at>>13)', 'if(v==0xd1 && (at>>13)')],
            nv,
            'rd(n,at)==0x80',
        )
        check(
            tree,
            'an erased or partial bank must not replace the committed one',
            [
                (
                    'src/nvram.c',
                    'if(nd_nv_valid(b) && (!nd_nv_valid(old)||pt_read_be(b+20,4)>pt_read_be(old+20,4)))',
                    'if((nd_nv_valid(b)||(b[0]==0x5a && b[1]==0xff)) && (!nd_nv_valid(old)||pt_read_be(b+20,4)>pt_read_be(old+20,4)))',
                )
            ],
            nv,
            'memcmp(n.data+8192,old,8192)',
        )
        check(
            tree,
            'failed assertion release must keep ownership',
            [('guest/agent/sleep_guard.h', 'if(e)return e;g->active=0;', 'if(e){g->active=0;return e;}g->active=0;')],
            ['-O2', 'tests/guest-sleep-guard.c', '-framework', 'CoreFoundation'],
            'g.active',
        )
    print('PASS: 8 assertion-based closeout negative controls; input tree unchanged')


if __name__ == '__main__':
    main()
