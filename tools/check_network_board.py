#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the exact product PCI/BAR/I/O functions without HVF or a guest."""

from pathlib import Path
import re
import subprocess

r = Path(__file__).resolve().parents[1]
o = r / 'build/network'
o.mkdir(parents=True, exist_ok=True)
s = (r / 'src/firmware.c').read_text()
parts = []
for name in [
    'pci_init',
    'relocate_bar',
    'pci_access',
    'radeon_configure',
    'isa_access',
    'network_access',
    'add_network_regions',
    'add_pci_io_region',
]:
    m = re.search(r'^static [^\n]*\b' + name + r'\([^;\n]*\) ?\{', s, re.M)
    assert m, name
    end = s.index('{', m.start()) + 1
    depth = 1
    start = m.start()
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    parts.append(s[start:end])
(o / 'network-board-functions.inc').write_text('\n'.join(parts) + '\n')
command = [
    'clang',
    '-std=c11',
    '-D_DARWIN_C_SOURCE',
    '-O1',
    '-g',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-fsanitize=address,undefined',
    '-I' + str(r / 'include'),
    '-I' + str(r / 'src'),
    '-I' + str(o),
    str(r / 'tests/net-board.c'),
    str(r / 'src/net_rtl.c'),
    str(r / 'src/platform.c'),
    str(r / 'src/board.c'),
    str(r / 'src/nvram.c'),
    '-pthread',
    '-o',
    str(o / 'test-board'),
]
subprocess.run(command, check=True)
subprocess.run([str(o / 'test-board')], check=True)

# Mutate the exact old failure: let network BARs overwrite Radeon config.
path = o / 'network-board-functions.inc'
saved = path.read_text()
assert 'if (bars[i].slot == 14)' in saved
try:
    path.write_text(saved.replace('if (bars[i].slot == 14)', 'if (1)'))
    bad = command.copy()
    bad[-1] = str(o / 'test-board-bad')
    subprocess.run(bad, check=True)
    result = subprocess.run([bad[-1]], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode and '0x90000008' in result.stdout, result.stdout
    print('PASS: negative control rejects network BAR corruption of Radeon configuration')
finally:
    path.write_text(saved)

# A split physical I/O window must preserve its original PCI port offset.
try:
    assert '(void *)0x513' in saved
    path.write_text(saved.replace('(void *)0x513', 'NULL'))
    bad = command.copy()
    bad[-1] = str(o / 'test-board-bad-port')
    subprocess.run(bad, check=True)
    result = subprocess.run([bad[-1]], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode and 'last_port==0x40' in ''.join(result.stdout.split()), result.stdout
    print('PASS: negative control rejects a shifted G5 PCI port aperture')
finally:
    path.write_text(saved)
