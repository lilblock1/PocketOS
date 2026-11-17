#!/bin/sh
# PocketOS R1 opt-in boot guard. Requires microSD assets and a pinned hash.
# Only replaces the vendor's player launcher wrapper, not init or recovery.
# This script must be baked into an explicitly reviewed R1 test rootfs.
set -eu
ORIGINAL=/usr/bin/sonix_player.pocketos-fallback.sh
RUNTIME=/mnt/sd_0
BIN="$RUNTIME/PocketOS/bin/pocketos-shell"
MARKER="$RUNTIME/PocketOS-try-new-ui"
HASH=/etc/pocketos/boot-shell.sha256
fallback() { exec "$ORIGINAL"; }
[ -f "$ORIGINAL" ] && [ -x "$ORIGINAL" ] && [ ! -L "$ORIGINAL" ] || exit 1
[ "$(id -u)" = 0 ] || fallback
[ -c /dev/fb0 ] || fallback
[ -d "$RUNTIME" ] && [ ! -L "$RUNTIME" ] || fallback
awk '$2=="/mnt/sd_0" {found=1} END {exit !found}' /proc/mounts || fallback
[ -f "$MARKER" ] && [ ! -L "$MARKER" ] || fallback
[ "$(cat "$MARKER")" = "POCKETOS-R1-TRY-1" ] || fallback
[ -d "$RUNTIME/PocketOS" ] && [ ! -L "$RUNTIME/PocketOS" ] || fallback
[ -d "$RUNTIME/PocketOS/bin" ] && [ ! -L "$RUNTIME/PocketOS/bin" ] || fallback
[ -f "$BIN" ] && [ ! -L "$BIN" ] && [ -f "$HASH" ] && [ ! -L "$HASH" ] || fallback
[ "$(wc -c < "$BIN")" -le 4194304 ] || fallback
hash=$(cat "$HASH") || fallback
case "$hash" in
    *[!0-9a-f]*|'') fallback ;;
esac
[ "$(printf %s "$hash" | wc -c)" -eq 64 ] || fallback
actual=$(sha256sum "$BIN" | cut -c1-64) || fallback
[ "$actual" = "$hash" ] || fallback

# Never guess event indexes. A missing/ambiguous touchscreen falls back.
found=""
for name in /sys/class/input/event*/device/name; do
    [ -r "$name" ] || continue
    label=$(cat "$name") || continue
    case "$label" in
        *cst8xx*|*CST8XX*|*touch*|*Touch*) ;;
        *) continue ;;
    esac
    event=$(basename "$(dirname "$(dirname "$name")")")
    case "$event" in event[0-9]*) ;; *) continue ;; esac
    [ -c "/dev/input/$event" ] || continue
    [ -z "$found" ] || fallback
    found="/dev/input/$event"
done
[ -n "$found" ] || fallback

# SD may be mounted noexec. Copy the hash-checked executable to private
# temporary RAM storage; it is discarded after the UI exits.
umask 077
temp=$(mktemp -d /tmp/pocketos-r1.XXXXXX 2>/dev/null) || fallback
cleanup() { rm -f "$temp/pocketos-shell"; rmdir "$temp" 2>/dev/null || :; }
trap cleanup EXIT
cp "$BIN" "$temp/pocketos-shell" || fallback
chmod 700 "$temp/pocketos-shell" || fallback
[ "$(sha256sum "$temp/pocketos-shell" | cut -c1-64)" = "$hash" ] || fallback
"$temp/pocketos-shell" --boot-splash --fb "$found" </dev/null || :
fallback
