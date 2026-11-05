#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=${PT_G5_BUILD_DIR:-build}
mkdir -p "$out"
flags='-std=c11 -O1 -g -Wall -Wextra -Werror -fno-omit-frame-pointer'
sanitize=${PT_G5_SANITIZER:-address,undefined}
sources='src/g5_cpu.c src/g5_integer.c src/g5_mmu.c src/g5_memory.c src/g5_dec.c'
clang $flags -fsanitize="$sanitize" -Isrc tests/cpu-runtime.c $sources -pthread -o "$out/cpu-runtime"
"$out/cpu-runtime"
clang $flags -fsanitize="$sanitize" -Isrc tests/machine.c $sources src/g5_machine.c src/g5_pic.c src/g5_fabric.c src/g5_owners.c -pthread -o "$out/machine"
"$out/machine"
