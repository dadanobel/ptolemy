#!/usr/bin/env python3
"""Summarize FP exits in an explicitly marked benchmark log window."""

import argparse, collections, json, re
from pathlib import Path


def summarize(text, start, end):
    active = False
    seen = 0
    complete = 0
    control = collections.Counter()
    sites = collections.Counter()
    other = 0
    samples = 0
    for line in text.splitlines():
        if start in line:
            if active:
                raise ValueError('nested start marker')
            active = True
            seen += 1
            continue
        if end in line:
            if not active:
                raise ValueError('end without start')
            active = False
            complete += 1
            continue
        if not active:
            continue
        if 'FP exits by FPSCR&0xff:' in line:
            samples += 1
            for v, n in re.findall(r'\b([0-9a-f]{2})×(\d+)', line):
                control[int(v, 16)] += int(n)
        m = re.search(r'FP_SITE pc=([0-9a-f]+) op=(\d+/\d+) control=([0-9a-f]+) count=(\d+)', line)
        if m:
            sites[m[1], m[2], m[3]] += int(m[4])
        m = re.search(r'FP_SITE other=(\d+)', line)
        if m:
            other += int(m[1])
    if active or not seen or seen != complete:
        raise ValueError('missing or incomplete benchmark window')
    if not samples:
        raise ValueError('no FP snapshots in the window')
    rn = [sum(n for v, n in control.items() if v & 3 == r) for r in range(4)]
    return dict(
        windows=complete,
        snapshots=samples,
        exits=sum(rn),
        rn=rn,
        enabled_exceptions=sum(n for v, n in control.items() if v & 0xF8),
        sites=[dict(pc=p, op=o, control=c, count=n) for (p, o, c), n in sites.most_common()],
        other_sites=other,
        limitation='Exit counts are not time shares; snapshots may straddle marker boundaries.',
    )


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('log', type=Path)
    p.add_argument('--start', required=True)
    p.add_argument('--end', required=True)
    a = p.parse_args()
    if not a.start or not a.end or a.start == a.end:
        p.error('distinct nonempty markers required')
    try:
        print(json.dumps(summarize(a.log.read_text(errors='replace'), a.start, a.end), indent=2))
    except ValueError as e:
        p.error(str(e))
