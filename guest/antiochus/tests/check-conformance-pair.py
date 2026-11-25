#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit a collected run or compare two runs of one configuration."""
import hashlib
import sys
from pathlib import Path
from conformance_reference import check_reference


def rows(path):
    return [line.split('\t', 1) for line in path.read_text().splitlines()]


def inspect(root, ordinal, case, required_revision=None):
    folder = root / case
    events = rows(folder / 'result.tsv')
    values = {}
    for key, value in events:
        values.setdefault(key, []).append(value)
    if values.get('id') != [case]:
        raise ValueError('missing or incorrect worker id')
    shader = case.startswith('sr-')
    revision = values.get('sr_protocol' if shader else 'cf_protocol')
    if revision not in (['1'], ['2']):
        raise ValueError('unknown conformance protocol')
    if shader and revision != ['1']:
        raise ValueError('unknown shader protocol')
    if not shader and required_revision and revision != [required_revision]:
        raise ValueError('worker revision disagrees with manifest')
    markers = [v.split() for v in values.get('cf_marker', [])]
    if not markers or markers[0][:3] != ['begin', ordinal, case] or len(markers[0]) != 4:
        raise ValueError('missing begin marker')
    start = int(markers[0][3])
    if (folder / 'host.tsv').is_file():
        host = dict(rows(folder / 'host.tsv'))
        if host.get('status') not in ('timeout', 'crash', 'incomplete') or not host.get('reason'):
            raise ValueError('invalid host verdict')
        if len(markers) != 1:
            raise ValueError('host termination conflicts with end marker')
        return (revision, host['status'], values.get('side', [])[-1:], values.get('probe', [])[-1:], values.get('step', [])[-1:])
    if not shader and revision == ['2']:
        if 'accelerated' in values.get('side', []) and not (folder / 'reference.rgba').is_file():
            raise ValueError('missing completed software reference')
        for name, probe in [('reference','primary'), ('same-fbo-reference','same_fbo_rewrite'),
                            ('post-read-reference','post_read_progress')]:
            path = folder / (name + '.rgba')
            if path.exists():
                check_reference(case, path.read_bytes(), probe)
    status = values['status'][-1]
    if (len(markers) != 2 or markers[1][:3] != ['end', ordinal, case] or
            len(markers[1]) != 4 or int(markers[1][3]) < start):
        raise ValueError('invalid end marker')
    if status not in ('pass', 'fail', 'unsupported') or (status != 'pass' and not values.get('reason')):
        raise ValueError('missing worker verdict or reason')
    if not float(values['seconds'][-1]) >= 0:
        raise ValueError('invalid elapsed time')
    probes = values.get('probe', [])
    if not shader and status == 'pass':
        expected = ['primary', 'post_read_progress'] if case.startswith('cf-fbo-') else ['primary']
        if case.startswith('cf-fbo-') and revision == ['2']:
            expected.insert(1, 'same_fbo_rewrite')
        if probes != expected or len(values.get('gl_renderer', [])) != 2:
            raise ValueError('pass without both renderers or required image probes')
    tolerance = 0 if shader else int(values['tolerance_rgba8'][0])
    width, height = (int(values[k][0]) for k in ('width', 'height'))
    hashes = {}
    for raw in sorted(folder.glob('*.rgba')):
        data = raw.read_bytes()
        if len(data) != 4 * width * height or not raw.with_suffix('.ppm').is_file():
            raise ValueError('incomplete image ' + raw.name)
        hashes[raw.name] = hashlib.sha256(data).hexdigest()
    if shader:
        sides = values.get('side_status', [])
        if values.get('side') != ['reference','accelerated'] or len(sides)!=2:
            raise ValueError('missing shader side verdicts')
        expected = 'fail' if 'fail' in sides else 'unsupported' if 'unsupported' in sides else 'pass'
        if expected != status or any(v not in ('pass','fail','unsupported') for v in sides):
            raise ValueError('inconsistent shader verdicts')
        for name in values.get('stage_file', []) + values.get('driver_log', []):
            if Path(name).name != name or not (folder/name).is_file():
                raise ValueError('missing stage or driver log')
        defined = ('side_status','warning_count','bad_pixels','max_channel_error_normalized','source_line',
                   'first_x','first_y','actual_channel','expected_channel','link_ok','compile_ok','source_sha256')
        return status,values.get('reason', []),[values.get(k, []) for k in defined],hashes
    for i, probe in enumerate(probes):
        prefix = {'primary': '', 'post_read_progress': 'post-read-', 'same_fbo_rewrite': 'same-fbo-'}[probe]
        # An API or write failure can interrupt a probe before pixel comparison.
        if i >= len(values.get('bad_pixels', [])) and status == 'fail':
            continue
        a, b = [(folder / (prefix + name + '.rgba')).read_bytes() for name in ('actual', 'reference')]
        delta = [max(abs(x-y) for x, y in zip(a[j:j+4], b[j:j+4])) for j in range(0, len(a), 4)]
        bad = [j for j, d in enumerate(delta) if d > tolerance]
        if (len(bad) != int(values['bad_pixels'][i]) or max(delta) != int(values['max_channel_error'][i]) or
                (status == 'pass' and bad)):
            raise ValueError('raw RGBA disagrees with ' + probe)
        if bad:
            j = bad[0]
            rgba = lambda image: ','.join(map(str, image[4*j:4*j+4]))
            if (int(values['first_x'][-1]), int(values['first_y'][-1]),
                values['actual_rgba'][-1], values['expected_rgba'][-1]) != (j % width, j // width, rgba(a), rgba(b)):
                raise ValueError('incorrect first mismatch')
        if not (folder / (prefix + 'difference.ppm')).is_file():
            raise ValueError('missing difference ' + probe)
    return revision, status, values.get('reason', []), hashes


def main(manifest, first, second=None):
    cases = [line.split('\t') for line in Path(manifest).read_text().splitlines()]
    first_ordinal = 101 if cases and len(cases[0])==5 and cases[0][1].startswith('sr-') else 1
    if not cases or any(len(c) != 5 or c[0] != str(i) for i, c in enumerate(cases, first_ordinal)):
        raise ValueError('malformed or empty manifest')
    if len({c[1] for c in cases}) != len(cases):
        raise ValueError('duplicate case id')
    required_revision = '2' if any(c[1] == 'cf-wrap-clamp-linear' for c in cases) else None
    issues = 0
    for ordinal, case, *_ in cases:
        try:
            verdict = inspect(Path(first), ordinal, case, required_revision)
            if second is not None and verdict != inspect(Path(second), ordinal, case, required_revision):
                raise ValueError('different verdict, failure point, reason or pixels')
        except (OSError, ValueError, KeyError, IndexError) as err:
            print(case, err, sep='\t')
            issues += 1
    mode = 'paired runs' if second is not None else 'single run'
    print(f'{mode}: {len(cases)} cases, {issues} evidence or reference issues')
    return bool(issues)


if __name__ == '__main__':
    if len(sys.argv) not in (3,4):
        sys.exit('usage: check-conformance-pair.py MANIFEST RUN1 [RUN2]')
    sys.exit(main(*sys.argv[1:]))
