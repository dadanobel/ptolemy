#!/usr/bin/env python3
# Names the functions of a PT_PROF session profile (ptolemy-profile.txt): for each sampled pc, the last symbol of the
# image's file at or below vmaddr + offset (nm -n). The guest's files stay on this Mac.
# usage: prof-sym.py PROFILE ROOT [IMAGE=FILE ...]
#   ROOT: a directory holding the guest's files under their guest paths (ROOT/usr/lib/libSystem.B.dylib);
#   IMAGE=FILE: the file of image number IMAGE (executables and bundles carry no path in their header).
import bisect, collections, os, subprocess, sys

def symbols(path):
    nm = os.environ.get('NM', 'nm')
    for args in ([nm, '-n', '-arch', 'ppc', path], [nm, '-n', path]):
        r = subprocess.run(args, capture_output=True, text=True)
        rows = [l.split() for l in r.stdout.splitlines()]
        rows = sorted((int(x[0], 16), x[-1]) for x in rows if len(x) >= 3 and x[1] in 'tTsS')
        if rows:
            return rows
    return []

def main():
    profile, root, files = sys.argv[1], sys.argv[2], dict(a.split('=', 1) for a in sys.argv[3:])
    images, table, total = {}, None, {}
    for line in open(profile):
        f = line.rstrip('\n').split('\t')
        if f[0] in ('session', 'stalls'):
            table = collections.Counter()
            total[f[0]] = (int(f[1].split()[0]), table)
        elif f[0] == 'image' and f[1] not in images:
            path = files.get(f[1]) or (os.path.join(root, f[8].lstrip('/')) if f[8].startswith('/') else None)
            images[f[1]] = (f[8], int(f[5], 16), symbols(path) if path and os.path.exists(path) else [])
        elif f[0] == 'pc':
            name, vmaddr, syms = images[f[1]]
            k = bisect.bisect_right(syms, (vmaddr + int(f[4], 16), '\x7f')) - 1
            table[(name, syms[k][1] if k >= 0 else '(no symbols) offset ' + f[4][:-3] + 'xxx')] += int(f[2])
    for part, (ticks, table) in total.items():
        print(f'{part}: {ticks} ticks')
        for (image, function), n in table.most_common(40):
            print(f'  {100 * n / max(ticks, 1):5.1f}%  {function}  [{image}]')

main()
