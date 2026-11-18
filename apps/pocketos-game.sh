#!/bin/sh
# Optional SD game, pinned by the immutable system. Never executes SD directly.
set -eu
umask 077
[ "$#" -eq 1 ] && [ "$1" = doom ] || exit 2
game=/mnt/sd_0/PocketOS/Games/doom
for path in /mnt/sd_0 /mnt/sd_0/PocketOS /mnt/sd_0/PocketOS/Games "$game"; do
    [ -d "$path" ] && [ ! -L "$path" ] || exit 10
done
awk '$1=="/dev/mmcblk0p1" && $2=="/mnt/sd_0" && $3=="vfat" {ok=1} END {exit !ok}' /proc/mounts || exit 10
expected=
dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$dir" in
    /tmp/pocketos-design-*) expected=${POCKETOS_DOOM_REVIEW_SHA256:-} ;;
    /usr/bin)
        trust=/etc/pocketos/doom.sha256
        [ -f "$trust" ] && [ ! -L "$trust" ] || exit 11
        [ "$(/usr/bin/pocketos-stat -c '%u:%a' "$trust")" = 0:400 ] || exit 11
        [ "$(wc -c < "$trust")" -eq 65 ] || exit 11
        read -r expected < "$trust" ;;
    *) exit 11;;
esac
case "$expected" in ''|*[!0-9a-f]*) exit 11;; esac
[ "${#expected}" -eq 64 ] || exit 11
[ -f "$game/pocketos-doom" ] && [ ! -L "$game/pocketos-doom" ] || exit 10
[ -f "$game/doom1.wad" ] && [ ! -L "$game/doom1.wad" ] || exit 10
[ "$(sha256sum "$game/pocketos-doom" | awk '{print $1}')" = "$expected" ] || exit 11
# Exact original shareware game data chosen for this first port.
[ "$(sha256sum "$game/doom1.wad" | awk '{print $1}')" = 1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771 ] || exit 12
awk '$2=="/tmp" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || exit 11
[ ! -L "$game/data" ] || exit 10
mkdir -p "$game/data"
run=$(mktemp -d /tmp/pocketos-game.XXXXXX)
child=
finish(){
    code=$?
    trap '' HUP INT TERM
    trap - EXIT
    if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :;wait "$child" 2>/dev/null || :;fi
    case "$run" in /tmp/pocketos-game.*) rm -f "$run/pocketos-doom";rmdir "$run";; *) exit 11;; esac
    exit "$code"
}
trap finish EXIT
trap 'exit 0' HUP INT TERM
cp "$game/pocketos-doom" "$run/pocketos-doom"
chmod 500 "$run/pocketos-doom"
[ "$(sha256sum "$run/pocketos-doom" | awk '{print $1}')" = "$expected" ] || exit 11
cd "$game/data"
HOME="$game/data" "$run/pocketos-doom" -iwad "$game/doom1.wad" -config "$game/data/default.cfg" -nosound -nomusic -mb 6 </dev/null &
child=$!
code=0
wait "$child" || code=$?
child=
exit "$code"
