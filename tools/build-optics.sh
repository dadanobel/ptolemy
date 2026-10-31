#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# The product: the stand linked with src/optics (its Radeon frontend, draw decoder, Metal renderer and
# modules). No GLib, gettext, shaderc or SPIRV-Cross in this link. Self-contained: compiles its own device, window and
# agent objects (no build.sh step).
set -eu
cd "$(dirname "$0")/.."
# P/O/OUT overrides build another source copy (paired A/B binaries); OPTICS_NO_TESTS=1 skips the test programs.
E=${E:-./epicycle}; P=${P:-src/optics}; O=${O:-build/optics}; OUT=${OUT:-build/ptolemy-engine}; mkdir -p "$O"
CF="-std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude -I$P -I$P/foundation"
objs=""
for f in frontend draw ring command_stream r300_state pixel address io guard timeline diagnostics diagnostics_io foundation/pm4 foundation/msaa foundation/resolve foundation/primitive \
         foundation/shader_decode foundation/shader_eval foundation/shader_msl; do
    o="$O/$(echo "$f" | tr / _).o"; clang $CF -c "$P/$f.c" -o "$o"; objs="$objs $o"
done
# Platform: devices, window/presenter and agent objects, from source into $O/dev.
D=$O/dev; mkdir -p "$D"; dev=""
for unit in platform block dma scsi atapi ata hid input usb_msc ohci audio audio_macos pmu nvram board devices net_rtl net_user web; do
    clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude -c "src/$unit.c" -o "$D/$unit.o" -pthread; dev="$dev $D/$unit.o"
done
clang -fobjc-arc -O2 -Wall -Wextra -Werror -Iinclude -c src/input_macos.m -o "$D/input_macos.o"
rm -f "$D/libptdevices.a"; ar rcs "$D/libptdevices.a" $dev "$D/input_macos.o"
for unit in stand_window_macos display_macos guest_window_macos net_macos web_macos; do clang -fobjc-arc -O2 -Wall -Wextra -Werror -Iinclude -c "src/$unit.m" -o "$D/$unit.o"; done
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude -c src/agent.c -o "$D/agent.o" -pthread
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude -c src/export.c -o "$D/export.o" -pthread
for unit in agent_guest; do clang -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -I"$E" -c "src/$unit.c" -o "$D/$unit.o"; done
clang -fobjc-arc -O2 -Wall -Wextra -Werror -Iinclude -I"$P" -I"$P/foundation" -c "$P/frontend_host_macos.m" -o "$O/frontend_host_macos.o"
# HVF fault route of fast VRAM (needs the engine's vCPU): linked into the firmware only, not into liboptics.a.
hvfobjs=""; for f in cpu_access hvf_access cpu_guard; do clang $CF -c "$P/$f.c" -o "$O/$f.o"; hvfobjs="$hvfobjs $O/$f.o"; done
clang -fobjc-arc -O2 -Wall -Wextra -Werror -I"$P" -I"$P/foundation" -c "$P/foundation/surface_metal.m" -o "$O/surface_metal.o"
for f in renderer frontend_metal diagnostics_metal; do
    clang++ -std=c++17 -fobjc-arc -O2 -Wall -Wextra -Werror -I"$P" -I"$P/foundation" -c "$P/$f.mm" -o "$O/$f.o"
done
rm -f "$O/liboptics.a"; ar rcs "$O/liboptics.a" $objs "$O"/frontend_host_macos.o "$O"/surface_metal.o "$O"/renderer.o "$O"/frontend_metal.o "$O"/diagnostics_metal.o
clang -std=gnu11 -D_DARWIN_C_SOURCE -DPT_HVF -DPT_OPTICS -DPT_NO_MAIN -O2 -Iinclude -I"$E" \
    src/firmware.c "$D"/export.o "$D"/agent.o "$D"/agent_guest.o \
    "$E"/hvf.c "$E"/interp.c "$E"/altivec.c "$E"/mmu.c "$E"/jit.c "$D"/libptdevices.a -force_load "$O/liboptics.a" $hvfobjs \
    "$D"/stand_window_macos.o "$D"/display_macos.o "$D"/guest_window_macos.o "$D"/net_macos.o "$D"/web_macos.o \
    -framework Carbon -framework AppKit -framework Metal -framework QuartzCore -framework Foundation \
    -framework vmnet -framework ImageIO -framework CoreGraphics -framework Hypervisor -framework AudioUnit -framework AudioToolbox -framework CoreAudio -lc++ \
    -Wl,-map,"$O/link.map" -o "$OUT"
codesign --force --sign - --entitlements "$E"/hv.entitlements "$OUT"
if [ -z "${OPTICS_NO_TESTS:-}" ]; then
sh tools/check_R1.sh
# Device protocol contracts are permanent product checks; no guest or GPU is started.
PYTHONDONTWRITEBYTECODE=1 python3 tools/check_S4.py
clang -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/pointer-math.c -o build/test-pointer-math
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test-pointer-math
clang -std=c11 -D_DARWIN_C_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude tests/pointer-tools.c src/agent.c src/export.c "$P/diagnostics.c" -pthread -o build/test-pointer-tools
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/test-pointer-tools
# Independent network protocol test and optional host transport compile; starts no interface.
sh tools/build-network.sh
# Compile the explicit host audio probe; no audio device is opened here.
sh tools/check_audio_host.sh
# Replay test of a recorded trace, with the CPU reference executor for the draw description.
clang $CF tests/optics-frontend-replay.c tests/optics-draw-reference.c src/platform.c "$P/frontend.c" "$P/io.c" "$P/guard.c" "$P/address.c" "$P/timeline.c" "$P/diagnostics.c" "$P/diagnostics_io.c" "$P/draw.c" "$P/ring.c" "$P/command_stream.c" "$P/r300_state.c" \
    "$P/pixel.c" "$P/foundation/pm4.c" "$P/foundation/shader_decode.c" "$P/foundation/shader_eval.c" -o build/test-optics-replay
# Vertex stream element decode check.
clang $CF tests/optics-draw-decode.c "$P/r300_state.c" "$P/pixel.c" "$P/foundation/pm4.c" "$P/foundation/shader_decode.c" "$P/foundation/shader_eval.c" \
    -o build/test-optics-draw-decode
build/test-optics-draw-decode
# Completion: deferred mode (delayed and failed GPU completion, no double execution), then the default early publish.
clang $CF tests/optics-fence.c src/platform.c "$P/frontend.c" "$P/io.c" "$P/guard.c" "$P/address.c" "$P/timeline.c" "$P/diagnostics.c" "$P/diagnostics_io.c" "$P/draw.c" "$P/ring.c" "$P/command_stream.c" "$P/r300_state.c" "$P/pixel.c" \
    "$P/foundation/pm4.c" "$P/foundation/shader_decode.c" "$P/foundation/shader_eval.c" -o build/test-optics-fence && build/test-optics-fence 2>/dev/null && build/test-optics-fence early 2>/dev/null
# The CP thread beside the CPU (locking, hand-over, reset in flight, close), under ThreadSanitizer.
clang $CF -g -fsanitize=thread tests/optics-cp-thread.c src/platform.c "$P/frontend.c" "$P/io.c" "$P/guard.c" "$P/address.c" "$P/timeline.c" "$P/diagnostics.c" "$P/diagnostics_io.c" "$P/draw.c" "$P/ring.c" "$P/command_stream.c" "$P/r300_state.c" "$P/pixel.c" \
    "$P/foundation/pm4.c" "$P/foundation/shader_decode.c" "$P/foundation/shader_eval.c" -o build/test-optics-cp-thread && { TSAN_OPTIONS=halt_on_error=1 build/test-optics-cp-thread 2>build/test-optics-cp-thread.log || { cat build/test-optics-cp-thread.log; exit 1; }; }
# Fast VRAM on real HVF stage-2 protection; the argument run bypasses the protection (negative control).
clang $CF tests/optics-guard-hvf.c src/platform.c "$P/frontend.c" "$P/io.c" "$P/guard.c" "$P/address.c" "$P/timeline.c" "$P/diagnostics.c" "$P/diagnostics_io.c" "$P/draw.c" "$P/ring.c" "$P/command_stream.c" "$P/r300_state.c" \
    "$P/pixel.c" "$P/foundation/pm4.c" "$P/foundation/shader_decode.c" "$P/foundation/shader_eval.c" "$P/cpu_access.c" "$P/hvf_access.c" "$P/cpu_guard.c" \
    -framework Hypervisor -o build/test-optics-guard-hvf && codesign --force --sign - --entitlements "$E"/hv.entitlements build/test-optics-guard-hvf
build/test-optics-guard-hvf 2>/dev/null && build/test-optics-guard-hvf bypass 2>/dev/null
# The same replay through the PRODUCT Metal executor (no OPTICS_REFERENCE): checks the GPU path against the trace.
clang $CF tests/optics-frontend-replay.c tests/optics-draw-reference.c src/platform.c -Wl,-force_load,"$O/liboptics.a" -framework Metal -framework Foundation -lc++ -o build/test-optics-replay-metal
# Independent depth/stencil storage in the product Metal executor (shared and alternating ZB buffers, CPU-write drop).
clang $CF tests/optics-depth-metal.c src/platform.c -Wl,-force_load,"$O/liboptics.a" -framework Metal -framework Foundation -lc++ -o build/test-optics-depth-metal
fi
# Link audit: the engine links system frameworks only (no GLib, shader cross-compilers, pixman or Vulkan).
if otool -L "$OUT" | grep -E -i "glib|intl|shaderc|spirv|pixman|vulkan"; then echo "the engine links a third-party library" >&2; exit 1; fi
echo "engine: $OUT ($(shasum -a 256 "$OUT" | cut -c1-16)); link audit clean"
