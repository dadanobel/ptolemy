#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")"
export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-15.0}   # the oldest macOS Ptolemy runs on (also build-optics.sh)
E=${E:-./epicycle}   # G4-capable isolated engine; do not point at an unpatched G3 engine
mkdir -p build
case "${1-}" in
""|--test) ;;
*) echo "Usage: $0 [--test]" >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [--test]" >&2
    exit 2
fi
CF="-std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude"
clang $CF src/platform.c tests/devices.c -o build/test-devices -pthread
clang $CF src/platform.c src/block.c tests/block.c -o build/test-block -pthread
clang -std=c11 -O2 -Wall -Wextra -Werror -I"$E" \
    "$E"/mmu.c tests/memory.c -o build/test-memory
clang -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/boot-mode.c -o build/test-boot-mode
# CPU model unit tests (G4 system registers, decode coverage, AltiVec runner).
clang -O2 -std=gnu11 -Iepicycle tests/g4-system.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c -o build/test-g4-system
clang -O2 -std=gnu11 -Iepicycle tests/g4-decode-runner.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c -o build/test-g4-decode
clang -O2 -std=gnu11 -Iepicycle tests/altivec-runner.c epicycle/altivec.c -o build/test-altivec-runner
# The engine with Optics, its tests and the link audit.
# build-optics also runs the host device protocol/sanitizer contracts and device tests (tools/check_S4.py).
sh tools/build-optics.sh
# Linked against build-optics' device library, agent and diagnostics objects.
O=${O:-build/optics}
clang $CF tests/input.c "$O"/dev/libptdevices.a -o build/test-input -pthread
clang $CF tests/agent-transfer.c src/export.c "$O"/dev/agent.o "$O"/diagnostics.o -o build/test-agent-transfer -pthread
if [ "${1-}" = "--test" ]; then
    sh tools/check-nvram.sh
    python3 tests/classic-soak.py
    sh tools/check-leopard-closeout.sh
    if [ "${PT_G5_TESTS:-0}" != 0 ]; then
        sh experimental/g5/tools/check.sh
        sh experimental/g5/tools/check-runtime.sh
        python3 experimental/g5/tools/negative_controls.py
        sh tools/check-g5-core.sh
        python3 tools/check-g5-gpu-wait.py
    fi
    python3 tools/test-optics-contracts.py
    ./build/test-devices
    ./build/test-block
    ./build/test-memory
    ./build/test-input
    ./build/test-boot-mode
    ./build/test-agent-transfer
    ./build/test-g4-system
    PYTHONDONTWRITEBYTECODE=1 python3 tests/g4-coverage.py build/test-g4-decode
fi
