#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=${PT_970_BENCH_DIR:-build/g5-walk-bench}
mkdir -p "$out"
clang -std=gnu11 -D_DARWIN_C_SOURCE -DPT_ENABLE_970=1 -O2 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable \
    -Iepicycle -Iinclude -Wl,-dead_strip \
    tests/g5-walk-bench.c epicycle/interp970.c epicycle/mmu970.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c \
    experimental/g5/src/g5_integer.c experimental/g5/src/g5_mmu.c experimental/g5/src/g5_memory.c \
    -o "$out/bench" -pthread -lm
"$out/bench"
