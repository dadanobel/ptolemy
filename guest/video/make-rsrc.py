# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes the resource file of Ptolemy Video.component: the Component Manager's 'thng' (extended form, one platform),
the 'dlle' naming the entry point, and two strings. Usage: make-rsrc.py OUT.rsrc [--check]

Runs under Tiger's Python 2.3 (the build) and under Python 3 (the host's check), so: no bytes literals, no
conditional expressions, no with. The layouts are Components.r's 'thng' template (version 1) and the resource fork
format of Inside Macintosh: More Macintosh Toolbox, 1-121."""
import struct
import sys

EMPTY = struct.pack('')
ENTRY = 'PtVideoDispatch'
FLAGS = 0x90000420  # Apple's H.264 decompressor's 0x10000420 (thread-safe among them) + cmpWantsRegisterMessage
VERSION = 0x00010001
REGISTER = 0x08 | 0x01  # componentHasMultiplePlatforms, componentDoAutoVersion
POWERPC_NATIVE_ENTRY = 5


def code(text):
    return text.encode('ascii')


def pstring(text):
    return struct.pack('B', len(text)) + code(text)


def thing():
    return EMPTY.join([
        struct.pack('>4s4s4sII', code('imdc'), code('avc1'), code('Ptol'), 0, 0),
        struct.pack('>Ih', 0, 0),  # no code resource: the platform entry below names it
        struct.pack('>4sh', code('STR '), 256),
        struct.pack('>4sh', code('STR '), 257),
        struct.pack('>Ih', 0, 0),  # no icon
        struct.pack('>IIh', VERSION, REGISTER, 0),
        struct.pack('>I', 1),
        struct.pack('>I4shh', FLAGS, code('dlle'), 256, POWERPC_NATIVE_ENTRY)])


RESOURCES = [
    ('thng', 256, thing()),
    ('dlle', 256, code(ENTRY) + struct.pack('B', 0)),
    ('STR ', 256, pstring('Ptolemy H.264')),
    ('STR ', 257, pstring('Sends H.264 to the Mac that runs Ptolemy and shows what comes back.')),
]


def fork(resources):
    types = []
    for kind, number, body in resources:
        if kind not in types:
            types.append(kind)
    data = EMPTY
    offsets = []
    for kind, number, body in resources:
        offsets.append(len(data))
        data += struct.pack('>I', len(body)) + body
    type_list = struct.pack('>h', len(types) - 1)
    references = EMPTY
    first = 2 + 8 * len(types)  # reference lists follow the type list; offsets count from the type list
    for kind in types:
        own = [i for i in range(len(resources)) if resources[i][0] == kind]
        type_list += struct.pack('>4shH', code(kind), len(own) - 1, first + len(references))
        for i in own:
            references += struct.pack('>hHI4x', resources[i][1], 0xffff, offsets[i])  # no name, no attributes
    names = 28 + len(type_list) + len(references)
    head = struct.pack('>IIII', 256, 256 + len(data), len(data), names)
    body = head + struct.pack('>IHHHH', 0, 0, 0, 28, names) + type_list + references
    return head + struct.pack('240x') + data + body


def read(blob):
    """The resources of a fork, read back by the offsets alone."""
    data_at, map_at, data_len, map_len = struct.unpack('>IIII', blob[:16])
    assert len(blob) == map_at + map_len and map_at == data_at + data_len
    types_at, names_at = struct.unpack('>HH', blob[map_at + 24:map_at + 28])
    assert names_at == map_len
    base = map_at + types_at
    count = struct.unpack('>h', blob[base:base + 2])[0] + 1
    found = []
    for t in range(count):
        kind, n, refs = struct.unpack('>4shH', blob[base + 2 + 8 * t:base + 10 + 8 * t])
        for r in range(n + 1):
            at = base + refs + 12 * r
            number, name, where = struct.unpack('>hHI', blob[at:at + 8])
            assert name == 0xffff and where >> 24 == 0
            size = struct.unpack('>I', blob[data_at + where:data_at + where + 4])[0]
            found.append((kind.decode('ascii'), number, blob[data_at + where + 4:data_at + where + 4 + size]))
    return found


def main():
    blob = fork(RESOURCES)
    out = open(sys.argv[1], 'wb')
    out.write(blob)
    out.close()
    if '--check' in sys.argv:
        back = read(open(sys.argv[1], 'rb').read())
        assert back == RESOURCES, back
        t = back[0][2]
        assert len(t) == 20 + 4 * 6 + 10 + 4 + 12
        kind, sub, maker = struct.unpack('>4s4s4s', t[:12])
        assert (kind, sub, maker) == (code('imdc'), code('avc1'), code('Ptol'))
        flags, entry, number, platform = struct.unpack('>I4shh', t[-12:])
        assert flags >> 31 and entry == code('dlle') and number == 256 and platform == 5
        sys.stdout.write('make-rsrc: %d bytes, %d resources, entry %s\n' % (len(blob), len(back), ENTRY))


main()
