#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=${PT_G5_BUILD_DIR:-build}
mkdir -p "$build_dir"
cc=${CC:-clang}
"$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I src src/g5_memory.c src/g5_mmu.c src/g5_integer.c \
    src/g5_fabric.c src/g5_of.c tests/contracts.c -pthread -o "$build_dir/contracts"
"$build_dir/contracts"

"$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I src src/g5_pic.c tests/pic.c -pthread -o "$build_dir/pic"
"$build_dir/pic"

"$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I src src/g5_dec.c tests/dec.c -o "$build_dir/dec"
"$build_dir/dec"

"$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I src src/g5_memory.c src/g5_dart.c tests/dart.c -pthread -o "$build_dir/dart"
"$build_dir/dart"

"$cc" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -I src src/g5_fabric.c src/g5_owners.c tests/owners.c -pthread -o "$build_dir/owners"
"$build_dir/owners"

python3 tests/page_diff.py
