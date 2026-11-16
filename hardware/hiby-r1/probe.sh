#!/bin/sh
# Run only on a working R1 using adb shell sh < hardware/hiby-r1/probe.sh.
# Read-only: no mounts, module loads, sysfs writes, services or firmware changes.
set -eu
printf 'PocketOS hardware baseline v2\n'
printf 'kernel='; uname -r
printf 'machine='; uname -m
printf '\n[memory KiB]\n'
awk '/^(MemTotal|MemAvailable|MemFree|Buffers|Cached|SwapTotal|SwapFree):/ {print}' /proc/meminfo
printf '\n[uptime seconds]\n'
awk '{print $1}' /proc/uptime
printf '\n[aggregate CPU counters]\n'
awk '$1=="cpu" {print; exit}' /proc/stat
printf '\n[loaded modules: name size refs]\n'
if [ -r /proc/modules ]; then awk '{print $1,$2,$3}' /proc/modules; fi
printf '\n[device availability]\n'
for p in /dev/fb0 /dev/input /dev/snd /dev/mmcblk0 /dev/mmcblk1 /dev/mtd /dev/ubi0 /sys/class/backlight /sys/class/power_supply /sys/class/net/wlan0 /sys/class/bluetooth /sys/class/udc; do
    if [ -e "$p" ]; then printf '%s present\n' "$p"; else printf '%s absent\n' "$p"; fi
done
printf '\n[recognized input device names only]\n'
if [ -r /proc/bus/input/devices ]; then awk '/^N: Name=/ {print}' /proc/bus/input/devices; fi
printf '\n[read-only battery/backlight fields]\n'
for p in /sys/class/power_supply/*/capacity /sys/class/power_supply/*/status /sys/class/power_supply/*/voltage_now /sys/class/backlight/*/max_brightness /sys/class/backlight/*/actual_brightness; do
    if [ -r "$p" ]; then printf '%s=' "$p"; cat "$p"; fi
done
printf '\n[framebuffer geometry]\n'
for p in /sys/class/graphics/fb0/virtual_size /sys/class/graphics/fb0/bits_per_pixel; do
    if [ -r "$p" ]; then printf '%s=' "$p"; cat "$p"; fi
done
printf '\n[ALSA cards]\n'
if [ -r /proc/asound/cards ]; then cat /proc/asound/cards; fi
printf '\n[filesystem types and selected mount points; no file contents]\n'
awk '$2=="/" || $2=="/usr/data" || $2=="/mnt/sd_0" || $2=="/tmp" {print $2,$3,$4}' /proc/mounts
printf '\n[power and CPU capability; current settings only]\n'
for p in /sys/power/state /sys/power/mem_sleep /sys/devices/system/cpu/cpufreq/policy*/scaling_available_governors /sys/devices/system/cpu/cpufreq/policy*/scaling_governor /sys/devices/system/cpu/cpufreq/policy*/scaling_cur_freq /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq /sys/devices/system/cpu/cpu0/cpuidle/state*/name /sys/devices/system/cpu/cpu0/cpuidle/state*/usage /sys/devices/system/cpu/cpu0/cpuidle/state*/time /sys/class/power_supply/*/type /sys/class/power_supply/*/current_now /sys/class/power_supply/*/temp /sys/class/power_supply/*/online; do
    if [ -r "$p" ]; then printf '%s=' "$p"; cat "$p" || printf 'unavailable\n'; fi
done
printf '\n[block device capacity and removable flag; no filesystem contents]\n'
for b in /sys/block/mmcblk[0-9] /sys/block/mtdblock[0-9]*; do
    if [ -d "$b" ]; then
        for key in size removable ro; do
            p="$b/$key"
            if [ -r "$p" ]; then printf '%s=' "$p"; cat "$p" || printf 'unavailable\n'; fi
        done
    fi
done
printf '\n[radio interfaces; no addresses or credentials]\n'
for p in /sys/class/net/*/operstate /sys/class/rfkill/rfkill*/type /sys/class/rfkill/rfkill*/soft /sys/class/rfkill/rfkill*/hard; do
    if [ -r "$p" ]; then printf '%s=' "$p"; cat "$p" || printf 'unavailable\n'; fi
done
printf '\nEnd of read-only baseline. No settings changed.\n'
