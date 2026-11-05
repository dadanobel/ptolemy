#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=${PT_G5_BUILD_DIR:-build}
mkdir -p "$build_dir"
clang -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=thread -I src \
    src/g5_fabric.c src/g5_owners.c tests/owners.c -pthread -o "$build_dir/owners-tsan"
"$build_dir/owners-tsan"
