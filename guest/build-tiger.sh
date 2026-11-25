#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds Ptolemy Tools (Ptolemy Agent.app) inside Tiger PPC with the compiler from your own Xcode for Tiger:
# tools/extract-tiger-toolchain.py extracts it on the Mac, guest/toolchain/ppc-cc runs it in Tiger. Compiles only.
set -eu
cd "$(dirname "$0")/.."
case "$(uname -s):$(uname -m)" in Darwin:ppc|Darwin:Power*) ;; *) echo 'Run inside Tiger PPC.' >&2; exit 2;; esac
: "${PT_TIGER_CC:?Set PT_TIGER_CC to the extracted devkit bin/ppc-cc}"
common='-std=gnu99 -O2 -Wall -Wextra -fno-common'
app=build/tiger/PtolemyAgent.app/Contents
mkdir -p "$app/MacOS" "$app/Resources"
"$PT_TIGER_CC" $common -Iinclude -Isrc/optics guest/agent/worker.c guest/agent/call.S src/optics/diagnostics.c \
    -framework CoreFoundation -o "$app/Resources/ptolemy-agent-worker"
"$PT_TIGER_CC" $common -fobjc-exceptions guest/agent/Agent.m -framework Cocoa -framework ApplicationServices \
    -o "$app/MacOS/PtolemyAgent"
cp guest/agent/Info.plist "$app/Info.plist"
# The DrawSprocket shim (guest/dsp): one library, which Ptolemy Agent puts into the DrawSprocket games it starts.
# Not -dynamiclib: Apple GCC 4.0's driver runs /usr/bin/libtool for it (absolute path, absent in Tiger without Xcode);
# ld makes the library itself, as libtool would: -dylib with dylib1.o in place of crt1.o.
"$PT_TIGER_CC" $common -nostartfiles -Wl,-dylib -Wl,-ldylib1.o guest/dsp/ptdsp.c -framework ApplicationServices \
    -o build/tiger/ptdsp.dylib
# Ptolemy Video (guest/video): the QuickTime component that hands H.264 to the host; the Tools medium carries it and the
# installer links it from ~/Library/QuickTime.
video='build/tiger/Ptolemy Video.component/Contents'
mkdir -p "$video/MacOS" "$video/Resources"
"$PT_TIGER_CC" $common -bundle -Iinclude guest/video/component.c guest/agent/call.S \
    -framework QuickTime -framework Carbon -o "$video/MacOS/Ptolemy Video"
python guest/video/make-rsrc.py "$video/Resources/Ptolemy Video.rsrc"
cp guest/video/Info.plist "$video/Info.plist"
echo 'Built build/tiger/PtolemyAgent.app.'
