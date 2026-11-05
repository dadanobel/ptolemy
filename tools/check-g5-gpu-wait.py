#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the actual board ownership function with deterministic GPU/clock stubs."""

from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'build/g5-gpu-wait'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/firmware.c').read_text()
start = source.index('static int radeon_ram970(')
body = source[start : source.index('\n}\n', start) + 3]
inc = out / 'g5-gpu-wait.inc'
inc.write_text(body)
command = [
    'clang',
    '-std=c11',
    '-O1',
    '-g',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-fsanitize=address,undefined',
    '-I' + str(out),
    str(root / 'tests/g5-gpu-wait.c'),
    '-o',
    str(out / 'test'),
]
subprocess.run(command, check=True)
subprocess.run([command[-1]], check=True)
try:
    # Restore the old unnecessary timer read; the fast-path contract must detect it.
    inc.write_text(body.replace('int e = optics_', '(void)now_ns(); int e = optics_', 1))
    subprocess.run(command, check=True)
    bad = subprocess.run([command[-1]], capture_output=True, text=True)
    assert bad.returncode and 'Assertion failed:' in bad.stderr, bad.stderr
    print('PASS negative: eager clock read rejected')
finally:
    inc.write_text(body)
    subprocess.run(command, check=True)
