#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the boot firmware/Apple PCI driver address contract, without a guest."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/openbios/arch/ppc/qemu/init.c').read_text()


def check(text, model):
    block = text.split('[' + model + '] = {', 1)[1].split('\n    },', 1)[0]
    field = lambda name: int(re.search(r'\.' + name + r' = (0x[0-9a-f]+)', block)[1], 16)
    io = re.search(r'\.type = IO_SPACE[^\n]*\.childaddr = (0x[0-9a-f]+)', block)
    base = int(io[1], 16)
    # AppleMacRiscPCI::start resolves the OF I/O range, then adds these offsets.
    # https://github.com/apple-oss-distributions/AppleMacRiscPCI/blob/main/AppleMacRiscPCI.cpp
    assert field('cfg_addr') == base + 0x00800000, model + ': config index'
    assert field('cfg_data') == base + 0x00C00000, model + ': config data'
    assert field('io_base') == base, model + ': port aperture'


for model in ('ARCH_MAC99', 'ARCH_MAC99_U3'):
    check(source, model)
prefix, g5 = source.split('[ARCH_MAC99_U3] = {', 1)
old = prefix + '[ARCH_MAC99_U3] = {' + g5.replace('.childaddr = 0xf0000000', '.childaddr = 0xf2000000', 1)
assert old != source
try:
    check(old, 'ARCH_MAC99_U3')
except AssertionError as error:
    assert 'config index' in str(error)
else:
    raise AssertionError('old mismatched G5 I/O range escaped the check')
print('PASS: G4/G5 firmware PCI ports match the Apple driver; old G5 range rejected')
