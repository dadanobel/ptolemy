#!/usr/bin/env python3
"""Reject an unmarked profile instead of treating desktop samples as a benchmark."""

from pathlib import Path
import runpy

summarize = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/fp-window.py'))['summarize']
sample = (
    'FP exits by FPSCR&0xff: 01×999\nBEGIN\n'
    'FP exits by FPSCR&0xff: 00×3 01×7 02×5 03×2 09×11\n'
    'FP_SITE pc=ffff8364 op=63/20 control=01 count=7\n'
    'FP_SITE other=21\nEND\nFP exits by FPSCR&0xff: 01×999\n'
)
result = summarize(sample, 'BEGIN', 'END')
assert result['exits'] == 28 and result['rn'] == [3, 18, 5, 2]
assert result['enabled_exceptions'] == 11 and result['other_sites'] == 21
assert result['sites'][0]['pc'] == 'ffff8364'
assert summarize(sample * 2, 'BEGIN', 'END')['exits'] == 56
for text in ('', 'BEGIN\n', 'END\n', 'BEGIN\nBEGIN\nEND\n', 'BEGIN\nEND\n'):
    try:
        summarize(text, 'BEGIN', 'END')
    except ValueError:
        continue
    raise AssertionError('an invalid profiling window was accepted')
print('PASS FP windows: marked aggregation; missing/nested/empty windows refused')
