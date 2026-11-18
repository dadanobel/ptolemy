#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/.."
: "${CC:=cc}"
mkdir -p build/tests
"$CC" -std=gnu99 -O2 -Wall -Wextra tests/test_score.c src/score.c src/image.c src/presentation.c -lm -o build/tests/score
"$CC" -std=gnu99 -O2 -Wall -Wextra tests/test_cpu.c -lm -o build/tests/cpu
build/tests/score
build/tests/cpu

if [ "$(uname -s)" = Darwin ]; then
    "$CC" -std=gnu99 -O2 -Wall -Wextra tests/test_veclib.c -framework Accelerate -o build/tests/veclib
    build/tests/veclib
fi

"$CC" -std=gnu99 -O2 -Wall -Wextra tests/test_panion.c src/panion.c src/panion_oracle.c -lm -o build/tests/panion
build/tests/panion
