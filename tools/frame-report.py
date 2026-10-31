#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Per-frame times of a Doom 3 timedemo. Input: Doom's own log with com_speeds 1 (one line per frame:
'frame:N all:MS gfr:MS rf:MS bk:MS', the guest clock), or a CGL probe CSV (ptolemy-cgl-v1; Doom itself never
reaches the probe). The demo is the last FRAMES frames. Prints one line of JSON.
Usage: frame-report.py FILE FRAMES | --self-test"""
import csv, io, json, re, sys

def frames_from(text):
    if text.startswith('# ptolemy-cgl-v1'):
        head, _, rest = text.partition('\n')
        f = dict(kv.split('=') for kv in head.split()[2:])
        rows = list(csv.DictReader(io.StringIO(rest)))
        ctx = max({r['context'] for r in rows}, key=lambda c: sum(r['context'] == c for r in rows))
        t = [int(r['begin_ticks']) * int(f['numer']) / int(f['denom']) / 1e6 for r in rows if r['context'] == ctx]
        return [b - a for a, b in zip(t, t[1:])], None
    m = re.findall(r'frame:\s*(\d+)\s+all:\s*(\d+)\s+gfr:\s*(\d+)\s+rf:\s*(\d+)\s+bk:\s*(\d+)', text)
    return [float(x[1]) for x in m], [(float(x[2]), float(x[3]), float(x[4])) for x in m]

def report(text, frames):
    iv, parts = frames_from(text)
    iv = iv[-frames:]
    parts = parts[-frames:] if parts else None
    off, acc = [], 0.0
    for x in iv:
        acc += x; off.append(acc)                     # ms from the demo start to this frame's end
    s = sorted(iv)
    q = lambda p: s[min(len(s) - 1, int(p * len(s)))]
    over = lambda v, lim: sum(x > lim for x in v)
    first = [x for x, o in zip(iv, off) if o <= 10000]
    worst = sorted(range(len(iv)), key=lambda i: -iv[i])[:10]
    r = {'frames': len(iv), 'total_s': round(sum(iv) / 1e3, 1), 'mean_ms': round(sum(iv) / len(iv), 1),
         'p50': round(q(.5), 1), 'p95': round(q(.95), 1), 'p99': round(q(.99), 1), 'max': round(s[-1], 1),
         'over_50': over(iv, 50), 'over_100': over(iv, 100), 'over_250': over(iv, 250), 'over_500': over(iv, 500),
         'over_1000': over(iv, 1000), 'time_in_over_100_s': round(sum(x for x in iv if x > 100) / 1e3, 1),
         'first10s': {'frames': len(first), 'over_100': over(first, 100), 'over_500': over(first, 500),
                      'max': round(max(first), 1) if first else 0},
         'worst': [[round(off[i] / 1e3, 2), round(iv[i], 1)] + (list(parts[i]) if parts else []) for i in worst]}
    return r

if __name__ == '__main__':
    if sys.argv[1:] == ['--self-test']:
        log = 'junk\n' + ''.join('frame:%d all:%d gfr:  1 rf:  2 bk:  3\n' % (i, 600 if i == 5 else 10) for i in range(10))
        r = report(log, 9)
        assert r['frames'] == 9 and r['max'] == 600 and r['over_500'] == 1 and r['p50'] == 10, r
        assert r['worst'][0][:2] == [0.64, 600.0] and r['worst'][0][2:] == [1.0, 2.0, 3.0], r
        print('frame-report self-test PASS')
    else:
        print(json.dumps(report(open(sys.argv[1], errors='replace').read(), int(sys.argv[2]))))
