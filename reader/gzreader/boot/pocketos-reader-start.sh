#!/bin/sh
# PocketOS Reader Edition: GZreader is the *only* user-facing application.
# Do not replace the installed R1 service until MIPS GZreader, touch, LCD,
# storage, update signing and recovery pass hardware-specific tests.
set -eu
umask 077
export PATH=/usr/bin:/bin:/usr/sbin:/sbin

if [ "$#" -eq 1 ] && [ "$1" = "--self-test" ]; then
    echo "PocketOS Reader Edition: GZreader-only launcher v1"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2

# SD stays mounted noexec. The pinned immutable GZreader SquashFS is
# separately mounted read-only, only AFTER its exact checksum is checked.
# This avoids copying ~30 MiB of executable code into 64 MiB of R1 RAM.
ko=/run/pocketos-reader
sd_image=/mnt/sd_0/PocketOS/GZreaderRuntime/GZreader-R1-SD-RUNTIME.squashfs
manifest=/etc/pocketos/gzreader-sd.sha256
probe=/usr/bin/pocketos-r1-touch-probe
fail() { printf 'PocketOS Reader: %s\n' "$*" >&2; exit 1; }

# No Sonix or PocketOS music/game launcher on ordinary boot. A missing
# dependency results in a clean failure, not a mystery second home screen.
# Refuse untrusted SD layouts and preserve its existing noexec policy.
[ -d /mnt/sd_0 ] && [ ! -L /mnt/sd_0 ] &&
    awk '$2=="/mnt/sd_0" && $3=="vfat" &&
         $4 ~ /(^|,)noexec(,|$)/ {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Protected SD card (vfat,noexec) is not mounted"
[ -d /mnt/sd_0/PocketOS ] && [ ! -L /mnt/sd_0/PocketOS ] ||
    fail "Unsafe SD root directory"
[ -d /mnt/sd_0/PocketOS/GZreaderRuntime ] &&
    [ ! -L /mnt/sd_0/PocketOS/GZreaderRuntime ] ||
    fail "Unsafe SD runtime directory"
[ -f "$sd_image" ] && [ ! -L "$sd_image" ] ||
    fail "Pinned GZreader runtime image missing"
[ -f "$manifest" ] && [ ! -L "$manifest" ] ||
    fail "Trusted SD runtime hash missing"
# Authenticate the entire immutable disk image before executing anything.
sha256sum -c "$manifest" >/dev/null 2>&1 ||
    fail "GZreader runtime image hash mismatch"
# Existing first-boot service provides /run as tmpfs; it remains RAM-only.
[ -d /run ] && [ ! -L /run ] &&
    awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Safe /run tmpfs is unavailable"
[ ! -e "$ko" ] && [ ! -L "$ko" ] ||
    fail "Reader runtime mountpoint is already present"
# Real R1 evidence: loop major 7 and /dev/loop-control exist, but
# /dev/loop0 may be absent. /dev is volatile devtmpfs; create only
# this one known, kernel-backed block node. Never modify NAND or SD.
[ -d /dev ] && [ ! -L /dev ] &&
    awk '$2=="/dev" && $3=="devtmpfs" {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Temporary /dev filesystem unavailable"
grep -Eq '^[[:space:]]*7[[:space:]]+loop$' /proc/devices ||
    fail "Kernel loop driver major 7 is missing"
[ -c /dev/loop-control ] && [ ! -L /dev/loop-control ] ||
    fail "Loop control node unavailable"
if [ ! -e /dev/loop0 ] && [ ! -L /dev/loop0 ]; then
    mknod -m 0600 /dev/loop0 b 7 0 ||
        fail "Cannot create volatile loop0 device node"
fi
[ -b /dev/loop0 ] && [ ! -L /dev/loop0 ] ||
    fail "Unsafe loop0 device node"
[ "$(busybox stat -c '%t:%T' /dev/loop0)" = "7:0" ] ||
    fail "Unexpected loop0 major/minor number"

mkdir -m 0700 "$ko" || fail "Cannot create reader mountpoint"
# The hash-verified SquashFS loop mount does not alter SD or NAND mounts.
# Kernel loop support is NOT YET verified on physical HiBy R1 hardware.
mount -t squashfs -o ro,nosuid,nodev,loop,exec "$sd_image" "$ko" ||
    fail "Verified reader SquashFS loop mount unsupported"
awk '$2=="/run/pocketos-reader" && $3=="squashfs" &&
     $4 ~ /(^|,)ro(,|$)/ {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Reader mount is not read-only SquashFS"
[ -f "$ko/reader.lua" ] && [ ! -L "$ko/reader.lua" ] || fail "GZreader entry point missing"
[ -x "$ko/luajit" ] && [ ! -L "$ko/luajit" ] || fail "MIPS LuaJIT runtime missing"
[ -f "$ko/frontend/device/hibyr1/device.lua" ] || fail "R1 GZreader backend missing"
[ -f "$ko/frontend/device.lua" ] || fail "GZreader dispatcher missing"
[ -d "$ko/plugins" ] || fail "GZreader plugin tree missing"
[ -f "$ko/plugins/gzreader.koplugin/main.lua" ] || fail "GZreader bookshelf plugin missing"
[ -f "$ko/plugins/zenos.koplugin/main.lua" ] || fail "GZreader ZenOS home UI missing"
[ -x "$probe" ] && [ ! -L "$probe" ] || fail "Touch detector missing"
[ -c /dev/fb0 ] || fail "R1 framebuffer missing"

touch=$("$probe") || fail "Touchscreen unavailable or ambiguous"
case "$touch" in
    /dev/input/event[0-9]*)
        event=$(basename "$touch")
        event=$(printf '%s' "$event" | sed 's/^event//')
        case "$event" in ''|*[!0-9]*) fail "Invalid touchscreen node";; esac
        ;;
    *) fail "Invalid touchscreen node";;
esac
[ -c "$touch" ] || fail "Touchscreen device node unavailable"
export KO_R1_TOUCH_DEV="$touch"
export KO_PLATFORM=hibyr1

# Use a writable data location for settings, reading progress, annotations
# and extra plugins. Never write to the immutable firmware partition.
# The only new mount is a read-only loop image; SD and internal partitions stay unchanged.
if [ -d /mnt/sd_0 ] && [ ! -L /mnt/sd_0 ] &&
   awk '$2=="/mnt/sd_0" {ok=1} END {exit !ok}' /proc/mounts &&
   [ -w /mnt/sd_0 ]; then
    state=/mnt/sd_0/GZreader
elif [ -d /run/pocketos-data ] && [ ! -L /run/pocketos-data ] &&
     awk '$2=="/run/pocketos-data" && $3=="ubifs" {ok=1} END {exit !ok}' /proc/mounts &&
     [ -w /run/pocketos-data ]; then
    state=/run/pocketos-data/GZreader
else
    fail "No safe writable state storage; refusing firmware writes"
fi
[ ! -L "$state" ] || fail "Unsafe settings directory symlink"
mkdir -p "$state"
[ -d "$state" ] && [ -w "$state" ] || fail "Settings directory not writable"
export KO_HOME="$state"
export STARDICT_DATA_DIR="$state/data/dict"

# Verified SD runtime contains genuine upstream GZreader and all its plugins.
# No custom home UI, music service or games service is invoked here.
cd "$ko"
# Never force the KOReader file-manager path: GZreader/ZenOS owns the
# initial home screen and presents the book library as the only visible UI.
exec ./luajit ./reader.lua
