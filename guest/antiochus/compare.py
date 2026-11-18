#!/usr/bin/env python3
"""Compare completed Antiochus reports. Does not infer statistical significance."""
import argparse
import math
import plistlib
from pathlib import Path


def read(folder):
    with (folder / 'summary.plist').open('rb') as stream:
        summary = plistlib.load(stream)
    with (folder / 'environment.plist').open('rb') as stream:
        environment = plistlib.load(stream)
    if not summary.get('complete'):
        raise ValueError(str(folder) + ': incomplete run')
    rows = summary.get('cases', [])
    ids = [row.get('id') for row in rows]
    expected = {'antiochus-score-2': 25, 'antiochus-score-3': 29, 'antiochus-score-4': 34}.get(summary.get('profile'))
    if expected is None or len(ids) != expected or len(set(ids)) != expected or None in ids:
        raise ValueError(str(folder) + ': missing or duplicate cases')
    for key in (('cpu', 'gpu', 'total', 'combined') if summary.get('profile') == 'antiochus-score-4' else ('cpu', 'gpu', 'total')):
        number = summary.get(key)
        if not isinstance(number, (int, float)) or not math.isfinite(number) or number < 0:
            raise ValueError(str(folder) + ': invalid score')
    return summary, environment, {row['id']: row for row in rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--allow-capability-change', action='store_true')
    args = parser.parse_args()
    try:
        a, ea, ra = read(args.baseline)
        b, eb, rb = read(args.candidate)
        if a.get('profile') != b.get('profile') or a.get('version') != b.get('version') or ra.keys() != rb.keys():
            raise ValueError('Different workload/profile versions cannot be ranked')
        changed = [key for key in ra if (ra[key].get('status') == 'unsupported') != (rb[key].get('status') == 'unsupported')]
        if changed and not args.allow_capability_change:
            raise ValueError('Capability coverage changed: ' + ', '.join(changed))
        print('Baseline OS:', ea.get('os', 'unknown'))
        print('Candidate OS:', eb.get('os', 'unknown'))
        if changed:
            print('UNRANKED: capability change explicitly allowed:', ', '.join(changed))
        print('Check build-manifest.txt: compiler and kernel changes require a new controlled baseline.')
        for key in ('cpu', 'gpu', 'total'):
            ratio = 'n/a' if a[key] == 0 else '%+.2f%%' % (100 * (b[key] / a[key] - 1))
            print('%-7s %12.3f -> %12.3f  %s' % (key, a[key], b[key], ratio))
        if a.get('profile') in ('antiochus-score-3', 'antiochus-score-4'):
            print('GPU checks:', str(a['gpu_passed']) + '/' + str(a['gpu_checked']), '->',
                  str(b['gpu_passed']) + '/' + str(b['gpu_checked']))
            core = ('gpu-fill', 'gpu-texture', 'gpu-depth-blend', 'gpu-geometry', 'gpu-upload', 'gpu-scene', 'gpu-packed')
            if a.get('profile') == 'antiochus-score-4':
                core += ('gpu-rosetta', 'gpu-paneion', 'gpu-phalanx', 'gpu-cavalry')
                print('Combined CPU + GPU:', a['combined'], '->', b['combined'])
            if all(bool(ra[k].get('accepted')) == bool(rb[k].get('accepted')) for k in core) and not changed:
                print('GPU speed on the SAME passing core subset:', a['gpu_speed'], '->', b['gpu_speed'])
            else:
                print('GPU speed UNRANKED: passing core subset or capabilities differ')
        print('\nPer-case time change (negative is faster; window cases are busy time/frame):')
        for key, old in ra.items():
            new = rb[key]
            if old.get('accepted') and new.get('accepted') and key != 'cpu-state':
                t0, t1 = old['accepted_seconds'], new['accepted_seconds']
                if not (math.isfinite(t0) and math.isfinite(t1) and t0 > 0 and t1 > 0):
                    raise ValueError('Invalid timing: ' + key)
                print('%-23s %+.2f%%' % (key, 100 * (t1 / t0 - 1)))
            else:
                print('%-23s %s -> %s' % (key, old.get('outcome', old.get('status')), new.get('outcome', new.get('status'))))
        print('\nSingle-run comparison; five batches are not five independent launches. Use A/A and repeated ABBA.')
    except (OSError, ValueError, KeyError, TypeError, plistlib.InvalidFileException) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
