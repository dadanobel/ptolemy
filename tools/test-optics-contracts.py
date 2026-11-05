#!/usr/bin/env python3
"""Permanent, bounded native contracts. No guest boot, images or benchmarks."""

import os, subprocess
from pathlib import Path

s = Path(__file__).resolve().parents[1]
src = s / 'src/optics'
lib = s / 'build/optics/liboptics.a'
assert lib.is_file(), lib
b = s / 'build/contracts'
b.mkdir(parents=True, exist_ok=True)
env = dict(
    os.environ,
    OPTICS_EXACT_PITCH='0',
    OPTICS_DEPTH_CAPACITY='0',
    OPTICS_DEPTH_IO='0',
    OPTICS_2D_CONTRACT='0',
    OPTICS_BITBLT_MULTI='1',
    OPTICS_RENDER_OWNER='0',
    OPTICS_PASS_REASONS='0',
    OPTICS_DIRECT_VRAM='0',
    OPTICS_RAW_VERTICES='0',
    OPTICS_RAW_STREAMS='0',
    OPTICS_VAP_NORM2='0',
    OPTICS_GPU_VTE='0',
    OPTICS_STRICT='0',
    PT_PIC_ACTIVITY='0',
    MTL_DEBUG_LAYER='1',
    ASAN_OPTIONS='halt_on_error=1',
    UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
)
env.setdefault(
    'SDKROOT', subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
)  # the toolchain's clang, called directly, finds no SDK by itself (Command Line Tools)
cc = subprocess.check_output(['xcrun', '--find', 'clang'], text=True).strip()
flags = [
    cc,
    '-arch',
    'arm64',
    '-O2',
    '-g',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-D_DARWIN_C_SOURCE',
    '-I' + str(src),
    '-I' + str(src / 'foundation'),
    '-iquote' + str(s / 'include'),
]
negatives = 0


def run(cmd, extra=None, negative=False):
    global negatives
    e = env.copy()
    e.update(extra or {})
    x = subprocess.run(list(map(str, cmd)), env=e, capture_output=True, text=True, timeout=120)
    if negative:
        assert x.returncode == -6 and 'Assertion failed:' in x.stderr, x.stdout + x.stderr
        negatives += 1
    else:
        assert x.returncode == 0, x.stdout + x.stderr


def test(name, extra=None, negative=None, args=(), cpp=False):
    exe = b / name
    f = flags + (['-std=c++17', '-fobjc-arc'] if cpp else ['-std=c11'])
    run(
        f
        + [
            s / ('tests/' + name + ('.mm' if cpp else '.c')),
            b / 'platform.o',
            lib,
            '-lc++',
            '-framework',
            'Metal',
            '-framework',
            'Foundation',
            '-o',
            exe,
        ]
    )
    run([exe, *args], extra)
    if negative is not None:
        run([exe, *args], negative, True)
    return exe


run(flags + ['-std=c11', '-c', s / 'src/platform.c', '-o', b / 'platform.o'])
test('optics-color-pitch', {'OPTICS_EXACT_PITCH': '1'}, {'OPTICS_EXACT_PITCH': '0'})
test('optics-draw-cleanup')
test('optics-draw-decode')
test('optics-2d-contract', {'OPTICS_2D_CONTRACT': '1'}, {'OPTICS_2D_CONTRACT': '0'})
test('optics-blit-accounting')
test('optics-display-mode')
test('optics-cpu-view', {'OPTICS_DIRECT_VRAM': '1'}, {'OPTICS_DIRECT_VRAM': '0'})
test('optics-bitblt-multi', {'OPTICS_BITBLT_MULTI': '1'}, {'OPTICS_BITBLT_MULTI': '0'})
test('optics-host-data')
test('optics-byte-matrix')
test(
    'optics-owner-memory-policy',
    {'OPTICS_DIRECT_VRAM': '1', 'OPTICS_RENDER_OWNER': '1'},
    {'OPTICS_DIRECT_VRAM': '1', 'OPTICS_RENDER_OWNER': '0'},
)
test('optics-depth-metal', {'OPTICS_DEPTH_IO': '1'})  # the product default: its tiled-ZB check needs depth I/O on
test('optics-mip-levels')
test('optics-dxt')
test('optics-colour-formats')
test('optics-lines-blend')
test('optics-pvs-math', cpp=True)
test('optics-depth-growth', {'OPTICS_DEPTH_CAPACITY': '1'}, {'OPTICS_DEPTH_CAPACITY': '0'})
test('optics-depth-io', {'OPTICS_DEPTH_IO': '2'}, {'OPTICS_DEPTH_IO': '0'})
# The complete depth contract also runs with FIFO Metal ownership enabled; then the product's on-demand export.
run([b / 'optics-depth-io'], {'OPTICS_DEPTH_IO': '2', 'OPTICS_RENDER_OWNER': '1'})
run([b / 'optics-depth-io'], {'OPTICS_DEPTH_IO': '1'})
exe = test(
    'optics-render-owner',
    {'OPTICS_RENDER_OWNER': '1', 'OPTICS_PASS_REASONS': '1'},
    {'OPTICS_RENDER_OWNER': '0'},
    cpp=True,
)
run([exe], {'OPTICS_RENDER_OWNER': '1', 'OPTICS_PASS_REASONS': '0'}, True)
exe = test('optics-raw-vertices', {'OPTICS_RAW_VERTICES': '1'}, {'OPTICS_RAW_VERTICES': '0'})
run([exe, 'normalized'], {'OPTICS_RAW_VERTICES': '0'})
exe = test('optics-raw-vertices-metal')
run([exe], {'OPTICS_RENDER_OWNER': '1'})
test('openpic-contract', {'PT_PIC_ACTIVITY': '1'}, {'PT_PIC_ACTIVITY': '0'})
run(['python3', s / 'tests/fp-range-oracle.py'])
run(['python3', s / 'tools/check_VAP.py'])
print('PASS: permanent native Optics/CPU contracts;', negatives, 'assertion-based negative controls')
