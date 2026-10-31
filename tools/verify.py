#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the packaged files and, optionally, the exact external firmware."""
import argparse
import hashlib
import json
import pathlib


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for part in iter(lambda: f.read(1024 * 1024), b""):
            h.update(part)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rom", type=pathlib.Path)
    a = p.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    manifest = json.loads((root / "MANIFEST.json").read_text())
    for name, expected in manifest["sha256"].items():
        path = (root / name).resolve(strict=True)
        if root not in path.parents or digest(path) != expected:
            raise SystemExit("FAIL: " + name)
    if a.rom and digest(a.rom) != manifest["firmware_sha256"]:
        raise SystemExit("FAIL: firmware differs from the tested build")
    print("PASS: %d package files%s" % (
        len(manifest["sha256"]), "; external firmware matches" if a.rom else ""))
    print("Checksums detect changes; they are not a publisher signature.")


if __name__ == "__main__":
    main()
