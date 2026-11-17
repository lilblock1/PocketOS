#!/bin/sh
# RAM-only visual review. Never replaces the installed application.
set -eu
umask 077
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$dir" in /tmp/pocketos-design-*) ;; *) exit 2;; esac
pid=${1:-}
seconds=${2:-600}
case "$pid" in ''|*[!0-9]*) exit 2;; esac
case "$seconds" in ''|*[!0-9]*) exit 2;; esac
mode=${3:-scenes}
case "$mode" in scenes|interactive) ;; *) exit 2;; esac
[ "$pid" -gt 1 ]
[ "$seconds" -ge 3 ] && [ "$seconds" -le 600 ]
awk '$2=="/tmp" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ "$(readlink "/proc/$pid/exe")" = /usr/bin/pocketos-shell ]
[ "$(awk '/^State:/ {print $2}' "/proc/$pid/status")" != T ]
identity=$(awk '{print $22}' "/proc/$pid/stat")
if [ "$mode" = interactive ]; then
    [ -x "$dir/pocketos-shell" ]
    # Do not leave the old decoder active underneath the temporary UI.
    if pidof pocketos-music >/dev/null 2>&1; then echo 'Stop music before an interactive UI test' >&2;exit 1;fi
    cp /run/pocketos-data/PocketOS/preferences "$dir/preferences"
    chmod 600 "$dir/preferences"
fi
light=/sys/class/backlight/backlight_pwm0
old_light=$(cat "$light/brightness")
case "$old_light" in ''|*[!0-9]*) exit 2;; esac
"$dir/r1-display" inspect > "$dir/display.txt"
child= timer= stopped=0 snapshot=0
same_ui(){
    [ "$(readlink "/proc/$pid/exe" 2>/dev/null)" = /usr/bin/pocketos-shell ] &&
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
    [ -z "$timer" ] || kill "$timer" 2>/dev/null
    restored=0
    if [ "$snapshot" -eq 1 ]; then "$dir/r1-display" restore "$dir/original.fb" || restored=1; fi
    printf '%s\n' "$old_light" > "$light/brightness"
    if [ "$stopped" -eq 1 ]; then
        if same_ui; then kill -CONT "$pid" || restored=1; else restored=1; fi
    fi
    if [ "$restored" -eq 0 ]; then echo restored-pocketos > "$dir/status"; else echo restore-needs-attention > "$dir/status"; fi
    exit "$code"
}
trap 'finish $?' EXIT
trap 'finish 129' HUP
trap 'finish 130' INT
trap 'finish 0' TERM
echo "$$" > "$dir/session.pid"
(sleep "$seconds"; kill -TERM "$$") </dev/null >/dev/null 2>&1 &
timer=$!
stopped=1
kill -STOP "$pid"
i=0
while [ "$(awk '/^State:/ {print $2}' "/proc/$pid/status")" != T ]; do
    i=$((i+1));[ "$i" -lt 20 ];sleep 0.05
done
"$dir/r1-display" save "$dir/original.fb"
snapshot=1
"$dir/r1-display" unblank
if [ "$old_light" -lt 30 ]; then printf '35\n' > "$light/brightness"; fi
if [ "$mode" = interactive ]; then
    "$dir/pocketos-shell" --config "$dir/preferences" --classic-ui --allow-audio --allow-backlight --exclusive-input --trace-touch --unblank-display --fb /dev/input/event1 </dev/null > "$dir/viewer.log" 2>&1 &
else
    "$dir/r1-design-preview" --ram-preview "$dir/home.rgb565" "$dir/music.rgb565" "$dir/player.rgb565" "$dir/books.rgb565" "$dir/reader-classic.rgb565" "$dir/reader-classic-settings.rgb565" "$dir/settings.rgb565" </dev/null > "$dir/viewer.log" 2>&1 &
fi
child=$!
echo "$child" > "$dir/viewer.pid"
echo running > "$dir/status"
wait "$child"
