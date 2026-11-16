#!/bin/sh
set -eu
bin=$1
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT HUP INT TERM
mkdir -p "$root/sys/class/power_supply/battery" "$root/sys/class/power_supply/usb" "$root/sys/class/backlight/panel" "$root/sys/power" "$root/sys/block/mmcblk1" "$root/dev/snd" "$root/dev/input"
printf 'Battery\n' > "$root/sys/class/power_supply/battery/type"
printf 'Discharging\n' > "$root/sys/class/power_supply/battery/status"
printf '18\n' > "$root/sys/class/power_supply/battery/capacity"
printf 'USB\n' > "$root/sys/class/power_supply/usb/type"
printf '0\n' > "$root/sys/class/power_supply/usb/online"
printf '100\n' > "$root/sys/class/backlight/panel/max_brightness"
printf '42\n' > "$root/sys/class/backlight/panel/actual_brightness"
printf 'freeze mem\n' > "$root/sys/power/state"
printf '1\n' > "$root/sys/block/mmcblk1/removable"
printf '10000\n' > "$root/sys/block/mmcblk1/size"
touch "$root/dev/snd/pcmC0D0p" "$root/dev/fb0" "$root/dev/input/event0"
out=$("$bin" --root "$root" --status)
for expected in battery_percent=18 external_power=0 audio_pcm_present=1 framebuffer_present=1 input_present=1 brightness=42 removable_sd_candidate=1 wifi_interface_present=0; do
    printf '%s\n' "$out" | grep -Fx "$expected"
done
"$bin" --root "$root" --policy 301 0 0 | grep -Fx recommend_suspend=1
"$bin" --root "$root" --policy 301 1 0 | grep -Fx recommend_suspend=0
"$bin" --root "$root" --policy 301 0 1 | grep -Fx recommend_suspend=0
"$bin" --root "$root" --policy 301 0 0 | grep -Fx recommend_saver=1
printf '1\n' > "$root/sys/class/power_supply/usb/online"
"$bin" --root "$root" --policy 301 0 0 | grep -Fx recommend_suspend=0
printf '0\n' > "$root/sys/class/power_supply/usb/online"
printf 'invalid\n' > "$root/sys/class/power_supply/battery/capacity"
"$bin" --root "$root" --status | grep -Fx battery_percent=-1
"$bin" --root "$root" --policy 301 0 0 | grep -Fx recommend_suspend=0
if "$bin" --root "$root" --apply; then exit 1; fi
test "$(cat "$root/sys/class/backlight/panel/actual_brightness")" = 42
