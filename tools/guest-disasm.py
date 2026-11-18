#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Disassemble guest PowerPC code (owner's rule No. 3: read the code before guessing).

  guest-disasm.py FILE [SYMBOL|0xADDR ...]                       a local Mach-O (the ppc slice of a fat file)
  guest-disasm.py --socket SOCK GUEST_PATH [SYMBOL|0xADDR ...]   fetched from the running guest through the agent

Without SYMBOL: lists the text symbols. -n N: instructions per item (default: to the next symbol, at most 400).
Calls into dyld stubs are shown by the imported name. Needs capstone (pip install capstone); development tool.
"""
import argparse, base64, os, re, struct, subprocess, sys
from pathlib import Path

CHUNK = 393216  # base64 of a chunk stays under the agent's 1 MiB output limit


def fetch(sock, guest, cache):
    q = lambda cmd: subprocess.run([sys.executable, str(Path(__file__).with_name('agentctl.py')), '--socket', sock,
                                    '--timeout', '120', '--', '/bin/sh', '-c', cmd], capture_output=True, text=True).stdout
    size = int(q("/usr/bin/stat -f %z '" + guest + "'").split()[0])
    local = Path(cache) / (re.sub(r'[^A-Za-z0-9._-]', '_', guest.strip('/')) + '-' + str(size))
    if local.exists() and local.stat().st_size == size:
        return local
    data = b''.join(base64.b64decode(q("/bin/dd if='%s' bs=%d skip=%d count=1 2>/dev/null | /usr/bin/openssl base64" %
                                       (guest, CHUNK, k))) for k in range((size + CHUNK - 1) // CHUNK))
    if len(data) != size:
        sys.exit('fetched %d of %d bytes' % (len(data), size))
    local.parent.mkdir(parents=True, exist_ok=True)
    local.write_bytes(data)
    return local


def macho(b):
    if b[:4] == b'\xca\xfe\xba\xbe':  # fat: take the PowerPC slice
        for i in range(struct.unpack('>I', b[4:8])[0]):
            cpu, _, off, size, _ = struct.unpack('>5I', b[8 + 20 * i:28 + 20 * i])
            if cpu == 18:
                return macho(b[off:off + size])
        sys.exit('no ppc slice')
    if b[:4] != b'\xfe\xed\xfa\xce':
        sys.exit('not a 32-bit big-endian Mach-O')
    ncmds = struct.unpack('>I', b[16:20])[0]
    sects, syms, stubs, at, symtab, indirect = [], {}, {}, 28, None, 0
    for _ in range(ncmds):
        cmd, size = struct.unpack('>2I', b[at:at + 8])
        if cmd == 1:  # LC_SEGMENT
            for s in range(struct.unpack('>I', b[at + 48:at + 52])[0]):
                o = at + 56 + 68 * s
                name = b[o:o + 16].rstrip(b'\0').decode()
                addr, sz, off, _, _, _, flags, r1, r2 = struct.unpack('>9I', b[o + 32:o + 68])
                sects.append((name, addr, sz, off, flags, r1, r2))
        elif cmd == 2:
            symtab = struct.unpack('>4I', b[at + 8:at + 24])
        elif cmd == 11:  # LC_DYSYMTAB: indirectsymoff
            indirect = struct.unpack('>I', b[at + 56:at + 60])[0]
        at += size
    names = []
    if symtab:
        symoff, nsyms, stroff, _ = symtab
        for i in range(nsyms):
            strx, typ, sect, _, value = struct.unpack('>IBBhI', b[symoff + 12 * i:symoff + 12 * i + 12])
            name = b[stroff + strx:b.index(b'\0', stroff + strx)].decode(errors='replace')
            names.append(name)
            if not typ & 0xe0 and typ & 0x0e == 0x0e and sect and sects[sect - 1][0] == '__text':
                syms.setdefault(value, name)
        for name, addr, sz, off, flags, r1, r2 in sects:
            if flags & 0xff == 8 and r2:  # S_SYMBOL_STUBS: reserved1 = first indirect entry, reserved2 = stub size
                for j in range(sz // r2):
                    k = struct.unpack('>I', b[indirect + 4 * (r1 + j):indirect + 4 * (r1 + j) + 4])[0]
                    stubs[addr + j * r2] = (names[k] if k < len(names) else '?') + ' stub'
    return sects, syms, stubs


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--socket')
    p.add_argument('--cache', default=os.path.expanduser('~/.cache/ptolemy-disasm'))
    p.add_argument('-n', type=int, default=0)
    p.add_argument('file')
    p.add_argument('items', nargs='*')
    a = p.parse_args()
    b = (fetch(a.socket, a.file, a.cache) if a.socket else Path(a.file)).read_bytes()
    sects, syms, stubs = macho(b)
    order = sorted(syms)
    if not a.items:
        for v in order:
            print('%08x %s' % (v, syms[v]))
        return
    by_name = {n: v for v, n in syms.items()}
    import capstone  # after the fetch: the cached copy stays even where capstone is missing
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    label = {**syms, **stubs}
    for item in a.items:
        start = int(item, 16) if item.startswith('0x') else by_name.get(item, by_name.get('_' + item))
        if start is None:
            print('no symbol', item)
            continue
        sec = next((s for s in sects if s[1] <= start < s[1] + s[2]), None)
        if not sec:
            print('%s: not in a section' % item)
            continue
        nxt = next((v for v in order if v > start), sec[1] + sec[2])
        count = a.n or min(400, (nxt - start) // 4)
        off = sec[3] + start - sec[1]
        print('%s:' % label.get(start, item))
        for i in md.disasm(b[off:off + 4 * count], start):
            ops = i.op_str
            m = re.search(r'0x([0-9a-f]+)$', ops)
            if i.mnemonic.startswith('b') and m and int(m.group(1), 16) in label:
                ops += ' <%s>' % label[int(m.group(1), 16)]
            print('  %08x  %-8s %s' % (i.address, i.mnemonic, ops))


if __name__ == '__main__':
    main()
