#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reads what an engine run leaves behind and says where the time went (development tool).

  ptolemy-analyze.py CONSOLE [--prof PROF] [--images DIR] [--kernel FILE]   one run
  ptolemy-analyze.py CONSOLE_A CONSOLE_B                          two runs side by side, differences flagged
  ptolemy-analyze.py --timeline FILE [--from S --to S]           PT_TIMELINE: frame times, 1%/0.1% low, pointer latency
  ptolemy-analyze.py --live CONSOLE [--on-alert CMD]             follows a running engine: warns on storms as they start;
      CMD (sh) runs at an alert (at most every 20 s) with $ALERT set: the job's flight recorder (guest ps, a log switched on)

CONSOLE: the engine's output with PT_STATS=1 (5 s windows) and Optics' frame pacing at exit. PROF: PT_PROF_FILE. DIR: guest
binaries fetched by guest-disasm.py (its --cache), used to name the hottest guest addresses; FILE: the guest's mach_kernel
(fetched the same way) for supervisor addresses. ROM (OpenBIOS) and commpage addresses are labelled as such.
"""
import argparse, bisect, collections, importlib.util, os, re, sys
from pathlib import Path

SPR = {1: 'XER', 8: 'LR', 9: 'CTR', 18: 'DSISR', 19: 'DAR', 22: 'DEC', 25: 'SDR1', 26: 'SRR0', 27: 'SRR1', 272: 'SPRG0',
       273: 'SPRG1', 274: 'SPRG2', 275: 'SPRG3', 284: 'TBL', 285: 'TBU', 287: 'PVR', 1008: 'HID0', 1009: 'HID1',
       1013: 'DABR', 1017: 'L2CR', 1018: 'L3CR', 1019: 'ICTC', 1014: 'MSSCR0', 1015: 'MSSSR0', 1023: 'PIR'}
SPR.update({528 + i: '%sBAT%d%s' % ('I', i // 2, 'UL'[i % 2]) for i in range(8)})
SPR.update({536 + i: '%sBAT%d%s' % ('D', i // 2, 'UL'[i % 2]) for i in range(8)})
SPR.update({560 + i: '%sBAT%d%s' % ('I', 4 + i // 2, 'UL'[i % 2]) for i in range(8)})
SPR.update({568 + i: '%sBAT%d%s' % ('D', 4 + i // 2, 'UL'[i % 2]) for i in range(8)})
X31 = {306: 'tlbie', 146: 'mtmsr', 178: 'mtmsrd', 210: 'mtsr', 242: 'mtsrin', 83: 'mfmsr', 595: 'mfsr', 659: 'mfsrin',
       339: 'mfspr', 467: 'mtspr', 371: 'mftb', 566: 'tlbsync', 598: 'sync', 854: 'eieio', 982: 'icbi', 1014: 'dcbz',
       86: 'dcbf', 54: 'dcbst', 470: 'dcbi', 20: 'lwarx', 150: 'stwcx.', 87: 'lbzx', 279: 'lhzx', 23: 'lwzx', 215: 'stbx',
       407: 'sthx', 151: 'stwx', 790: 'lhbrx', 534: 'lwbrx', 918: 'sthbrx', 662: 'stwbrx', 597: 'lswi', 533: 'lswx',
       725: 'stswi', 661: 'stswx', 310: 'eciwx', 438: 'ecowx', 4: 'tw', 512: 'mcrxr', 19: 'mfcr', 144: 'mtcrf'}
X19 = {50: 'rfi', 150: 'isync', 18: 'rfid'}
OP = {32: 'lwz', 33: 'lwzu', 34: 'lbz', 35: 'lbzu', 36: 'stw', 37: 'stwu', 38: 'stb', 39: 'stbu', 40: 'lhz', 41: 'lhzu',
      42: 'lha', 43: 'lhau', 44: 'sth', 45: 'sthu', 46: 'lmw', 47: 'stmw', 48: 'lfs', 50: 'lfd', 52: 'stfs', 54: 'stfd',
      17: 'sc', 3: 'twi', 0: 'op0 (illegal)'}


def mnemonic(key):
    m = re.match(r'(mtspr|mfspr) (\d+)$', key)
    if m:
        return '%s %s' % (m.group(1), SPR.get(int(m.group(2)), m.group(2)))
    m = re.match(r'(\d+)/(\d+)$', key)
    if not m:
        return key
    op, xo = int(m.group(1)), int(m.group(2))
    name = X31.get(xo) if op == 31 else X19.get(xo) if op == 19 else OP.get(op)
    return name or key


def num(s):
    return int(s.replace(',', ''))


def parse(path):
    """PT_STATS windows (per-window counters, cumulative ones turned into deltas) and the exit summary."""
    windows, cur, summary = [], None, {}
    last_dev = last_flush = 0
    for line in open(path, errors='replace'):
        m = re.match(r'== PT_STATS (\d+) s', line)
        if m:
            cur = {'t': int(m.group(1)), 'interp': collections.Counter()}
            windows.append(cur)
            continue
        if line.startswith('frame pacing:'):
            m = re.search(r'([\d.]+) fps\).*?longest (\d+) ms', line)
            summary['fps'], summary['longest_ms'] = float(m.group(1)), int(m.group(2))
        m = re.match(r'stalls of 100 ms or more while streaming: (\d+), ([\d.]+) s', line)
        if m:
            summary['stalls'], summary['stall_s'] = int(m.group(1)), float(m.group(2))
        if cur is None:
            continue
        if 'capacity flushes total' in line:
            n = num(re.search(r'capacity flushes total (\d+)', line).group(1))
            cur['flushes'], last_flush = n - last_flush, n
        m = re.search(r'page generations moved (\d+), of them (\d+) on page numbers with translated code\).*invalidated blocks (\d+)', line)
        if m:
            cur['gen_moved'], cur['gen_code'], cur['invalidated'] = map(int, m.groups())
        m = re.match(r'\s*time, ms: HVF (\d+).*translation (\d+), interpreter (\d+)', line)
        if m:
            cur['hvf_ms'], cur['trans_ms'], cur['interp_ms'] = map(int, m.groups())
        m = re.match(r'\s*exits: link (\d+) lookup (\d+) sc (\d+) interp (\d+) dsi (\d+) irq (\d+)', line)
        if m:
            for k, v in zip(('link', 'lookup', 'sc', 'interp_exits', 'dsi', 'irq'), m.groups()):
                cur[k] = int(v)
            m = re.search(r'jump-cache cell empty (\d+), other target (\d+), stale (\d+)', line)
            if m:
                cur['jc_empty'], cur['jc_other'], cur['jc_stale'] = map(int, m.groups())
        if line.lstrip().startswith('interpreter exits'):
            for k, v in re.findall(r'((?:mtspr|mfspr) \d+|\d+/\d+)×(\d+)', line):
                cur['interp'][k] += int(v)
        m = re.search(r'JIT device accesses (\d+)', line)
        if m:
            n = int(m.group(1))
            cur['dev'], last_dev = n - last_dev, n
        m = re.match(r'ptolemy: VM entries (\d+)', line)
        if m:
            cur['vm_entries'] = int(m.group(1))
    return windows, summary


COLS = [('t', 't s'), ('hvf_ms', 'HVF'), ('trans_ms', 'trans'), ('interp_ms', 'interp'), ('vm_entries', 'VMentry'),
        ('lookup', 'lookup'), ('jc_empty', 'jc-empty'), ('jc_stale', 'jc-stale'), ('interp_exits', 'to-interp'),
        ('dsi', 'dsi'), ('dev', 'devices'), ('gen_code', 'codepg'), ('invalidated', 'inval'), ('flushes', 'flush')]


def totals(windows):
    t = collections.Counter()
    for w in windows:
        for k, _ in COLS[1:]:
            t[k] += w.get(k, 0)
        t.update({'i:' + k: v for k, v in w['interp'].items()})
    return t


def report(path, prof, images, kernel=None):
    windows, s = parse(path)
    print('# %s' % path)
    if s:
        print('frame pacing: %.1f fps, stalls >=100 ms %s (%.2f s), longest %s ms' % (s.get('fps', 0), s.get('stalls', '?'),
                                                                                    s.get('stall_s', 0), s.get('longest_ms', '?')))
    print('\n5 s windows (ms; counts per window):')
    print(' '.join('%8s' % h for _, h in COLS) + '  top interpreter exits')
    for w in windows:
        top = ', '.join('%s %d' % (mnemonic(k), v) for k, v in w['interp'].most_common(3))
        print(' '.join('%8s' % w.get(k, '') for k, _ in COLS) + '  ' + top)
    t = totals(windows)
    print('\ntotals: ' + ', '.join('%s %d' % (h, t[k]) for k, h in COLS[1:]))
    print('interpreter exits, whole run:')
    for k, v in sorted(((k[2:], v) for k, v in t.items() if k.startswith('i:')), key=lambda x: -x[1])[:15]:
        print('  %-14s %9d  (%s)' % (mnemonic(k), v, k))
    if prof:
        hot(prof, images, kernel)


def macho_symbols(path):
    spec = importlib.util.spec_from_file_location('gd', Path(__file__).with_name('guest-disasm.py'))
    gd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gd)
    sects, syms, _ = gd.macho(open(path, 'rb').read())
    text = next((s[1] for s in sects if s[0] == '__text'), 0)
    seg = min((s[1] for s in sects), default=0) & ~0xfff
    return sorted(syms.items()), seg


def hot(prof, images, kernel=None):
    """The hottest guest addresses of a PT_PROF file, named when the image is in DIR (guest path mirrored or flattened)."""
    img, pcs, cache = {}, [], {}
    for line in open(prof, errors='replace'):
        f = line.rstrip('\n').split('\t')
        if f[0] == 'image':
            img[f[1]] = f[8] if len(f) > 8 else '?'
        elif f[0] == 'pc':
            pcs.append((int(f[2]), f[1], int(f[3], 16), int(f[4], 16)))
        elif f[0] in ('session', 'stalls'):
            print('\n' + ' | '.join(f[:2] + f[2:12]))
    total = sum(p[0] for p in pcs) or 1
    print('\nhottest guest addresses (share of sampled guest instructions):')
    for ticks, i, pc, off in sorted(pcs, reverse=True)[:25]:
        name, where = img.get(i, '?'), ''
        local = None
        if name == 'supervisor':
            name = 'ROM (OpenBIOS)' if 0xfff00000 <= pc < 0xffff8000 else 'commpage' if pc >= 0xffff8000 else 'supervisor'
            if kernel and name == 'supervisor':
                if kernel not in cache:
                    cache[kernel] = macho_symbols(kernel)
                syms, _ = cache[kernel]
                best = max((s for s in syms if s[0] <= pc), default=None)
                if best and pc - best[0] < 0x10000:
                    where = ' %s+0x%x' % (best[1], pc - best[0])
        if images and name.startswith('/'):
            for cand in (Path(images) / name.lstrip('/'), *Path(images).glob(re.sub(r'[^A-Za-z0-9._-]', '_', name.strip('/')) + '-*')):
                if cand.exists():
                    local = cand
                    break
        if local:
            if local not in cache:
                cache[local] = macho_symbols(local)
            syms, seg = cache[local]
            a = seg + off
            best = max((s for s in syms if s[0] <= a), default=None)
            if best:
                where = ' %s+0x%x' % (best[1], a - best[0])
        print('  %5.1f%% %08x %s%s' % (100 * ticks / total, pc, name[-60:], where))


def pct(v, q):
    return v[min(len(v) - 1, int(q * len(v)))] if v else 0


def timeline(path, t_from=None, t_to=None):
    """Frames shown, input and the guest's cursor, JIT flushes (PT_TIMELINE, b4-0084)."""
    ev, wall = [], None
    for line in open(path, errors='replace'):
        f = line.split()
        if len(f) >= 5:
            ev.append((f[0], int(f[1]), int(f[2]), int(f[3]), int(f[4])))
            if f[0] == 'w':
                wall = (int(f[1]), int(f[2]) + int(f[3]) / 1e6)
    if not ev:
        sys.exit('empty timeline')
    t0 = min(e[1] for e in ev)
    sec = lambda t: (t - t0) / 1e9
    ev = [e for e in ev if (t_from is None or sec(e[1]) >= t_from) and (t_to is None or sec(e[1]) <= t_to)]
    frames = [e[1] for e in ev if e[0] == 'f']
    gaps = [(b - a) / 1e6 for a, b in zip(frames, frames[1:])]
    stream = sorted((g for g in gaps if g < 1000), reverse=True)  # as Optics: a gap of 1 s or more is a pause, not a frame
    print('# %s%s' % (path, '' if wall is None else ' (wall clock at uptime %.3f s: %.3f)' % (wall[0] / 1e9, wall[1])))
    if stream:
        n = len(stream)
        worst = lambda k: 1000 / (sum(stream[:k]) / k)
        mean = sum(stream) / n
        asc = stream[::-1]
        print('frames %d over %.1f s: average %.1f fps; 1%% low %.1f fps, 0.1%% low %.1f fps' % (
            n + 1, sum(stream) / 1000, 1000 / mean, worst(max(1, n // 100)), worst(max(1, n // 1000))))
        print('frame time, ms: median %.1f, p90 %.1f, p99 %.1f, p99.9 %.1f, max %.1f; >=100 ms: %d (%.2f s)' % (
            pct(asc, 0.5), pct(asc, 0.9), pct(asc, 0.99), pct(asc, 0.999), asc[-1], sum(g >= 100 for g in stream),
            sum(g for g in stream if g >= 100) / 1000))
        per = collections.Counter(int(sec(t)) for t in frames)
        if per:
            print('fps per second:')
            row = []
            for s_ in range(min(per), max(per) + 1):
                row.append('%3d' % per.get(s_, 0))
                if len(row) == 20:
                    print('  %4d s: %s' % (s_ - 19, ' '.join(row)))
                    row = []
            if row:
                print('  %4d s: %s' % (max(per) - len(row) + 1, ' '.join(row)))
    ptr = [e[1] for e in ev if e[0] in 'pr']
    cur = [e[1] for e in ev if e[0] == 'c']
    if ptr:
        lat, miss, j = [], 0, 0
        for t in ptr:
            while j < len(cur) and cur[j] < t:
                j += 1
            if j < len(cur) and cur[j] - t < 500e6:
                lat.append((cur[j] - t) / 1e6)
            else:
                miss += 1
        lat.sort()
        print('pointer -> guest cursor: %d events, latency ms median %.1f, p95 %.1f, p99 %.1f, max %.1f; no cursor move within 0.5 s: %d' % (
            len(ptr), pct(lat, 0.5), pct(lat, 0.95), pct(lat, 0.99), lat[-1] if lat else 0, miss))
    irq = [e for e in ev if e[0] in 'id']
    if irq:
        span = max(1e-9, (max(e[1] for e in irq) - min(e[1] for e in irq)) / 1e9)
        src = collections.Counter('DEC' if e[0] == 'd' else 'src %d' % e[2] for e in irq)
        per = collections.Counter(int(sec(e[1])) for e in irq)
        rates = sorted(per.values())
        print('interrupts taken: %d over %.1f s (%.0f/s; per second median %d, max %d): %s' % (
            len(irq), span, len(irq) / span, pct(rates, 0.5), rates[-1], ', '.join('%s %.0f/s' % (k, v / span) for k, v in src.most_common(6))))
    dec = sorted(e[2] for e in ev if e[0] == 'D')
    if dec:
        us = lambda v: v / 25  # ticks -> us: the engine's timebase is 25 MHz (platform.c ticks(), fw_cfg TBFREQ)
        print('DEC writes (one in 16 logged, ~%d/s): value median %d ticks (~%.0f us), p10 %d, p90 %d, negative %d%%' % (
            16 * len(dec) / max(1e-9, span if irq else 1), pct(dec, 0.5), us(pct(dec, 0.5)), pct(dec, 0.1), pct(dec, 0.9),
            100 * sum(v < 0 for v in dec) // len(dec)))
    dts = [e[1] for e in ev if e[0] == 'd']
    gap = sorted((b - a) / 1e3 for a, b in zip(dts, dts[1:]) if b - a < 1e6)
    if gap:
        print('DEC interrupt intervals under 1 ms: %d, us median %.0f, p10 %.0f, p90 %.0f' % (
            len(gap), pct(gap, 0.5), pct(gap, 0.1), pct(gap, 0.9)))
    late = []
    for e in ev:  # a logged DEC write against the next DEC interrupt: how much later than programmed it was taken
        if e[0] == 'D' and 0 < e[2] < 25000:
            j = bisect.bisect_right(dts, e[1])
            if j < len(dts) and dts[j] - e[1] < 2e6:
                late.append((dts[j] - e[1]) / 1e3 - e[2] / 25)
    late.sort()
    if late:
        print('DEC lateness (writes under 1 ms -> the next DEC interrupt): %d, us median %.0f, p10 %.0f, p90 %.0f' % (
            len(late), pct(late, 0.5), pct(late, 0.1), pct(late, 0.9)))
    sh = [e for e in ev if e[0] == 'S']  # b4-0094: each wait of the CP for a shader (us)
    if sh:
        w = sorted((e[2] / 1000 for e in sh), reverse=True)
        print('shader waits: %d, %.2f s; longest ms %s' % (len(w), sum(w) / 1000, ', '.join('%.0f' % x for x in w[:8])))
    st = [(e, next((x for x in ev[k + 1:k + 64] if x[0] == 't'), ('t', 0, 0, 0, 0))) for k, e in enumerate(ev) if e[0] == 's']
    if st:  # b4-0094: stalls with what the CP did in them; the 't' record follows its 's'
        print('stalls (s: gap ms = shader / idle / fence / running ms, draws):')
        for e, t in st:
            print('  %7.1f: %4.0f = %4.0f / %4.0f / %4.0f / %4.0f, %d' % (sec(e[1]), e[2] / 1000, e[3] / 1000, e[4] / 1000,
                                                                     t[2] / 1000, t[3] / 1000, t[4]))
    qs = [e for e in ev if e[0] == 'q']  # b4-0096: occlusion queries and the Metal batches (B committed, b completed)
    com = {e[2]: e for e in ev if e[0] == 'B'}
    done = [e for e in ev if e[0] == 'b']
    med = lambda v: pct(sorted(v), 0.5) if v else 0
    if com:
        cb = [(e[1] - com[e[2]][1]) / 1e3 - e[3] - e[4] for e in done if e[2] in com]
        print('Metal batches: %d (%.1f a frame); us median: commit -> GPU start %.0f, GPU run %.0f, GPU end -> callback %.0f; jobs median %d' % (
            len(com), len(com) / max(1, len(frames)), med([e[3] for e in done]), med([e[4] for e in done]), med(cb),
            med([e[3] for e in com.values()])))
    if qs:
        wait = sorted(e[2] for e in qs if e[3])
        print('occlusion queries: %d (%.1f a frame), %d written at once; the others wait, us: median %.0f, p90 %.0f, p99 %.0f; draws in a query: median %d' % (
            len(qs), len(qs) / max(1, len(frames)), len(qs) - len(wait), pct(wait, 0.5), pct(wait, 0.9), pct(wait, 0.99),
            med([e[4] for e in qs])))
        dt = [e[1] for e in done]
        part = [[], [], [], [], [], []]
        for e in qs:  # the last batch completed before the word was written, within the query's wait
            j = bisect.bisect_right(dt, e[1]) - 1
            if not e[3] or j < 0 or e[1] - dt[j] > e[2] * 1000 or done[j][2] not in com:
                continue
            b_, c_ = done[j], com[done[j][2]]
            start = e[1] - e[2] * 1000
            for k, v in enumerate(((c_[1] - start) / 1e3, b_[3], b_[4], (b_[1] - c_[1]) / 1e3 - b_[3] - b_[4],
                                   (e[1] - b_[1]) / 1e3, c_[3])):
                part[k].append(v)
        if part[0]:
            print('  of that wait (%d matched to a batch), us median: packet -> commit %.0f, commit -> GPU start %.0f, GPU run %.0f, GPU end -> callback %.0f, callback -> word %.0f; jobs in the batch: median %d' % (
                len(part[0]), med(part[0]), med(part[1]), med(part[2]), med(part[3]), med(part[4]), med(part[5])))
    keys = [e[1] for e in ev if e[0] == 'k' and e[3]]
    if keys:
        print('keys pressed: %d' % len(keys))
    for e in ev:
        if e[0] == 'j':
            after = [g for b, g in zip(frames[1:], gaps) if 0 <= b - e[1] < 500e6]  # gaps that end in the 0.5 s
            print('JIT capacity flush %d at %.2f s: longest frame time in the next 0.5 s %.0f ms' % (e[2], sec(e[1]), max(after) if after else 0))


RULES = (  # (key, absolute floor per 5 s window, factor over the run's median so far, what it means)
    ('lookup', 300000, 4, 'block lookups (jump-cache misses / relinking)'),
    ('invalidated', 1000, 8, 'blocks invalidated'),
    ('interp_exits', 100000, 3, 'exits to the interpreter'),
    ('vm_entries', 800000, 3, 'VM entries'),
    ('dev', 150000, 3, 'device accesses from JIT code'),
    ('dsi', 100000, 4, 'DSI (page faults)'),
    ('trans_ms', 150, 5, 'ms translating'),
)


def alerts(w, history):
    out = []
    for key, floor, factor, what in RULES:
        v = w.get(key, 0)
        past = sorted(h.get(key, 0) for h in history)
        med = past[len(past) // 2] if past else 0
        if v >= floor and (not past or v >= factor * max(med, 1)):
            out.append('%s %d (run median %d)' % (what, v, med))
    if w.get('flushes'):
        out.append('JIT capacity flush')
    if w['interp']:
        k, v = w['interp'].most_common(1)[0]
        if v >= 50000:
            out.append('interpreter storm: %s x%d' % (mnemonic(k), v))
    return out


def live(path, on_alert):
    """Follows a console as the engine writes it; a PT_STATS window is judged when the next one starts."""
    import subprocess, time
    seen, last_cmd = 0, 0.0
    while True:
        windows, s = parse(path) if os.path.exists(path) else ([], {})
        for i in range(seen, max(0, len(windows) - 1)):
            w = windows[i]
            hits = alerts(w, windows[max(0, i - 24):i])
            if hits:
                msg = '%s s: %s' % (w['t'], '; '.join(hits))
                print('ALERT ' + msg, flush=True)
                if on_alert and time.time() - last_cmd >= 20:
                    last_cmd = time.time()
                    subprocess.run(on_alert, shell=True, env={**os.environ, 'ALERT': msg})
            seen = i + 1
        if 'fps' in s:  # the engine printed its exit statistics
            print('engine finished: %.1f fps, stalls >=100 ms %s' % (s['fps'], s.get('stalls', '?')), flush=True)
            return
        time.sleep(2)


def compare(a, b):
    (wa, sa), (wb, sb) = parse(a), parse(b)
    ta, tb = totals(wa), totals(wb)
    print('# A %s\n# B %s' % (a, b))
    for k, label in (('fps', 'fps'), ('stall_s', 'stalls >=100 ms, s'), ('longest_ms', 'longest stall, ms')):
        print('%-22s %10s %10s' % (label, sa.get(k, '?'), sb.get(k, '?')))
    print('%-22s %10s %10s %8s' % ('per run', 'A', 'B', 'B/A'))
    for k, h in COLS[1:]:
        x, y = ta[k], tb[k]
        ratio = y / x if x else float('inf') if y else 1
        flag = '  <<' if (x or y) and (ratio > 1.25 or ratio < 0.8) and max(x, y) > 50 else ''
        print('%-22s %10d %10d %8.2f%s' % (h, x, y, ratio, flag))
    keys = set(k for k in ta if k.startswith('i:')) | set(k for k in tb if k.startswith('i:'))
    print('interpreter exits that differ by more than 25%:')
    for k in sorted(keys, key=lambda k: -max(ta[k], tb[k])):
        x, y = ta[k], tb[k]
        if max(x, y) > 100 and (not x or not y or y / x > 1.25 or y / x < 0.8):
            print('  %-14s %9d %9d' % (mnemonic(k[2:]), x, y))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('console', nargs='*')
    p.add_argument('--timeline')
    p.add_argument('--live')
    p.add_argument('--on-alert')
    p.add_argument('--from', dest='t_from', type=float)
    p.add_argument('--to', dest='t_to', type=float)
    p.add_argument('--prof')
    p.add_argument('--images')
    p.add_argument('--kernel')
    a = p.parse_args()
    if a.live:
        live(a.live, a.on_alert)
    elif a.timeline:
        timeline(a.timeline, a.t_from, a.t_to)
    elif len(a.console) == 2:
        compare(*a.console)
    else:
        report(a.console[0], a.prof, a.images, a.kernel)


if __name__ == '__main__':
    main()
