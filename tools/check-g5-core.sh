#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 tools/check-pci-firmware.py
out=${PT_970_TEST_DIR:-build/g5-core}
mkdir -p "$out"
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    tests/g5-bridge.c src/g5_bridge.c src/platform.c -o "$out/bridge" -pthread
"$out/bridge"
clang -std=gnu11 -D_DARWIN_C_SOURCE -DPT_ENABLE_970=1 -O1 -g -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-sign-compare -Wno-unused-variable \
    -fsanitize="${PT_970_SANITIZER:-address,undefined}" -fno-omit-frame-pointer -Iepicycle -Iinclude -Wl,-dead_strip \
    tests/g5-core.c epicycle/interp970.c epicycle/mmu970.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c \
    experimental/g5/src/g5_integer.c experimental/g5/src/g5_mmu.c experimental/g5/src/g5_memory.c \
    -o "$out/core" -pthread -lm
"$out/core"
