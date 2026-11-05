#!/usr/bin/env python3
"""Sampler controls without a guest, real processes, sleeps, or existing outputs."""

from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
from unittest.mock import patch

source = Path(__file__).resolve().parents[1] / 'tools/classic-soak.py'
spec = importlib.util.spec_from_file_location('classic_soak', source)
sampler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sampler)


def run(output, changed=False, guest_status=0):
    identities = 0
    commands = []

    def capture(argv, timeout=15):
        nonlocal identities
        commands.append(argv)
        if argv[-1] == 'lstart=,comm=':
            identities += 1
            return {
                'status': 0,
                'stdout': ('changed' if changed and identities == 3 else 'start') + ' ptolemy-engine',
                'stderr': '',
            }
        if '--socket' in argv:
            return {'status': guest_status, 'stdout': 'guest metrics', 'stderr': ''}
        return {'status': 0, 'stdout': 'host metrics', 'stderr': ''}

    argv = [
        'classic-soak.py',
        '--pid',
        '123',
        '--socket',
        '/unused.sock',
        '--output',
        str(output),
        '--samples',
        '2',
        '--interval',
        '15',
    ]
    with patch.object(sampler, 'capture', capture), patch('sys.argv', argv), patch.object(
        sampler.time, 'sleep'
    ), redirect_stdout(io.StringIO()):
        sampler.main()
    assert len([c for c in commands if '--socket' in c]) == 2
    assert all('kill' not in c and 'reboot' not in c for c in commands)


with tempfile.TemporaryDirectory(prefix='pt-classic-sampler-') as tmp:
    output = Path(tmp) / 'samples.jsonl'
    run(output, guest_status='timeout')
    before = output.read_bytes()
    rows = [json.loads(s) for s in before.splitlines()]
    assert [r['sample'] for r in rows] == [0, 1]
    assert all(r['guest']['status'] == 'timeout' for r in rows)
    try:
        run(output)
    except FileExistsError:
        pass
    else:
        raise AssertionError('existing evidence was replaced')
    assert output.read_bytes() == before
    changed = Path(tmp) / 'changed.jsonl'
    try:
        run(changed, changed=True)
    except SystemExit as e:
        assert str(e) == 'Engine stopped or PID changed; samples retained'
    else:
        raise AssertionError('PID reuse was not detected')
    assert len(changed.read_text().splitlines()) == 1
with patch.object(sampler.subprocess, 'run', side_effect=subprocess.TimeoutExpired('probe', 1)):
    assert sampler.capture(['probe'], 1) == {'status': 'timeout'}
print(
    'PASS Classic sampler: timeout retained, PID reuse refused, existing evidence preserved; no guest/process actions'
)
