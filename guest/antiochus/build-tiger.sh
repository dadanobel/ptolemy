#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")"
case "$(uname -s):$(uname -m)" in Darwin:ppc|Darwin:Power*) ;; *) echo 'Build inside Tiger PPC with the supplied Apple GCC wrapper.' >&2; exit 2;; esac
: "${PT_TIGER_CC:?Set PT_TIGER_CC to the existing devkit/bin/ppc-cc}"
case "${ANT_ICON_VARIANT:-tiger}" in tiger) icon=Tiger;; leopard) icon=Leopard;; *) echo 'ANT_ICON_VARIANT must be tiger or leopard' >&2;exit 2;; esac
app=build/Antiochus.app/Contents
mkdir -p build/obj "$app/MacOS" "$app/Resources"
flags='-std=gnu99 -O2 -Wall -Wextra -fno-common -fno-fast-math -mcpu=750 -mno-altivec -mno-fused-madd'
for name in bench cpu ppc gpu image veclib panion panion_oracle panion_gl; do
    "$PT_TIGER_CC" $flags -Isrc -c "src/$name.c" -o "build/obj/$name.o"
done
"$PT_TIGER_CC" $flags -mcpu=7450 -maltivec -faltivec -Isrc -c src/vmx.c -o build/obj/vmx.o
"$PT_TIGER_CC" $flags -fobjc-exceptions -Isrc -c src/fork-view.m -o build/obj/fork-view.o
"$PT_TIGER_CC" $flags -fobjc-exceptions -Isrc -c src/conformance.m -o build/obj/conformance.o
"$PT_TIGER_CC" $flags -fobjc-exceptions -Isrc -c src/window.m -o build/obj/window.o
"$PT_TIGER_CC" $flags -Isrc -c src/presentation.c -o build/obj/presentation.o
"$PT_TIGER_CC" $flags -fobjc-exceptions build/obj/panion.o build/obj/panion_oracle.o build/obj/panion_gl.o build/obj/conformance.o build/obj/fork-view.o build/obj/window.o build/obj/veclib.o build/obj/presentation.o build/obj/bench.o build/obj/cpu.o build/obj/ppc.o build/obj/gpu.o build/obj/image.o build/obj/vmx.o \
    -Wl,-force_cpusubtype_ALL -framework Cocoa -framework ApplicationServices -framework Accelerate -framework QuartzCore -framework OpenGL -o "$app/Resources/antiochus-worker"
"$PT_TIGER_CC" $flags -Isrc -c src/score.c -o build/obj/score.o
"$PT_TIGER_CC" $flags -fobjc-exceptions -Isrc -c src/Antiochus.m -o build/obj/Antiochus.o
"$PT_TIGER_CC" $flags -fobjc-exceptions build/obj/Antiochus.o build/obj/score.o -framework Cocoa -framework ApplicationServices -o "$app/MacOS/Antiochus"
cp THIRD-PARTY.md resources/graphics-report.pl "$app/Resources/"
cp resources/Info.plist "$app/Info.plist"
cp "resources/Antiochus-$icon.icns" "$app/Resources/Antiochus.icns"
if [ -n "${ANT_CLASSIC_BIN:-}" ]; then
    cp "$ANT_CLASSIC_BIN" "$app/Resources/AntiochusClassic.bin"
else
    rm -f "$app/Resources/AntiochusClassic.bin"
    echo 'Classic source is supplied, but its PEF executable has not been included.' >&2
fi
{
    echo 'Antiochus 1.0-draft4 / antiochus-score-4'
    echo "Flags: $flags; VMX only: -mcpu=7450 -maltivec -faltivec"
    "$PT_TIGER_CC" --version
    /usr/bin/openssl sha1 src/* classic/* resources/graphics-report.pl resources/Info.plist resources/Rosetta-Info.plist resources/Rosetta.icns
    /usr/bin/openssl sha1 "$app/MacOS/Antiochus" "$app/Resources/antiochus-worker"
    if [ -f "$app/Resources/AntiochusClassic.bin" ]; then /usr/bin/openssl sha1 "$app/Resources/AntiochusClassic.bin"; fi
} > "$app/Resources/build-manifest.txt"
echo 'Built build/Antiochus.app. Run the acceptance protocol before distributing.'

rosetta='build/Rosetta Stone.app/Contents'
mkdir -p "$rosetta/MacOS" "$rosetta/Resources"
cp "$app/Resources/antiochus-worker" "$rosetta/MacOS/Rosetta"
cp THIRD-PARTY.md "$rosetta/Resources/THIRD-PARTY.md"
cp resources/Rosetta-Info.plist "$rosetta/Info.plist"
cp resources/Rosetta.icns "$rosetta/Resources/Rosetta.icns"
cp "$app/Resources/build-manifest.txt" "$rosetta/Resources/build-manifest.txt"
echo 'Built build/Rosetta Stone.app (interactive, unscored).'
