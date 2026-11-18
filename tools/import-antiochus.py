#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Copies the sources of Antiochus and rosettast from one of Astra's deliveries into guest/antiochus, unchanged and
checked against her SHA256SUMS: sources, Classic sources, resources, her build scripts, tests, PROTOCOL.md. Her built
programs, validation logs and the Apple SDK she builds with stay out of the repository; the programs reach Ptolemy
Tools through tools/build-guest-media.py --antiochus.
usage: import-antiochus.py DELIVERY [DESTINATION]"""

import hashlib
from pathlib import Path
import shutil
import sys

KEEP = ('LICENSE', 'README.md', 'build-tiger.sh', 'build-classic.sh', 'compare.py', 'docs/PROTOCOL.md')
FOLDERS = ('src/', 'classic/', 'resources/', 'tests/')


def main():
    delivery = Path(sys.argv[1])
    dest = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / 'guest/antiochus'
    if dest.exists():
        sys.exit(f'{dest} exists: move it away first, so that a file the new delivery dropped does not stay')
    sums = delivery / 'SHA256SUMS'
    listed = dict(line.split('  ', 1)[::-1] for line in sums.read_text().splitlines())
    names = sorted(n for n in listed if n in KEEP or n.startswith(FOLDERS))
    bare = []
    for name in names:
        data = (delivery / name).read_bytes()
        if hashlib.sha256(data).hexdigest() != listed[name]:
            sys.exit("Not the file of Astra's SHA256SUMS: " + name)
        (dest / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(delivery / name, dest / name)
        shutil.copymode(delivery / name, dest / name)
        if name != 'LICENSE' and b'SPDX-License-Identifier' not in data[:400]:
            bare.append(name)
    # the delivery's own name for itself: the first line of the build manifest in her built application
    version = (delivery / 'build/Antiochus.app/Contents/Resources/build-manifest.txt').read_text().splitlines()[0]
    (dest / 'SOURCE.txt').write_text(
        f'{version}, by Astra: sources as delivered, {len(names)} files, none edited here.\n'
        f"Her SHA256SUMS: sha256 {hashlib.sha256(sums.read_bytes()).hexdigest()}\n"
        'Built by her inside Tiger with Apple GCC 4.0 and the 10.4 SDK; the programs in Ptolemy Tools are those builds.\n\n'
        'SPDX-License-Identifier: GPL-2.0-or-later\n'
        'The line above also covers the files that carry no such line of their own:\n' + ''.join(f'  {n}\n' for n in bare))
    print(f'Imported {version}: {len(names)} files into {dest}')


if __name__ == '__main__':
    main()
