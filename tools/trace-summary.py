#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Summarize the firmware harness MMIO capture; this is NOT deterministic replay."""
import collections
import json
import pathlib
import sys

counts = collections.Counter()
unknown = collections.Counter()
previous = -1
with pathlib.Path(sys.argv[1]).open() as f:
    for number, line in enumerate(f, 1):
        event = json.loads(line)
        if event["seq"] != previous + 1:
            raise ValueError("discontinuous sequence at line %d" % number)
        previous = event["seq"]
        if event["size"] not in (1, 2, 4, 8) or event["rw"] not in ("r", "w"):
            raise ValueError("invalid MMIO record at line %d" % number)
        key = "0x%08x/%s/%d" % (event["pa"], event["rw"], event["size"])
        counts[key] += 1
        if not event["known"]:
            unknown[key] += 1
print(json.dumps({"captured_events": previous + 1, "top": counts.most_common(20),
                  "unknown": unknown.most_common(50),
                  "scope": "MMIO prefix only; see console for capture limit"}, indent=2))
