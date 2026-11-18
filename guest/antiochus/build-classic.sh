#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")"
mkdir -p build/classic
docker run --rm --network none --cpus 1 --platform linux/amd64 \
    -v "$PWD/classic:/src:ro" -v "$PWD/build/classic:/out" \
    ghcr.io/autc04/retro68@sha256:dd923969100f408943f73c9618b3c7628686aae819583eecb0ddd1837da6943c \
    /bin/sh -c 'cmake -S /src -B /out -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake && cmake --build /out -j1'
