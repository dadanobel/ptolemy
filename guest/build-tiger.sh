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
"$PT_TIGER_CC" $common -fobjc-exceptions guest/agent/Agent.m -framework Cocoa -o "$app/MacOS/PtolemyAgent"
cp guest/agent/Info.plist "$app/Info.plist"
echo 'Built build/tiger/PtolemyAgent.app.'
