#!/bin/sh
# RAM-only, bounded UI handoff. Keep the existing player alive for its launcher.
set -eu
umask 077
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$dir" in /tmp/pocketos-ui-*) ;; *) echo 'Requires a new /tmp/pocketos-ui-* directory' >&2; exit 2;; esac
pid=${1:-}
seconds=${2:-300}
case "$pid:$seconds" in *[!0-9:]*|:*|*:) exit 2;; esac
[ "$pid" -gt 1 ]
# Zero is an explicit operator-requested session until exit or reboot.
if [ "$seconds" -ne 0 ]; then [ "$seconds" -ge 10 ] && [ "$seconds" -le 600 ]; fi
awk '$2=="/tmp" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ "$(readlink "/proc/$pid/exe")" = /usr/bin/sonix_player ]
[ "$(awk '/^State:/ {print $2}' "/proc/$pid/status")" != T ]
identity=$(awk '{print $22}' "/proc/$pid/stat")
child=
timer=
stopped=0
snapshot=0
light=/sys/class/backlight/backlight_pwm0
old_light=$(cat "$light/brightness")
max_light=$(cat "$light/max_brightness")
case "$old_light:$max_light" in *[!0-9:]*|:*|*:) exit 2;; esac
[ "$old_light" -le "$max_light" ] && [ "$max_light" -eq 101 ]
same_player(){
    [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = /usr/bin/sonix_player ] &&
    [ "$(awk '{print $22}' "/proc/$pid/stat" 2>/dev/null)" = "$identity" ]
}
finish(){
    code=$1
    trap '' HUP INT TERM
    trap - EXIT
    set +e
    if [ -n "$child" ]; then
        kill -TERM "$child" 2>/dev/null
        sleep 1
        kill -KILL "$child" 2>/dev/null
        wait "$child" 2>/dev/null
    fi
    if [ -n "$timer" ]; then kill "$timer" 2>/dev/null; fi
    restore=0
    if [ "$snapshot" -eq 1 ]; then "$dir/r1-display" restore "$dir/original.fb" || restore=1; fi
    printf '%s\n' "$old_light" > "$light/brightness"
    if [ "$stopped" -eq 1 ] && same_player; then kill -CONT "$pid" || restore=1; fi
    if [ "$restore" -eq 0 ]; then echo restored-sonix > "$dir/status"; else echo restore-needs-attention > "$dir/status"; fi
    echo "Session finished: exit=$code restore_error=$restore"
    exit "$code"
}
trap 'finish $?' EXIT
trap 'finish 129' HUP
trap 'finish 130' INT
trap 'finish 0' TERM
# Set the flag first so cleanup also resumes after an interrupted STOP.
stopped=1
kill -STOP "$pid"
i=0
while [ "$(awk '/^State:/ {print $2}' "/proc/$pid/status")" != T ]; do
    i=$((i+1));[ "$i" -lt 20 ];sleep 0.05
done
"$dir/r1-display" save "$dir/original.fb"
snapshot=1
# A readable display only during the test; restore the exact prior value.
if [ "$old_light" -lt 30 ]; then printf '60\n' > "$light/brightness"; fi
echo "$$" > "$dir/session.pid"
if [ "$seconds" -gt 0 ]; then
    (sleep "$seconds"; kill -TERM "$$") </dev/null >/dev/null 2>&1 &
    timer=$!
    echo "Temporary PocketOS UI started for at most $seconds seconds; Sonix PID $pid is paused."
else
    echo "PocketOS UI remains active until exit or reboot; Sonix PID $pid is paused."
fi
while :; do
    "$dir/pocketos-shell" --trace-touch --exclusive-input --fb /dev/input/event1 </dev/null >> "$dir/ui.log" 2>&1 &
    child=$!
    echo "$child" > "$dir/ui.pid"
    echo running > "$dir/status"
    code=0
    wait "$child" || code=$?
    child=
    # Host verifies the staged file's SHA-256 before requesting replacement.
    # Keep the original display snapshot, Sonix STOP and deadline in place.
    if [ "$code" -eq 0 ] && [ -f "$dir/reload.request" ] && [ ! -L "$dir/reload.request" ] &&
       [ -f "$dir/pocketos-shell.next" ] && [ ! -L "$dir/pocketos-shell.next" ] && [ -x "$dir/pocketos-shell.next" ]; then
        rm -f "$dir/reload.request"
        mv "$dir/pocketos-shell.next" "$dir/pocketos-shell"
        echo reloading > "$dir/status"
    else
        exit "$code"
    fi
done
