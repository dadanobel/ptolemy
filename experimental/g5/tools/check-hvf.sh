#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=${PT_G5_BUILD_DIR:-../build}
mkdir -p "$build_dir"
clang -std=c11 -O2 -g -Wall -Wextra -Werror -arch arm64 -I src \
    tests/hvf_quad.c tests/hvf_quad.S src/g5_lower.c src/g5_integer.c \
    -framework Hypervisor -pthread -o "$build_dir/hvf-quad"
codesign --force --sign - --entitlements tests/hvf-entitlements.plist "$build_dir/hvf-quad"
"$build_dir/hvf-quad"
