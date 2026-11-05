#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=${PT_G5_BUILD_DIR:-../build}
mkdir -p "$build_dir"
clang -std=c11 -O2 -g -Wall -Wextra -Werror -arch arm64 -I src \
    tests/hvf_owners.c src/g5_owners.c src/g5_fabric.c \
    -framework Hypervisor -pthread -o "$build_dir/hvf-owners"
codesign --force --sign - --entitlements tests/hvf-entitlements.plist "$build_dir/hvf-owners"
"$build_dir/hvf-owners"
