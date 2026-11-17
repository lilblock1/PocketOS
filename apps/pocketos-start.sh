#!/bin/sh
# PocketOS development-only shell supervisor; no vendor service, flash, or mount changes.
set -eu
umask 077
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
die(){ printf '%s\n' "$*" >&2; exit 1; }
fixture=0
stat_cmd=/usr/bin/pocketos-stat
root=/
shell=/usr/bin/pocketos-shell
if [ "$#" -eq 3 ] && [ "$1" = "--fixture" ]; then
    fixture=1;root=$2;shell=$3;stat_cmd=stat
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
valid_event(){
    case "$1" in event[0-9]*) ;; *) return 1;; esac
    number=${1#event}
    case "$number" in ''|*[!0-9]*) return 1;; esac
    [ "${#number}" -le 3 ]
}
[ "$event" = auto ] || valid_event "$event" || die "Invalid input node"
# Auto mode is restricted to the physically observed touchscreen name. Never
# select the first input device or guess between two matching devices.
find_touch(){
    match=
    for node in "$root"/sys/class/input/event*; do
        [ -r "$node/device/name" ] || continue
        name=$(cat "$node/device/name") || continue
        [ "$name" = hyn_ts ] || continue
        candidate=${node##*/}
        valid_event "$candidate" || continue
        [ -z "$match" ] || return 1
        match=$candidate
    done
    [ -n "$match" ] || return 1
    if [ "$fixture" -eq 0 ]; then [ -c "/dev/input/$match" ] || return 1; fi
    printf '%s\n' "$match"
}
[ -d "$run" ] && [ ! -L "$run" ] || die "No runtime directory"
if [ "$fixture" -eq 0 ]; then
    [ "$(id -u)" -eq 0 ] || die "Trusted startup service requires root"
    [ -x "$stat_cmd" ] || die "PocketOS metadata helper missing"
    [ ! -L /etc/pocketos ] && [ "$("$stat_cmd" -c '%u:%a' /etc/pocketos)" = "0:755" ] || die "Unsafe configuration directory"
    case "$("$stat_cmd" -c '%u:%a' "$marker")" in 0:400|0:444|0:600|0:644) ;; *) die "Untrusted autostart marker";; esac
    awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || die "No /run tmpfs"
    [ "$("$stat_cmd" -c '%u:%a' /run)" = "0:755" ] || die "Unsafe runtime directory"
    [ -x "$shell" ] && [ -f "$shell" ] && [ ! -L "$shell" ] || die "Native shell missing"
    case "$("$stat_cmd" -c '%u:%a' "$shell")" in 0:500|0:555|0:700|0:755) ;; *) die "Unsafe shell";; esac
fi
state="$run/pocketos-ui"
feature_args=
# The installed, read-only system chooses the approved Classic interface.
# This marker enables no hardware, storage, radio or update permissions.
classic_marker="$root/etc/pocketos/classic-ui-enable"
if [ -e "$classic_marker" ] || [ -L "$classic_marker" ]; then
    [ -f "$classic_marker" ] && [ ! -L "$classic_marker" ] || die "Unsafe Classic UI marker"
    [ "$(wc -c < "$classic_marker")" -eq 22 ] &&
    grep -Fxq POCKETOS-R1-CLASSIC-1 "$classic_marker" || die "Invalid Classic UI marker"
    if [ "$fixture" -eq 0 ]; then
        case "$("$stat_cmd" -c '%u:%a' "$classic_marker")" in 0:400|0:444|0:600|0:644) ;; *) die "Untrusted Classic UI marker";; esac
    fi
    feature_args=--classic-ui
fi
features="$root/etc/pocketos/features"
if [ -e "$features" ] || [ -L "$features" ]; then
    [ -f "$features" ] && [ ! -L "$features" ] || die "Unsafe feature configuration"
    [ "$(wc -c < "$features")" -eq 24 ] && grep -Fxq POCKETOS-DEV-FEATURES-1 "$features" || die "Invalid feature configuration"
    if [ "$fixture" -eq 0 ]; then
        case "$("$stat_cmd" -c '%u:%a' "$features")" in 0:400|0:600) ;; *) die "Untrusted feature configuration";; esac
    fi
    feature_args="$feature_args --allow-audio --allow-bookmarks --allow-book-preparation --allow-radio --allow-backlight"
fi
# Playback can be approved without unlocking unrelated development features.
audio_marker="$root/etc/pocketos/audio-enable"
if [ -e "$audio_marker" ] || [ -L "$audio_marker" ]; then
    [ -f "$audio_marker" ] && [ ! -L "$audio_marker" ] || die "Unsafe audio marker"
    [ "$(wc -c < "$audio_marker")" -eq 20 ] && grep -Fxq POCKETOS-R1-AUDIO-1 "$audio_marker" || die "Invalid audio marker"
    if [ "$fixture" -eq 0 ]; then
        case "$("$stat_cmd" -c '%u:%a' "$audio_marker")" in 0:400|0:600) ;; *) die "Untrusted audio marker";; esac
    fi
    case " $feature_args " in *' --allow-audio '*) ;; *) feature_args="$feature_args --allow-audio";; esac
fi
# Book preparation/resume can be enabled without unrelated development actions.
books_marker="$root/etc/pocketos/books-enable"
if [ -e "$books_marker" ] || [ -L "$books_marker" ]; then
    [ -f "$books_marker" ] && [ ! -L "$books_marker" ] || die "Unsafe books marker"
    [ "$(wc -c < "$books_marker")" -eq 20 ] && grep -Fxq POCKETOS-R1-BOOKS-1 "$books_marker" || die "Invalid books marker"
    if [ "$fixture" -eq 0 ]; then
        case "$("$stat_cmd" -c '%u:%a' "$books_marker")" in 0:400|0:600) ;; *) die "Untrusted books marker";; esac
    fi
    feature_args="$feature_args --allow-bookmarks --allow-book-preparation"
fi
# Radio controls can be enabled independently of broad development features.
radio_marker="$root/etc/pocketos/radio-enable"
if [ -e "$radio_marker" ] || [ -L "$radio_marker" ]; then
    [ -f "$radio_marker" ] && [ ! -L "$radio_marker" ] || die "Unsafe radio marker"
    [ "$(wc -c < "$radio_marker")" -eq 20 ] && grep -Fxq POCKETOS-R1-RADIO-1 "$radio_marker" || die "Invalid radio marker"
    if [ "$fixture" -eq 0 ]; then
        case "$("$stat_cmd" -c '%u:%a' "$radio_marker")" in 0:400|0:600) ;; *) die "Untrusted radio marker";; esac
    fi
    case " $feature_args " in *' --allow-radio '*) ;; *) feature_args="$feature_args --allow-radio";; esac
fi
# Kernel suspend failed physical testing. Only software Smart Sleep is supported.
smart_marker="$root/etc/pocketos/smart-sleep-enable"
if [ -e "$smart_marker" ] || [ -L "$smart_marker" ]; then
    [ -f "$smart_marker" ] && [ ! -L "$smart_marker" ] || die "Unsafe Smart Sleep marker"
    [ "$(wc -c < "$smart_marker")" -eq 26 ] && grep -Fxq POCKETOS-R1-SMART-SLEEP-1 "$smart_marker" || die "Invalid Smart Sleep marker"
    if [ "$fixture" -eq 0 ]; then
        case "$("$stat_cmd" -c '%u:%a' "$smart_marker")" in 0:400|0:600) ;; *) die "Untrusted Smart Sleep marker";; esac
    fi
    feature_args="$feature_args --allow-smart-sleep --allow-idle-dim"
    case " $feature_args " in *' --allow-backlight '*) ;; *) feature_args="$feature_args --allow-backlight";; esac
fi
# File management uses the independent, trusted SD-write approval. Its
# descriptor-relative operations still limit writes to user files and folders.
sd_marker="$root/etc/pocketos/sd-write-enable"
if [ -f "$sd_marker" ] && [ ! -L "$sd_marker" ] &&
   [ "$(wc -c < "$sd_marker")" -eq 23 ] &&
   grep -Fxq POCKETOS-R1-SD-WRITE-1 "$sd_marker"; then
    if [ "$fixture" -eq 1 ] || [ "$("$stat_cmd" -c '%u:%a' "$sd_marker")" = 0:400 ]; then
        feature_args="$feature_args --allow-file-writes"
    fi
fi
# Signed update staging is separately approved. Missing/invalid trust must not
# prevent the rest of PocketOS from booting. No flash installer is enabled.
update_marker="$root/etc/pocketos/update-enable"
update_key="$root/etc/pocketos/update-public.pem"
if [ -e "$update_marker" ] || [ -L "$update_marker" ]; then
    update_ok=0
    if [ -f "$update_marker" ] && [ ! -L "$update_marker" ] &&
       [ "$(wc -c < "$update_marker")" -eq 22 ] &&
       grep -Fxq POCKETOS-R1-UPDATES-1 "$update_marker" &&
       [ -f "$update_key" ] && [ ! -L "$update_key" ]; then
        if [ "$fixture" -eq 1 ]; then
            update_ok=1
        else
            case "$("$stat_cmd" -c '%u:%a' "$update_marker"):$("$stat_cmd" -c '%u:%a' "$update_key")" in
                0:400:0:400|0:400:0:444|0:600:0:400|0:600:0:444) update_ok=1 ;;
            esac
        fi
        if [ "$update_ok" -eq 1 ] &&
           command -v openssl >/dev/null && command -v sha256sum >/dev/null &&
           command -v curl >/dev/null &&
           openssl pkey -pubin -in "$update_key" -noout >/dev/null 2>&1; then
            feature_args="$feature_args --allow-updates"
        else
            echo 'Signed update staging locked: invalid trust or missing runtime'
        fi
    else
        echo 'Signed update staging locked: invalid approval or missing key'
    fi
fi
[ ! -L "$state" ] || die "Unsafe state directory"
mkdir -p "$state"
[ "$("$stat_cmd" -c '%a' "$state")" = 700 ] || die "Unsafe state permissions"
if [ "$fixture" -eq 0 ]; then [ "$("$stat_cmd" -c '%u' "$state")" -eq 0 ] || die "Unsafe state owner"; fi
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
ready(){
    if [ "$event" = auto ]; then
        resolved=$(find_touch) || return 1
    else resolved=$event; fi
    if [ "$fixture" -eq 0 ]; then
        [ -c /dev/fb0 ] && [ -c "/dev/input/$resolved" ] || return 1
    fi
    return 0
}
i=0
while ! ready; do
    i=$((i+1))
    if [ "$fixture" -eq 1 ] || [ "$i" -ge 15 ]; then status hardware-not-ready; exit 1; fi
    sleep 1
done
event=$resolved
config_args=
if [ "$fixture" -eq 0 ] &&
   awk '$2=="/run/pocketos-data" && $3=="ubifs" {ok=1} END {exit !ok}' /proc/mounts &&
   [ ! -L /run/pocketos-data/PocketOS ] &&
   [ "$("$stat_cmd" -c '%u:%a' /run/pocketos-data/PocketOS)" = 0:700 ]; then
    config_args='--config /run/pocketos-data/PocketOS/preferences'
fi
printf '%s\n' "$event" > "$state/input"
# Start only if protected persistent configuration contains an enabled saved
# network. Explicit disconnect disables profiles; no radio start for empty or
# disabled configurations. Do not expose credentials or delay UI startup.
saved_wifi="$root/run/pocketos-data/PocketOS/wpa.conf"
if [ "$fixture" -eq 0 ] && [ -n "$config_args" ] &&
   [ -f "$saved_wifi" ] && [ ! -L "$saved_wifi" ] &&
   [ "$("$stat_cmd" -c '%u:%a' "$saved_wifi")" = 0:600 ] &&
   [ "$(wc -c < "$saved_wifi")" -le 65536 ] &&
   awk '
     /^[[:space:]]*network[[:space:]]*=[[:space:]]*\{/ {inside=1; disabled=0; next}
     inside && /^[[:space:]]*disabled[[:space:]]*=/ {split($0,a,"=");gsub(/[[:space:]]/,"",a[2]);disabled=(a[2]!="0")}
     inside && /^[[:space:]]*\}/ {if(!disabled)enabled=1;inside=0}
     END {exit !enabled}
   ' "$saved_wifi" &&
   [ -x /usr/sbin/pocketos-radio-service ]; then
    case " $feature_args " in
        *' --allow-radio '*) /usr/sbin/pocketos-radio-service wifi-start </dev/null >"$state/wifi-start.log" 2>&1 & ;;
    esac
fi
# Provisioned digest is independent of the writable SD card. Decoder failure
# does not prevent native WAV playback or the shell from launching.
if [ "$fixture" -eq 0 ] && [ -f "$root/etc/pocketos/decoder.sha256" ] && [ ! -L "$root/etc/pocketos/decoder.sha256" ] && [ -x /usr/bin/pocketos-decoder ]; then
    /usr/bin/pocketos-decoder ensure > "$state/decoder.log" 2>&1 || :
fi
# One initial launch and at most two retries. Explicit display activation at cold boot.
attempt=1
while [ "$attempt" -le 3 ]; do
    status "starting-$attempt"
    if [ "$attempt" -eq 1 ]; then
        "$shell" $config_args --boot-splash --unblank-display $feature_args --fb "/dev/input/$event" </dev/null >"$state/shell.log" 2>&1 &
    else
        "$shell" $config_args --unblank-display $feature_args --fb "/dev/input/$event" </dev/null >"$state/shell.log" 2>&1 &
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
