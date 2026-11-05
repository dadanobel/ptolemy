#!/usr/bin/env python3
"""Reject an experimental 970 engine before it can enter the application."""

from pathlib import Path
import subprocess
import sys

if len(sys.argv) != 2:
    sys.exit('Usage: check-product-engine.py ENGINE')
engine = Path(sys.argv[1]).resolve(strict=True)
symbols = subprocess.run(
    ['/usr/bin/nm', '-gUj', str(engine)], text=True, capture_output=True, check=True
).stdout.splitlines()
if not {'_jit_run', '_interp_step'}.issubset(symbols):
    sys.exit('The application requires the unstripped G3/G4 engine; identity could not be verified')
if any(s.startswith(('_ppc970_', '_interp970_', '_pt_g5_', '_pt_xlate970')) for s in symbols):
    sys.exit('The application refuses an engine containing the experimental G5 core; build with PT_BUILD_970=0')
# Parsing must refuse this model before opening firmware, disks, or a guest.
result = subprocess.run([str(engine), '--cpu', '970mp'], capture_output=True, timeout=10)
if result.returncode != 2:
    sys.exit('The engine did not refuse the G5 CPU selector')
print('PASS product engine: G3/G4 entry points present, no 970 core, G5 selector refused before boot')
