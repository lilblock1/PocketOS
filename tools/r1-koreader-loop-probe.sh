#!/bin/sh
# KOReader HiBy R1 pre-install hardware gate. NO NAND, OTA, recovery or GUI writes.
# --audit: read-only device information.
# <SHA256>: verified SD image read-only mount + native LuaJIT smoke, with cleanup.
set -eu
umask 077
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
if [ "$#" -eq 1 ] && [ "$1" = "--self-test" ]; then
    echo 'PocketOS KOReader R1 read-only hardware probe v1'
    exit 0
fi
fail() { echo "BLOCKED: $*" >&2; exit 1; }
[ "$#" -eq 1 ] || {
    echo "Usage: sh r1-koreader-loop-probe.sh --audit|EXPECTED_SD_IMAGE_SHA256" >&2
    exit 2
}
report_environment() {
    echo '=== KOReader R1 hardware inventory (READ-ONLY) ==='
    echo "Kernel: $(uname -r)"
    grep -E '^(NAME|TARGET|UPDATE_SEQUENCE)=' /etc/pocketos-release 2>/dev/null || :
    echo 'Filesystems:'
    grep -E '[[:space:]](squashfs|vfat|tmpfs|devtmpfs)$' /proc/filesystems || :
    echo 'Loop driver registration:'
    grep -E '^[[:space:]]*7[[:space:]]+loop$' /proc/devices || :
    echo 'SD / RAM mounts:'
    awk '$2=="/mnt/sd_0" || $2=="/run" || $2=="/dev" {print $2,$3,$4}' /proc/mounts
    echo 'Memory:'
    grep -E '^(MemTotal|MemAvailable|MemFree|SwapTotal):' /proc/meminfo || :
    echo 'Framebuffer:'
    [ -c /dev/fb0 ] && echo 'Character device /dev/fb0 present' || echo 'No /dev/fb0'
    for p in /sys/class/graphics/fb0/name /sys/class/graphics/fb0/bits_per_pixel \
             /sys/class/graphics/fb0/virtual_size; do
        if [ -r "$p" ]; then printf '%s: ' "$p"; cat "$p"; fi
    done
    echo 'Input devices (names/capabilities only; no touch events or identifiers):'
    if [ -r /proc/bus/input/devices ]; then
        grep -E '^[NHB]: ' /proc/bus/input/devices | sed -E 's/(Uniq|Phys)=.*/(omitted)/' || :
    fi
}
if [ "$1" = --audit ]; then
    report_environment
    echo 'AUDIT FINISHED; filesystem and boot image unchanged.'
    exit 0
fi
expected=$1
case "$expected" in ''|*[!0123456789abcdef]*)
    fail 'Supply a lowercase 64-character SHA256 from the verified GitHub report';;
esac
[ "$(printf '%s' "$expected" | wc -c | tr -d ' ')" -eq 64 ] || fail 'SHA256 must contain exactly 64 characters'
[ "$(id -u)" -eq 0 ] || fail 'Root USB ADB is needed for volatile loop mount'
[ "$(uname -r)" = 4.4.94-sonix ] || fail 'Unexpected kernel; do not test'
grep -Fxq NAME=PocketOS /etc/pocketos-release || fail 'Installed independent PocketOS missing'
grep -Fxq TARGET=hiby-r1 /etc/pocketos-release || fail 'Unexpected device'
[ -c /dev/loop-control ] && [ ! -L /dev/loop-control ] || fail 'Loop control node missing'
grep -Eq '^[[:space:]]*7[[:space:]]+loop$' /proc/devices || fail 'Loop driver missing'
grep -Eq '^[[:space:]]*squashfs$' /proc/filesystems || fail 'SquashFS kernel support missing'
awk '$2=="/dev" && $3=="devtmpfs" {ok=1} END{exit !ok}' /proc/mounts ||
    fail '/dev must be volatile devtmpfs'
awk '$2=="/run" && $3=="tmpfs" {ok=1} END{exit !ok}' /proc/mounts ||
    fail '/run must be volatile tmpfs'
awk '$2=="/mnt/sd_0" && $3=="vfat" &&
    $4 ~ /(^|,)noexec(,|$)/ {ok=1} END{exit !ok}' /proc/mounts ||
    fail 'FAT32 microSD must remain mounted noexec'
sd_dir=/mnt/sd_0/PocketOS/KOReaderRuntime
sd_image=$sd_dir/KOReader-R1-SD-RUNTIME.squashfs
mountpoint=/run/pocketos-koreader-readonly-probe
for d in /mnt/sd_0/PocketOS "$sd_dir"; do
    [ -d "$d" ] && [ ! -L "$d" ] || fail "Missing/unsafe SD folder: $d"
done
[ -f "$sd_image" ] && [ ! -L "$sd_image" ] ||
    fail 'KOReader-R1-SD-RUNTIME.squashfs missing from FAT32 card'
echo 'Verifying SD SquashFS SHA256 (read-only; may take a while on R1)...'
actual=$(sha256sum "$sd_image" | awk '{print $1}') || fail 'Unable to hash runtime'
[ "$actual" = "$expected" ] || fail 'SD runtime image digest differs; do not proceed'
[ ! -e "$mountpoint" ] && [ ! -L "$mountpoint" ] ||
    fail 'Temporary mountpoint occupied; refusing to disturb it'
made_loop=0
made_mount=0
mounted_here() { awk -v p="$mountpoint" '$2==p {ok=1} END{exit !ok}' /proc/mounts; }
cleanup() {
    if [ "$made_mount" -eq 1 ]; then
        if mounted_here; then
            umount "$mountpoint" || {
                echo 'WARNING: test mount could not be unmounted; inspect with ADB' >&2
                return
            }
        fi
        rmdir "$mountpoint" 2>/dev/null || :
    fi
    if [ "$made_loop" -eq 1 ] && ! mounted_here; then
        rm -f /dev/loop0
    fi
}
trap 'cleanup' EXIT
trap 'exit 1' HUP INT TERM
if [ ! -e /dev/loop0 ] && [ ! -L /dev/loop0 ]; then
    mknod -m 0600 /dev/loop0 b 7 0 || fail 'Cannot create temporary loop0'
    made_loop=1
fi
[ -b /dev/loop0 ] && [ ! -L /dev/loop0 ] || fail '/dev/loop0 not a block node'
[ "$(busybox stat -c '%t:%T' /dev/loop0)" = 7:0 ] || fail '/dev/loop0 major/minor incorrect'
[ "$(busybox losetup -f)" = /dev/loop0 ] || fail 'Loop0 is already in use'
mkdir -m 0700 "$mountpoint" || fail 'Unable to create temporary RAM mountpoint'
made_mount=1
busybox mount -t squashfs -o ro,nosuid,nodev,loop,exec "$sd_image" "$mountpoint" ||
    fail 'HiBy R1 could not mount read-only SquashFS over loop'
awk -v m="$mountpoint" '$2==m && $3=="squashfs" &&
    $4 ~ /(^|,)ro(,|$)/ {ok=1} END{exit !ok}' /proc/mounts ||
    fail 'Mount is not confirmed read-only SquashFS'
for file in reader.lua luajit frontend/device/hibyr1/device.lua \
            frontend/device.lua ffi/posix_h.lua; do
    [ -f "$mountpoint/$file" ] || fail "KOReader runtime missing $file"
done
[ -x "$mountpoint/luajit" ] || fail 'MIPS LuaJIT cannot be executed'
plugins=$(find "$mountpoint/plugins" -mindepth 1 -maxdepth 1 -type d -name '*.koplugin' | wc -l)
[ "$plugins" -ge 37 ] || fail 'KOReader original plugin tree incomplete (need 37+)'
[ ! -e "$mountpoint/plugins/zenos.koplugin" ] || fail 'ZenOS must not be in standard KOReader'
[ ! -e "$mountpoint/plugins/gzreader.koplugin" ] || fail 'GZreader must not be in standard KOReader'
"$mountpoint/luajit" -v || fail 'Native MIPS LuaJIT could not execute on physical R1'
echo "PASS: real R1 kernel mounted verified $actual SD SquashFS read-only, $plugins plugins, MIPS LuaJIT executed"
echo 'NOT TESTED: LCD graphics, actual touch input, full KOReader GUI, RAM pressure, boot/recovery/rollback. DO NOT FLASH.'
