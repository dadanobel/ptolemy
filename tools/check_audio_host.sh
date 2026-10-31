#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/audio
clang -std=c11 -D_DARWIN_C_SOURCE -O2 -Wall -Wextra -Werror -Iinclude tools/audio-host-probe.c src/audio.c src/audio_macos.c src/dma.c src/platform.c -pthread -framework AudioUnit -framework AudioToolbox -framework CoreAudio -o build/audio/audio-host-probe
# Compiling creates no audio device. Running one must be explicit.
if [ "${1:-}" = --silent ] || [ "${1:-}" = --tone ]; then build/audio/audio-host-probe "$1"; fi
