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

ko=/opt/koreader
probe=/usr/bin/pocketos-r1-touch-probe
fail() { printf 'PocketOS Reader: %s\n' "$*" >&2; exit 1; }

# No Sonix or PocketOS music/game launcher on ordinary boot. A missing
# dependency results in a clean failure, not a mystery second home screen.
[ -d "$ko" ] && [ ! -L "$ko" ] || fail "KOReader runtime missing"
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
# Do not mount, remount, format or alter partitions.
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

# Runtime contains the genuine upstream KOReader application and plugins.
# No custom home UI, music service or games service is invoked here.
cd "$ko"
if [ -d /mnt/sd_0/Books ] && [ ! -L /mnt/sd_0/Books ]; then
    exec ./luajit ./reader.lua /mnt/sd_0/Books
fi
exec ./luajit ./reader.lua
