#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build_dir=${PT_G5_BUILD_DIR:-build}
mkdir -p "$build_dir/library"
cc=${CC:-clang}
set --
for source in src/*.c; do
    object="$build_dir/library/$(basename "${source%.c}").o"
    "$cc" -std=c11 -arch arm64 -O2 -Wall -Wextra -Werror -I src -c "$source" -o "$object"
    set -- "$@" "$object"
done
ar rcs "$build_dir/libptg5.a" "$@"
"$cc" -std=c11 -Wall -Wextra -Werror -Isrc tools/state-layout.c -o "$build_dir/state-layout"
"$build_dir/state-layout" > "$build_dir/cpu-layout.json"
printf '%s\n' "$build_dir/libptg5.a"
