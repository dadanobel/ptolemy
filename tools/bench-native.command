#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
D=$(cd "$(dirname "$0")" && pwd)
exec > "$D/native-status.txt" 2>&1
W=0
while [ $W -lt 180 ] && ps -axco command | grep -q '^kextcache$'; do sleep 1; W=$((W+1)); done
printf 'kextcache_wait=%s\n' "$W";sysctl hw.vectorunit
system_profiler SPDisplaysDataType > "$D/native-display.txt"
{ echo gl_vsync 0; i=0; while [ $i -lt 300 ]; do echo wait; i=$((i+1)); done
  printf 'timerefresh 300\nwait\ntimerefresh 300\nwait\ntimerefresh 300\n'
  i=0; while [ $i -lt 20 ]; do echo wait; i=$((i+1)); done; echo quit
} > "$D/valve/bench_native.cfg"
"$D/Half-Life.app/Contents/MacOS/xash3d" -nomsgbox -nosound -ref gl -width 800 -height 600 -fullscreen +map c0a0 +exec bench_native.cfg > "$D/native-stdout.txt" 2>&1
L="$D/last-run.log"
if [ -n "$L" ]; then
  cp "$L" "$D/native-game.log"
  tr -d '\033' < "$L" | grep 'timerefresh:' > "$D/native-fps.txt"
fi
printf 'done\n'
sync
