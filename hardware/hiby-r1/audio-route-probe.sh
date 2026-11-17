#!/bin/sh
# Phase 6 read-only R1 audio discovery. No mixer changes, radio activation or playback.
set -eu
printf 'PocketOS audio route diagnostic v1\n'
printf '\n[ALSA cards]\n'
if [ -r /proc/asound/cards ]; then cat /proc/asound/cards; else echo 'unavailable'; fi
printf '\n[ALSA PCM names]\n'
if [ -r /proc/asound/pcm ]; then cat /proc/asound/pcm; else echo 'unavailable'; fi
printf '\n[jack switch candidates]\n'
for p in /sys/class/switch/h2w/state /sys/class/switch/headphone/state /sys/class/extcon/extcon*/state; do
    if [ -r "$p" ] && [ ! -L "$p" ]; then
        printf '%s=' "$p"
        head -c 256 "$p" || :
        printf '\n'
    fi
done
printf '\n[input switch capability inventory]\n'
if [ -r /proc/bus/input/devices ]; then
    # Device names and switch bitmaps only; excludes unique ID, MAC and serial fields.
    awk '/^(N: Name=|B: SW=)/ {print}' /proc/bus/input/devices
fi
printf '\n[ALSA routing tools]\n'
for tool in aplay amixer bluetoothctl bluealsa-aplay; do
    if command -v "$tool" >/dev/null 2>&1; then printf '%s available\n' "$tool"; else printf '%s unavailable\n' "$tool"; fi
done
printf '\n[known route aliases]\n'
if command -v aplay >/dev/null 2>&1; then
    # Names only. Do not open PCM devices or play audio.
    aplay -L 2>/dev/null | awk '/^(default|pocketos_wired|pocketos_bluetooth)([[:space:]]|$)/ {print}' || :
fi
printf '\n[Bluetooth service evidence]\n'
if [ -d /run/dbus ]; then echo 'D-Bus runtime directory present'; else echo 'D-Bus runtime directory unavailable'; fi
if [ -d /run/bluealsa ]; then echo 'BlueALSA runtime directory present'; else echo 'BlueALSA runtime directory unavailable'; fi
printf '\nDiagnostic complete; no route was changed.\n'
