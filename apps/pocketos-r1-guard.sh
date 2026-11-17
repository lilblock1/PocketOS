#!/bin/sh
# PocketOS R1 opt-in boot guard for offline reference compatibility tests.
# Never install without verified stock recovery and real R1 testing.
# Hooked only in place of the Sonix player wrapper, after vendor initialization.
set -eu
ORIGINAL=/usr/bin/sonix_player.pocketos-fallback.sh
SHELL=/usr/bin/pocketos-shell
SD=/mnt/sd_0
MARKER="$SD/PocketOS-try-new-ui"
fallback() { exec "$ORIGINAL"; }
[ -x "$ORIGINAL" ] && [ -f "$ORIGINAL" ] && [ ! -L "$ORIGINAL" ] || exit 1
[ -x "$SHELL" ] && [ -f "$SHELL" ] && [ ! -L "$SHELL" ] || fallback
[ "$(id -u)" -eq 0 ] || fallback
[ -c /dev/fb0 ] || fallback
[ -d "$SD" ] && [ ! -L "$SD" ] || fallback
awk '$2=="/mnt/sd_0" {found=1} END {exit !found}' /proc/mounts || fallback
[ -f "$MARKER" ] && [ ! -L "$MARKER" ] || fallback
[ "$(wc -c < "$MARKER")" -le 40 ] || fallback
[ "$(cat "$MARKER")" = "POCKETOS-R1-TRY-1" ] || fallback

# Resolve R1 touch input by sysfs name instead of guessing event0 or event1.
# Unknown and ambiguous devices leave the stock player functional.
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
    if [ -n "$found" ]; then fallback; fi
    found="/dev/input/$event"
done
[ -n "$found" ] || fallback

# PocketOS is foreground. Exits/crashes immediately return to the vendor player.
# Removing SD or marker restores the original boot on the next restart.
# No kernel module, mount, flash, persistent service or reboot operations.
"$SHELL" --allow-transfer --boot-splash --fb "$found" </dev/null || :
fallback
