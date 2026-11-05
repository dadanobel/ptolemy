#!/usr/bin/env python3
"""Reject semantic NVRAM faults without modifying the working tree."""

from pathlib import Path
import re
import resource, shutil, subprocess, tempfile

root = Path(__file__).resolve().parent.parent
mutations = [
    (
        'backup failure must stop migration',
        'if((error=nd_nv_backup(s,n->data)))return error;',
        '(void)nd_nv_backup(s,n->data);',
    ),
    ('existing backup must match', 'if(!error&&memcmp(copy,bytes,sizeof copy))error=EEXIST;', ''),
    ('flash snapshot must include the new UUID', 'if(next.flash_on)memcpy(next.flash,next.data,sizeof next.data);', ''),
]


def anchor(text, s):  # the one span of text that s matches, whitespace aside (the sources are clang-formatted)
    hits = re.findall(r'\s*'.join(map(re.escape, ''.join(s.split()))), text)
    return hits[0] if len(hits) == 1 else None


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


for label, before, after in mutations:
    with tempfile.TemporaryDirectory(prefix='pt-nvram-negative-') as tmp:
        tree = Path(tmp)
        shutil.copytree(root / 'include', tree / 'include')
        for f in ('src/platform.c', 'src/nvram.c', 'tests/nvram-default.c'):
            p = tree / f
            p.parent.mkdir(exist_ok=True, parents=True)
            shutil.copy2(root / f, p)
        p = tree / 'src/nvram.c'
        text = p.read_text()
        span = anchor(text, before)
        assert span
        p.write_text(text.replace(span, after))
        compile = subprocess.run(
            [
                'clang',
                '-std=c11',
                '-D_DARWIN_C_SOURCE',
                '-O1',
                '-Iinclude',
                'src/platform.c',
                'src/nvram.c',
                'tests/nvram-default.c',
                '-pthread',
                '-o',
                'probe',
            ],
            cwd=tree,
            capture_output=True,
            text=True,
        )
        assert compile.returncode == 0, compile.stderr
        result = subprocess.run(
            [str(tree / 'probe')], cwd=tree, capture_output=True, text=True, preexec_fn=limits, timeout=30
        )
        assert result.returncode and 'Assertion failed:' in result.stderr, (label, result.stdout, result.stderr)
        print('PASS negative:', label, flush=True)
print('PASS: 3 semantic NVRAM mutations rejected')
