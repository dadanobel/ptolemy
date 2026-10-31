#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Run manually INSIDE Tiger. No sudo, installation or network transmission.
set -eu
dest="${1:-$HOME/Desktop/Ptolemy-baseline-$(date +%Y%m%d-%H%M%S)}"
umask 077
mkdir "$dest"
collect() {
    name=$1; shift
    rc=0
    "$@" > "$dest/$name.txt" 2>&1 || rc=$?
    printf '%s %s\n' "$name" "$rc" >> "$dest/status.txt"
}
collect system /usr/bin/sw_vers
collect kernel /usr/bin/uname -a
collect device-tree /usr/sbin/ioreg -p IODeviceTree -l -w0
collect io-service /usr/sbin/ioreg -p IOService -l -w0
collect kexts /usr/sbin/kextstat
collect cpu /usr/sbin/sysctl hw.cputype hw.cpusubtype hw.optional.altivec hw.memsize hw.tbfrequency
collect nvram /usr/sbin/nvram -p
collect mounts /sbin/mount
collect extensions /usr/sbin/system_profiler SPDisplaysDataType SPAudioDataType SPUSBDataType
printf 'Saved baseline to %s\n' "$dest"
