#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Explicit offline disk preparation. Never opens the source for writing."""
import argparse
import ctypes
import json
import os
import pathlib
import sys


def clone(source, dest):
    if sys.platform != "darwin":
        raise RuntimeError("clonefile requires macOS")
    lib = ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
    fn = lib.clonefile
    fn.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int)
    fn.restype = ctypes.c_int
    if fn(os.fsencode(source), os.fsencode(dest), 0):
        err = ctypes.get_errno()
        raise OSError(err, os.strerror(err), str(dest))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("action", choices=["clone"])
    p.add_argument("source", type=pathlib.Path)
    p.add_argument("destination", type=pathlib.Path, help="new directory; files are never overwritten")
    a = p.parse_args()
    source = a.source.resolve(strict=True)
    if not source.is_file():
        p.error("source must be a regular file")
    before = source.stat()
    if before.st_size < 512 or before.st_size % 512:
        p.error("raw source must contain a whole number of 512-byte sectors")
    with source.open("rb") as f:
        if f.read(4) == b"QFI\xfb":
            p.error("clone expects a raw image")
    a.destination.mkdir(mode=0o700, parents=False, exist_ok=False)
    out = a.destination / "disk.raw"
    clone(source, out)
    after = source.stat()
    stable = (before.st_size, before.st_mtime_ns, before.st_ino) == (
        after.st_size, after.st_mtime_ns, after.st_ino)
    result = {"action": a.action, "source": str(source), "output": str(out),
              "source_metadata_unchanged": stable, "virtual_bytes": out.stat().st_size,
              "allocated_bytes": out.stat().st_blocks * 512}
    (a.destination / "receipt.json").write_text(json.dumps(result, indent=2) + "\n")
    if not stable:
        raise RuntimeError("source changed during operation; output is not a verified snapshot")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
