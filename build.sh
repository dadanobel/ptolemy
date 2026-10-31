#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")"
E=${E:-./epicycle}   # G4-capable isolated engine; do not point at an unpatched G3 engine
mkdir -p build
case "${1-}" in ""|--test) ;; *) echo "Usage: $0 [--test]" >&2; exit 2;; esac
if [ "$#" -gt 1 ]; then echo "Usage: $0 [--test]" >&2; exit 2; fi
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude \
    src/platform.c tests/devices.c -o build/test-devices -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude \
    src/platform.c src/block.c tests/block.c -o build/test-block -pthread
clang -std=c11 -O2 -Wall -Wextra -Werror -I"$E" \
    "$E"/mmu.c tests/memory.c -o build/test-memory
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude \
    src/platform.c tests/pic-cli.c -o build/pic-cli -pthread
objects=""
for unit in platform block dma scsi atapi ata hid input usb_msc ohci audio audio_macos pmu nvram board devices; do
    clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude \
        -c "src/$unit.c" -o "build/$unit.o" -pthread
    objects="$objects build/$unit.o"
done
clang -fobjc-arc -O2 -Wall -Wextra -Werror -Iinclude -c src/input_macos.m -o build/input_macos.o
rm -f build/libptdevices.a   # ar keeps members of units that no longer exist
ar rcs build/libptdevices.a $objects build/input_macos.o
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude \
    tests/peripherals.c build/libptdevices.a -o build/test-peripherals -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude \
    tests/platform-v3.c build/libptdevices.a -o build/test-platform-v3 -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude tests/product.c build/libptdevices.a -o build/test-product -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude \
    src/platform.c src/block.c src/scsi.c tests/optical.c -o build/test-optical -pthread
clang -std=c11 -O2 -Wall -Wextra -Werror -Iinclude tests/boot-mode.c -o build/test-boot-mode
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude src/platform.c src/pmu.c tests/pmu-faults.c -o build/test-pmu-faults -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude tests/input.c build/libptdevices.a -o build/test-input -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude tests/media-lifetime.c build/libptdevices.a -o build/test-media-lifetime -pthread -framework AudioUnit -framework AudioToolbox -framework CoreAudio
clang -std=c11 -O2 -Wall -Wextra -Werror -c src/optics/diagnostics.c -o build/optics-diagnostics.o
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude -c src/agent.c -o build/agent.o -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude tests/agent-transfer.c src/export.c build/agent.o build/optics-diagnostics.o -o build/test-agent-transfer -pthread
# CPU model unit tests (G4 system registers, decode coverage, AltiVec runner).
clang -O2 -std=gnu11 -Iepicycle tests/g4-system.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c -o build/test-g4-system
clang -O2 -std=gnu11 -Iepicycle tests/g4-decode-runner.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c -o build/test-g4-decode
clang -O2 -std=gnu11 -Iepicycle tests/altivec-runner.c epicycle/altivec.c -o build/test-altivec-runner
# The engine with Optics, its tests and the link audit.
# build-optics also runs the host device protocol/sanitizer contracts (tools/check_S4.py).
sh tools/build-optics.sh
if [ "${1-}" = "--test" ]; then
    python3 tools/test-optics-contracts.py
    ./build/test-devices
    ./build/test-block
    ./build/test-memory
    ./build/test-peripherals
    ./build/test-media-lifetime
    ./build/test-input
    ./build/test-product
    ./build/test-platform-v3
    ./build/test-boot-mode
    ./build/test-pmu-faults
    ./build/test-optical
    ./build/test-agent-transfer
    ./build/test-g4-system
    PYTHONDONTWRITEBYTECODE=1 python3 tests/g4-coverage.py build/test-g4-decode
fi
