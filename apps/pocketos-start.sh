#!/bin/sh
# PocketOS development-only shell supervisor; no vendor service, flash, or mount changes.
set -eu
umask 077
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
die(){ printf '%s\n' "$*" >&2; exit 1; }
fixture=0
root=/
shell=/usr/bin/pocketos-shell
if [ "$#" -eq 3 ] && [ "$1" = "--fixture" ]; then
    fixture=1;root=$2;shell=$3
    [ -d "$root" ] && [ ! -L "$root" ] && [ -x "$shell" ] || die "Invalid fixture"
elif [ "$#" -ne 0 ]; then die "No production arguments accepted"; fi
release="$root/etc/pocketos-release"
marker="$root/etc/pocketos/autostart"
run="$root/run"
[ -f "$release" ] && [ ! -L "$release" ] || die "Not PocketOS development userspace"
grep -Fxq 'NAME=PocketOS' "$release" &&
grep -Fxq 'TARGET=hiby-r1' "$release" &&
grep -Fxq 'STATE=development-userspace-only' "$release" &&
grep -Fxq 'FLASHABLE=no' "$release" || die "Wrong rootfs identity"
[ -f "$marker" ] && [ ! -L "$marker" ] || { echo "PocketOS autostart disabled"; exit 0; }
[ "$(wc -c < "$marker")" -le 64 ] || die "Invalid autostart marker"
read -r magic event extra < "$marker" || die "Invalid autostart marker"
[ "$magic" = POCKETOS-DEV-AUTOSTART-1 ] && [ -z "$extra" ] || die "Invalid autostart marker"
case "$event" in event[0-9]*) ;; *) die "Invalid input node" ;; esac
number=${event#event}
case "$number" in ''|*[!0-9]*) die "Invalid input node" ;; esac
[ "${#number}" -le 3 ] || die "Invalid input node"
[ -d "$run" ] && [ ! -L "$run" ] || die "No runtime directory"
if [ "$fixture" -eq 0 ]; then
    [ "$(id -u)" -eq 0 ] || die "Trusted startup service requires root"
    [ ! -L /etc/pocketos ] && [ "$(stat -c '%u:%a' /etc/pocketos)" = "0:755" ] || die "Unsafe configuration directory"
    case "$(stat -c '%u:%a' "$marker")" in 0:400|0:444|0:600|0:644) ;; *) die "Untrusted autostart marker";; esac
    awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || die "No /run tmpfs"
    [ "$(stat -c '%u:%a' /run)" = "0:755" ] || die "Unsafe runtime directory"
    [ -x "$shell" ] && [ -f "$shell" ] && [ ! -L "$shell" ] || die "Native shell missing"
    case "$(stat -c '%u:%a' "$shell")" in 0:500|0:555|0:700|0:755) ;; *) die "Unsafe shell";; esac
fi
state="$run/pocketos-ui"
[ ! -L "$state" ] || die "Unsafe state directory"
mkdir -p "$state"
[ "$(stat -c '%a' "$state")" = 700 ] || die "Unsafe state permissions"
if [ "$fixture" -eq 0 ]; then [ "$(stat -c '%u' "$state")" -eq 0 ] || die "Unsafe state owner"; fi
mkdir "$state/lock" 2>/dev/null || die "Supervisor already active or interrupted"
child=
status(){
    printf '%s\n' "$1" > "$state/.status.$$"
    mv -f "$state/.status.$$" "$state/status"
}
cleanup(){
    rm -f "$state/pid" "$state/.status.$$"
    rmdir "$state/lock" 2>/dev/null || :
}
stopping(){
    trap '' HUP INT TERM
    if [ -n "$child" ]; then
        kill -TERM "$child" 2>/dev/null || :
        wait "$child" 2>/dev/null || :
        child=
    fi
    status stopped
    exit 0
}
trap cleanup EXIT
trap stopping HUP INT TERM
printf '%s\n' "$$" > "$state/pid"
status waiting-for-display-and-input
if [ "$fixture" -eq 0 ]; then
    i=0
    while [ ! -c /dev/fb0 ] || [ ! -c "/dev/input/$event" ]; do
        i=$((i+1)); if [ "$i" -ge 15 ]; then status hardware-not-ready; exit 1; fi
        sleep 1
    done
fi
# One initial launch and at most two retries. No repeated splash, no hardware flags.
attempt=1
while [ "$attempt" -le 3 ]; do
    status "starting-$attempt"
    if [ "$attempt" -eq 1 ]; then
        "$shell" --boot-splash --fb "/dev/input/$event" </dev/null >/dev/null 2>&1 &
    else
        "$shell" --fb "/dev/input/$event" </dev/null >/dev/null 2>&1 &
    fi
    child=$!
    status "shell-launched-$attempt"
    code=0
    wait "$child" || code=$?
    child=
    if [ "$code" -eq 0 ]; then status shell-exited; exit 0; fi
    status "shell-exit-$code-attempt-$attempt"
    if [ "$attempt" -eq 3 ]; then status "recovery-required-exit-$code"; exit 1; fi
    attempt=$((attempt+1))
    if [ "$fixture" -eq 0 ]; then sleep 2; fi
done
