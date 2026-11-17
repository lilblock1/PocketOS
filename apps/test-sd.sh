#!/bin/sh
set -eu
helper=$1
tmp=$(mktemp -d);trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/proc" "$tmp/sys/class/block/mmcblk0p1"
run(){ sh "$helper" --fixture "$tmp" "$1"; }
: > "$tmp/proc/mounts"
test -z "$(run refresh)"
printf '1\n' > "$tmp/sys/class/block/mmcblk0p1/partition"
test "$(run refresh)" = mount
printf '/dev/mmcblk0p1 /mnt/sd_0 vfat rw,nosuid,nodev,noexec 0 0\n' > "$tmp/proc/mounts"
test -z "$(run refresh)"
test "$(run replace-stale)" = "$(printf 'unmount\nmount')"
rm "$tmp/sys/class/block/mmcblk0p1/partition"
test "$(run refresh)" = unmount
printf '/dev/sda1 /mnt/sd_0 vfat rw 0 0\n' > "$tmp/proc/mounts"
if run refresh; then exit 1; fi
