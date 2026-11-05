#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/nvram-check
clang -std=c11 -D_DARWIN_C_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude \
    src/platform.c src/nvram.c tests/nvram-default.c -pthread -o build/nvram-check/nvram-default
build/nvram-check/nvram-default
python3 tools/check-nvram-negative.py
