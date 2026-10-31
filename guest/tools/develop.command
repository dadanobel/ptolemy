#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# LOCAL development bootstrap from the user's Xcode tools, not release media.
set -eu
media=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
support="$HOME/Library/Application Support/Ptolemy"
mkdir -p "$support"
stage=$(/usr/bin/mktemp -d "$support/develop.XXXXXX")
echo "Local build retained at: $stage"
mkdir "$stage/source" "$stage/devkit"
/usr/bin/tar -xzf "$media/source.tar.gz" -C "$stage/source"
/usr/bin/tar -xzf "$media/devkit.tar.gz" -C "$stage/devkit"
PT_TIGER_CC="$stage/devkit/bin/ppc-cc";export PT_TIGER_CC
/bin/sh "$stage/source/guest/build-tiger.sh"
/usr/bin/python "$stage/source/guest/tools/prepare.py" "$stage/source/build/tiger" "$stage/source/guest/tools" "$stage/install"
exec /usr/bin/python "$stage/install/install.py"
