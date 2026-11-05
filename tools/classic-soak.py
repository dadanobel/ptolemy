#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Observe a running Classic workload without restarting it or clearing caches.
Start the engine with PT_STATS=1 and retain its console separately. Repeat the same
workload, record its own completion times, and compare warm windows, not startup.
This sampler reports resources, not application throughput or a leak diagnosis.
"""

import argparse
import json
from pathlib import Path
import subprocess
import sys
import time


def capture(argv, timeout=15):
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {'status': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}
    except subprocess.TimeoutExpired:
        return {'status': 'timeout'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid', type=int, required=True, help='host engine PID')
    p.add_argument('--socket', type=Path, required=True, help='existing agent socket')
    p.add_argument('--output', type=Path, required=True, help='new JSONL file')
    p.add_argument('--samples', type=int, default=61)
    p.add_argument('--interval', type=float, default=60)
    a = p.parse_args()
    if a.pid <= 0 or not 1 <= a.samples <= 1441 or not 15 <= a.interval <= 3600:
        p.error('positive PID; 1..1441 samples; interval 15..3600 seconds')
    agent = Path(__file__).with_name('agentctl.py')
    identity_cmd = ['/bin/ps', '-p', str(a.pid), '-o', 'lstart=,comm=']
    identity = capture(identity_cmd)
    if identity['status'] or 'ptolemy-engine' not in identity.get('stdout', ''):
        p.error('PID must identify a live Ptolemy engine')
    started = time.monotonic()
    with a.output.open('x') as out:
        for i in range(a.samples):
            if capture(identity_cmd) != identity:
                raise SystemExit('Engine stopped or PID changed; samples retained')
            row = {'sample': i, 'elapsed_s': time.monotonic() - started, 'identity': identity['stdout'].strip()}
            row['host'] = capture(['/bin/ps', '-p', str(a.pid), '-o', 'pid=,pcpu=,rss=,vsz=,time='])
            row['host_vm'] = capture(['/usr/bin/vm_stat'])
            # comm avoids logging unrelated command-line arguments in the guest.
            row['guest'] = capture(
                [
                    sys.executable,
                    str(agent),
                    '--socket',
                    str(a.socket),
                    '--timeout',
                    '10',
                    '--',
                    '/bin/sh',
                    '-c',
                    '/bin/ps -axo pid,pcpu,rss,vsz,time,comm; /usr/bin/vm_stat',
                ],
                25,
            )
            out.write(json.dumps(row) + '\n')
            out.flush()
            print('sample', i, 'guest status', row['guest']['status'], flush=True)
            if i + 1 < a.samples:
                time.sleep(max(0, started + (i + 1) * a.interval - time.monotonic()))


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
