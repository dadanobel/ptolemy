#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/r1
clang -std=c11 -D_DARWIN_C_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/r1-export.c src/export.c -pthread -o build/r1/test-export
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-export
sh tools/build-product.sh --test
CF='-std=c11 -D_DARWIN_C_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined'
clang $CF -Iinclude tests/r1-input.c -framework CoreFoundation -o build/r1/test-input
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-input
clang $CF -Isrc/optics tests/r1-yuv.c src/optics/pixel.c -o build/r1/test-yuv
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-yuv
clang $CF -Iinclude tests/r1-network.c src/platform.c -pthread -o build/r1/test-network
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-network
clang $CF -Iinclude tests/r1-tcp.c src/platform.c -pthread -o build/r1/test-tcp
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-tcp
clang $CF -Iinclude tests/web.c src/web.c -o build/r1/test-web
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-web
clang $CF -Iinclude tests/r1-export-agent.c src/agent.c src/export.c src/optics/diagnostics.c -pthread -o build/r1/test-export-agent
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-export-agent
clang $CF -Iinclude tests/r1-guest-export.c -o build/r1/test-guest-export
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/r1/test-guest-export
PYTHONDONTWRITEBYTECODE=1 python3 tools/check_R1_negatives.py
