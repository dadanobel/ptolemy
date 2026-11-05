#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Directed CPU/device checks. No VM, guest OS, disk image or GPU workload.
set -eu
cd "$(dirname "$0")/.."
out=build/leopard-closeout
mkdir -p "$out"
for name in fp-moves-jit fp-directed-jit; do
    clang -std=gnu11 -D_DARWIN_C_SOURCE -O2 -Iepicycle "tests/$name.c" epicycle/interp.c epicycle/altivec.c epicycle/mmu.c -lm -o "$out/$name"
    "$out/$name"
done
clang -std=gnu11 -D_DARWIN_C_SOURCE -O2 -Iepicycle tests/fpscr-lazy-jit.c epicycle/interp.c epicycle/altivec.c epicycle/mmu.c -lm -o "$out/fpscr-lazy-jit"
PT_FP_MOVES=1 PT_FP_DIRECTED=1 "$out/fpscr-lazy-jit"
clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wl,-dead_strip -ffunction-sections -fdata-sections -fsanitize=address,undefined -framework Hypervisor tests/mmu-segment-key.c -o "$out/mmu-segment-key"
"$out/mmu-segment-key"
clang -std=c11 -D_DARWIN_C_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude src/platform.c src/nvram.c tests/nvram-flash.c -pthread -o "$out/nvram-flash"
"$out/nvram-flash"
clang -O2 -Wall -Wextra -Werror tests/guest-sleep-guard.c -framework CoreFoundation -o "$out/guest-sleep-guard"
"$out/guest-sleep-guard"
python3 tests/fp-window.py
python3 tools/check-leopard-negative.py
