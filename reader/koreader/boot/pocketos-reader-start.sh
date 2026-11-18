#!/bin/sh
# PocketOS Reader Edition: KOReader is the *only* user-facing application.
# Do not replace the installed R1 service until MIPS KOReader, touch, LCD,
# storage, update signing and recovery pass hardware-specific tests.
set -eu
umask 077
export PATH=/usr/bin:/bin:/usr/sbin:/sbin

if [ "$#" -eq 1 ] && [ "$1" = "--self-test" ]; then
    echo "PocketOS Reader Edition: KOReader-only launcher v1"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2

# SD stays mounted noexec. The pinned immutable KOReader SquashFS is
# separately mounted read-only, only AFTER its exact checksum is checked.
# This avoids copying ~30 MiB of executable code into 64 MiB of R1 RAM.
ko=/run/pocketos-reader
sd_image=/mnt/sd_0/PocketOS/KOReaderRuntime/KOReader-R1-SD-RUNTIME.squashfs
manifest=/etc/pocketos/koreader-sd.sha256
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
[ -d /mnt/sd_0/PocketOS/KOReaderRuntime ] &&
    [ ! -L /mnt/sd_0/PocketOS/KOReaderRuntime ] ||
    fail "Unsafe SD runtime directory"
[ -f "$sd_image" ] && [ ! -L "$sd_image" ] ||
    fail "Pinned KOReader runtime image missing"
[ -f "$manifest" ] && [ ! -L "$manifest" ] ||
    fail "Trusted SD runtime hash missing"
# Authenticate the entire immutable disk image before executing anything.
sha256sum -c "$manifest" >/dev/null 2>&1 ||
    fail "KOReader runtime image hash mismatch"
# Existing first-boot service provides /run as tmpfs; it remains RAM-only.
[ -d /run ] && [ ! -L /run ] &&
    awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Safe /run tmpfs is unavailable"
[ ! -e "$ko" ] && [ ! -L "$ko" ] ||
    fail "Reader runtime mountpoint is already present"
mkdir -m 0700 "$ko" || fail "Cannot create reader mountpoint"
# Mount only the hash-verified SquashFS, never remount /mnt/sd_0 or NAND.
# Kernel loop support is NOT YET verified on physical HiBy R1 hardware.
mount -t squashfs -o ro,nosuid,nodev,loop,exec "$sd_image" "$ko" ||
    fail "Verified reader SquashFS loop mount unsupported"
awk '$2=="/run/pocketos-reader" && $3=="squashfs" &&
     $4 ~ /(^|,)ro(,|$)/ {ok=1} END {exit !ok}' /proc/mounts ||
    fail "Reader mount is not read-only SquashFS"
[ -f "$ko/reader.lua" ] && [ ! -L "$ko/reader.lua" ] || fail "KOReader entry point missing"
[ -x "$ko/luajit" ] && [ ! -L "$ko/luajit" ] || fail "MIPS LuaJIT runtime missing"
[ -f "$ko/frontend/device/hibyr1/device.lua" ] || fail "R1 KOReader backend missing"
[ -f "$ko/frontend/device.lua" ] || fail "KOReader dispatcher missing"
[ -d "$ko/plugins" ] || fail "Full KOReader plugin tree missing"
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
# Never remount/format SD or internal partitions; only the trusted RO loop image is mounted.
if [ -d /mnt/sd_0 ] && [ ! -L /mnt/sd_0 ] &&
   awk '$2=="/mnt/sd_0" {ok=1} END {exit !ok}' /proc/mounts &&
   [ -w /mnt/sd_0 ]; then
    state=/mnt/sd_0/KOReader
elif [ -d /run/pocketos-data ] && [ ! -L /run/pocketos-data ] &&
     awk '$2=="/run/pocketos-data" && $3=="ubifs" {ok=1} END {exit !ok}' /proc/mounts &&
     [ -w /run/pocketos-data ]; then
    state=/run/pocketos-data/KOReader
else
    fail "No safe writable state storage; refusing firmware writes"
fi
[ ! -L "$state" ] || fail "Unsafe settings directory symlink"
mkdir -p "$state"
[ -d "$state" ] && [ -w "$state" ] || fail "Settings directory not writable"
export KO_HOME="$state"
export STARDICT_DATA_DIR="$state/data/dict"

# Verified SD runtime contains genuine upstream KOReader and all its plugins.
# No custom home UI, music service or games service is invoked here.
cd "$ko"
if [ -d /mnt/sd_0/Books ] && [ ! -L /mnt/sd_0/Books ]; then
    exec ./luajit ./reader.lua /mnt/sd_0/Books
fi
exec ./luajit ./reader.lua
