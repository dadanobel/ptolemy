#!/usr/bin/env python3
import importlib.util, pathlib, tempfile

p = pathlib.Path(__file__).resolve().parents[1] / 'tools/page_diff.py'
s = importlib.util.spec_from_file_location('page_diff', p)
m = importlib.util.module_from_spec(s)
s.loader.exec_module(m)
with tempfile.TemporaryDirectory() as t:
    a = pathlib.Path(t) / 'a'
    b = pathlib.Path(t) / 'b'
    pages = [(i.to_bytes(4, 'big') + bytes(range(252))) * 16 for i in range(1, 5)]
    original = b''.join(pages)
    a.write_bytes(original)
    b.write_bytes(original)
    assert m.compare(a, b)['different_pages'] == 0
    b.write_bytes(pages[0] + pages[3] + pages[2] + pages[1])
    r = m.compare(a, b)
    assert r['different_pages'] == 2 and r['different_pages_matching_reference_elsewhere'] == 2
    b.write_bytes(b''.join(original[i : i + 4][::-1] for i in range(0, len(original), 4)))
    assert m.compare(a, b)['word32_swapped_pages'] == 4
    b.write_bytes(bytes(len(original)))
    assert m.compare(a, b)['zero_pages'] == 4
    b.write_bytes(original[:-1])
    assert m.compare(a, b)['different_pages'] == 1
    b.write_bytes(original + b'x')
    assert m.compare(a, b)['different_pages'] == 1
    assert a.read_bytes() == original
print('PASS page identity tool: aliases, byte swaps, zeros, truncation, extension and read-only input')
