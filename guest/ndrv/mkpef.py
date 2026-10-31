#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds the display NDRV (display.c) into a PEF container and writes src/display_ndrv.inc, which
# the engine compiles in (the Mac's compilers have no PowerPC target). Needs clang, ld.lld and llvm-objcopy with the
# PowerPC target. Usage: mkpef.py [--pef FILE]  (FILE: also write the raw container, for tests)
import os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'src', 'display_ndrv.inc')
CFLAGS = ['--target=powerpc-unknown-linux-gnu', '-mcpu=750', '-O2', '-ffreestanding', '-fno-builtin', '-fno-jump-tables',
          '-msoft-float', '-mno-altivec', '-fno-pic', '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
          '-fno-unwind-tables', '-Wall', '-Wextra', '-Werror']
DATE = 0xE706EB80          # 2026-10-28 in Mac OS seconds since 1904 (IONDRV refuses containers before March 2001)
IMAGE_END = 384 + 64 * 64 * 4 + 8 + 256 * 4 + 4 + 256 * 4 + 780  # struct Data in display.c (cursor, mode, depth,
#                                                               colour table, base, gamma, GammaTbl)


def code_bytes(tmp):
    obj, elf, bin_ = (os.path.join(tmp, n) for n in ('display.o', 'display.elf', 'text.bin'))
    script = os.path.join(tmp, 'ndrv.ld')
    with open(script, 'w') as f:
        f.write('SECTIONS { .text 0 : { *(.text.entry) *(.text*) } '
                '/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.llvm_addrsig) } }\n')
    subprocess.run(['clang', *CFLAGS, '-c', os.path.join(HERE, 'display.c'), '-o', obj], check=True)
    subprocess.run(['ld.lld', '-T', script, '-e', '0', '-o', elf, obj], check=True)
    # Position independence: no data, literals or absolute relocations may survive, only .text.
    sections = subprocess.run(['llvm-objdump', '-h', elf], check=True, capture_output=True, text=True).stdout
    alloc = [l.split()[1] for l in sections.splitlines() if l.strip()[:1].isdigit() and len(l.split()) > 4]
    if alloc != ['.text']:
        sys.exit(f'mkpef: sections other than .text: {alloc}')
    subprocess.run(['llvm-objcopy', '-O', 'binary', '--only-section=.text', elf, bin_], check=True)
    return open(bin_, 'rb').read()


def pascal(s, size):
    return bytes([len(s)]) + s.encode() + bytes(size - 1 - len(s))


def data_bytes():
    desc = (b'mtej' + struct.pack('>I', 0) +                       # DriverDescription, version 0
            pascal('Ptolemy,Display', 32) + bytes([1, 0, 0x80, 0]) +  # driverType: name, 1.0.0 final
            struct.pack('>I', 0) + pascal('.Display_Ptolemy', 32) + bytes(32) +   # runtime options, name
            struct.pack('>I', 1) + b'ndrv' + b'vido' + bytes([1, 0, 0x80, 0]))   # one service: video
    assert len(desc) == 128
    names = b''.join(n.encode().ljust(12, b'\0') for n in ('address', 'linebytes', 'width', 'height', 'depth'))
    # HardwareCursorDescriptor 1.0: 64x64, 32 bits, no colours; special encodings transparent (0) and inverting.
    hw = struct.pack('>HH7I', 1, 0, 64, 64, 32, 0, 0, 0, 0) + struct.pack('>I', 5) + \
        struct.pack('>16I', 0, 0x00FFFFFF, *([0] * 14))
    data = bytes(8) + desc + bytes(8) + names + hw
    assert len(data) == 304
    return data


def name_hash(name):
    h = 0
    for c in name.encode():
        h = ((h << 1) - (h >> 16)) ^ c          # PseudoRotate on a signed 32-bit value
        h = (h + 2**31) % 2**32 - 2**31
    return (len(name) << 16) | ((h ^ (h >> 16)) & 0xFFFF)


def loader():
    libs = [('VideoServicesLib', ['VSLPrepareCursorForHardwareCursor']), ('NameRegistryLib', ['RegistryPropertyGet'])]
    exports = [('DoDriverIO', 2, 0), ('TheDriverDescription', 1, 8)]   # name, class (TVector, data), data offset
    strings = b''
    def add(s):
        nonlocal strings
        at = len(strings); strings += s.encode() + b'\0'; return at
    lib_table, sym_table, first = b'', b'', 0
    for lib, syms in libs:
        lib_table += struct.pack('>5IBBH', add(lib), 0, 0, len(syms), first, 0, 0, 0)
        for s in syms:
            sym_table += struct.pack('>I', 2 << 24 | add(s))           # TVector import
        first += len(syms)
    export_names = [add(n) for n, _, _ in exports]
    # Data section relocations: the TVector at 0 (code, data), then 128 bytes on the two imports at 136.
    relocs = struct.pack('>3H', 0x23 << 9 | 0, 0x8000 | (128 - 1), 0x25 << 9 | 1)
    header_size, nsym = 56, sum(len(s) for _, s in libs)
    reloc_headers = struct.pack('>hhII', 1, 0, len(relocs) // 2, 0)
    reloc_at = header_size + len(lib_table) + len(sym_table) + len(reloc_headers)
    strings_at = reloc_at + len(relocs)
    hash_at = (strings_at + len(strings) + 3) & ~3
    header = struct.pack('>iIiIiI8I', -1, 0, -1, 0, -1, 0, len(libs), nsym, 1, reloc_at, strings_at, hash_at, 0,
                         len(exports))
    hashes = struct.pack('>I', len(exports) << 18 | 0)                  # one slot: the whole chain
    hashes += b''.join(struct.pack('>I', name_hash(n)) for n, _, _ in exports)
    hashes += b''.join(struct.pack('>IIh', cls << 24 | at, off, 1) for (_, cls, off), at in zip(exports, export_names))
    body = header + lib_table + sym_table + reloc_headers + relocs + strings
    return body + bytes(hash_at - len(body)) + hashes


def pef(code, data, ldr):
    align = lambda n: (n + 15) & ~15
    at = align(40 + 3 * 28)
    code_at, data_at = at, align(at + len(code))
    loader_at = align(data_at + len(data))
    head = struct.pack('>4s4s4s5I2hI', b'Joy!', b'peff', b'pwpc', 1, DATE, 0, 0, 0, 3, 2, 0)
    head += struct.pack('>i5I4B', -1, 0, len(code), len(code), len(code), code_at, 0, 4, 4, 0)       # code
    head += struct.pack('>i5I4B', -1, 0, IMAGE_END, len(data), len(data), data_at, 1, 4, 4, 0)      # data
    head += struct.pack('>i5I4B', -1, 0, 0, 0, len(ldr), loader_at, 4, 4, 4, 0)                     # loader
    out = bytearray(loader_at + len(ldr))
    for off, b in ((0, head), (code_at, code), (data_at, data), (loader_at, ldr)):
        out[off:off + len(b)] = b
    return bytes(out)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        container = pef(code_bytes(tmp), data_bytes(), loader())
    if len(sys.argv) == 3 and sys.argv[1] == '--pef':
        open(sys.argv[2], 'wb').write(container)
    rows = ['    ' + ','.join(f'0x{b:02x}' for b in container[i:i + 24]) + ',' for i in range(0, len(container), 24)]
    version = subprocess.run(['clang', '--version'], check=True, capture_output=True, text=True).stdout.splitlines()[0]
    with open(OUT, 'w') as f:
        f.write('/* SPDX-License-Identifier: GPL-2.0-or-later\n * Generated by guest/ndrv/mkpef.py from guest/ndrv/display.c'
                f' ({version}): the display NDRV, a PEF\n * container for the boot display node. Do not edit. */\n'
                f'static const uint8_t display_ndrv[{len(container)}]={{\n' + '\n'.join(rows) + '\n};\n')
    print(f'{OUT}: {len(container)} bytes')


main()
