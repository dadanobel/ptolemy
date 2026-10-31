#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Default: compile only. --emit translates blocks without executing them.
# --run-unit executes generated A64 fixtures on the host, never boots Tiger.
set -eu
cd "$(dirname "$0")/.."
case "${1:-}" in ''|--emit|--run-unit) ;; *) echo 'usage: tools/build-perf.sh [--emit|--run-unit]' >&2;exit 2;; esac
out=build/perf-20261022-sol
mkdir -p "$out"
for test in fpr-cache-jit fp-fmas-jit fpscr-lazy-jit vmx-integer-jit vmx-fp-jit vmx-mem-jit mfcr-fuse-jit fp-more-jit vmx-fp2-jit jit-check-state fp-mixed-add-jit fpscr-fr cpu-config fp-kernels-jit perf-emission;do
    clang -std=gnu11 -D_DARWIN_C_SOURCE -O2 -Iepicycle "tests/$test.c" \
        epicycle/interp.c epicycle/altivec.c epicycle/mmu.c -lm -o "$out/$test"
done
if test "${1:-}" = --emit;then "$out/perf-emission";fi
if test "${1:-}" = --run-unit;then
    for test in fpr-cache-jit fp-fmas-jit fpscr-lazy-jit vmx-integer-jit vmx-fp-jit vmx-mem-jit mfcr-fuse-jit fp-more-jit vmx-fp2-jit jit-check-state fp-mixed-add-jit fpscr-fr cpu-config fp-kernels-jit;do "$out/$test";done
    "$out/fp-kernels-jit" --bench; "$out/fp-kernels-jit" --bench --no-cache
fi
echo 'CPU fixtures built. This script does not build or run the stand.'
