#!/bin/sh
# Test pinned KOReader SD runtime on a real R1 without touching NAND/OTA.
# Only a temporary block node under devtmpfs and RO loop mount under /run.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
umask 077
if [ "$#" -eq 1 ] && [ "$1" = --self-test ]; then
    echo "KOReader R1 SD loop mount smoke-test v1"
    exit 0
fi
[ "$#" -eq 1 ] || {
    echo "Usage: sh r1-koreader-loop-probe.sh EXPECTED_SD_IMAGE_SHA256" >&2
    exit 2
}
expected=$1
case "$expected" in ""|*[!0123456789abcdef]*) echo "Expected lowercase SHA256 digest" >&2; exit 2;; esac
[ "$(printf '%s' "$expected" | wc -c)" -eq 64 ] || exit 2
fail() { echo "BLOCKED: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || fail "Root ADB is required"
[ "$(uname -r)" = 4.4.94-sonix ] || fail "Unexpected R1 kernel"
grep -Fxq NAME=PocketOS /etc/pocketos-release || fail "Independent PocketOS absent"
grep -Fxq TARGET=hiby-r1 /etc/pocketos-release || fail "Unexpected device"
awk '$1=="/dev/mmcblk0p1" && $2=="/mnt/sd_0" && $3=="vfat" &&
     $4 ~ /(^|,)noexec(,|$)/ {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Reviewed protected SD mount missing"
awk '$2=="/dev" && $3=="devtmpfs" {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Volatile devtmpfs unavailable"
awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Volatile /run unavailable"
grep -Eq '^[[:space:]]*squashfs$' /proc/filesystems || fail "SquashFS unavailable"
grep -Eq '^[[:space:]]*7[[:space:]]+loop$' /proc/devices || fail "Loop driver unavailable"
[ -c /dev/loop-control ] && [ ! -L /dev/loop-control ] ||
    fail "Loop control node unavailable"
sd=/mnt/sd_0/PocketOS/KOReaderRuntime
image=$sd/KOReader-R1-SD-RUNTIME.squashfs
point=/run/pocketos-reader-smoke
[ -d /mnt/sd_0/PocketOS ] && [ ! -L /mnt/sd_0/PocketOS ] ||
    fail "Unsafe SD folder"
[ -d "$sd" ] && [ ! -L "$sd" ] || fail "SD runtime folder missing"
[ -f "$image" ] && [ ! -L "$image" ] || fail "Expected KOReader image missing"
actual=$(sha256sum "$image" | awk '{print $1}') || fail "Cannot hash SD image"
[ "$actual" = "$expected" ] || fail "SD image SHA256 mismatch"
[ ! -e "$point" ] && [ ! -L "$point" ] ||
    fail "Temporary mountpoint already exists"
made_node=0
made_point=0
mounted_here() {
    awk -v m="$point" '$2==m {ok=1} END {exit !ok}' /proc/mounts
}
cleanup() {
    if [ "$made_point" -eq 1 ]; then
        if mounted_here; then
            umount "$point" || {
                echo "WARNING: probe mount still active; leaving device node alone" >&2
                return
            }
        fi
        rmdir "$point" 2>/dev/null || :
    fi
    if [ "$made_node" -eq 1 ] && ! mounted_here; then rm -f /dev/loop0; fi
}
trap 'cleanup' EXIT
trap 'exit 1' HUP INT TERM
if [ ! -e /dev/loop0 ] && [ ! -L /dev/loop0 ]; then
    mknod -m 0600 /dev/loop0 b 7 0 || fail "Unable to make volatile loop0"
    made_node=1
fi
[ -b /dev/loop0 ] && [ ! -L /dev/loop0 ] || fail "Unsafe loop0 node"
[ "$(busybox stat -c '%t:%T' /dev/loop0)" = 7:0 ] ||
    fail "Wrong loop0 major/minor"
[ "$(busybox losetup -f)" = /dev/loop0 ] ||
    fail "Loop0 already in use or unavailable"
mkdir -m 0700 "$point" || fail "Unable to make RAM mountpoint"
made_point=1
busybox mount -t squashfs -o ro,nosuid,nodev,loop,exec "$image" "$point" ||
    fail "Read-only SquashFS loop mount failed"
awk -v m="$point" '$2==m && $3=="squashfs" &&
     $4 ~ /(^|,)ro(,|$)/ {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Mount was not verified read-only"
[ -f "$point/reader.lua" ] && [ -x "$point/luajit" ] ||
    fail "Native KOReader entry points missing"
[ -f "$point/frontend/device/hibyr1/device.lua" ] ||
    fail "Native HiBy R1 backend missing"
plugins=$(find "$point/plugins" -maxdepth 1 -type d -name '*.koplugin' | wc -l)
[ "$plugins" -ge 20 ] || fail "Incomplete KOReader plugins"
"$point/luajit" -v || fail "Native MIPS LuaJIT failed on physical R1"
echo "PASS: SD runtime verified, mounted RO, $plugins plugins, MIPS LuaJIT launched"
echo "NOT TESTED: display/touch, firmware install or signing. DO NOT FLASH."
