# Reads prof.bin (written by the Linux stand with PT_PROF=1): python3 profdump.py prof.bin [N|regs]
# prints the N hottest PPC blocks (default 3) with their ARM64 code (capstone); "regs": time-weighted PPC register use.
import struct, sys, re, collections, capstone as cs
ppc = cs.Cs(cs.CS_ARCH_PPC, cs.CS_MODE_32 | cs.CS_MODE_BIG_ENDIAN); arm = cs.Cs(cs.CS_ARCH_ARM64, cs.CS_MODE_ARM)
d = open(sys.argv[1], 'rb').read(); o = 0; blocks = []
while o < len(d):
    pc, n, m, hits = struct.unpack('<4I', d[o:o + 16]); o += 16
    blocks.append((pc, n, m, hits, d[o:o + 4 * n], d[o + 4 * n:o + 4 * n + 4 * m])); o += 4 * n + 4 * m
if sys.argv[2:3] == ['regs']:
    use = collections.Counter(); tot = sum(b[3] for b in blocks)
    for pc, n, m, hits, p, a in blocks:
        for i in ppc.disasm(p, pc):
            for r in re.findall(r'\br(\d+)\b', i.op_str): use[int(r)] += hits / n
    print('register: share of accesses (time-weighted)')
    s = sum(use.values())
    print(' '.join(f'r{r}:{100*v/s:.1f}' for r, v in use.most_common()))
else:
    for pc, n, m, hits, p, a in blocks[:int(sys.argv[2]) if len(sys.argv) > 2 else 3]:
        print(f'=== block {pc:08x}: {n} PPC -> {m} ARM64, samples {hits}')
        for i in ppc.disasm(p, pc): print(f'  {i.address:08x}  {i.mnemonic:8} {i.op_str}')
        print('  --')
        for i in arm.disasm(a, 0): print(f'    {i.address:4x}  {i.mnemonic:8} {i.op_str}')
