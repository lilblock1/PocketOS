#!/bin/sh
# Development R1 only. Starts a verified Wi-Fi stack on explicit request.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
umask 077
[ "${1:-}" = start ] && [ "$#" -eq 1 ] || exit 2
[ "$(id -u)" = 0 ]
for token in NAME=PocketOS TARGET=hiby-r1 STATE=development-userspace-only FLASHABLE=no; do
    grep -Fxq "$token" /etc/pocketos-release
done
[ "$(uname -r)" = 4.4.94-sonix ]
grep -Fxq POCKETOS-R1-WIFI-DEV-1 /etc/pocketos/wifi-enable
awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ -d /sys/module/bcm_wlbt_power ]
# This manifest is placed in the read-only root by the offline staging tool.
[ -f /etc/pocketos/wifi.sha256 ] && [ ! -L /etc/pocketos/wifi.sha256 ]
sha256sum -c /etc/pocketos/wifi.sha256 >/dev/null
command -v wpa_supplicant >/dev/null
command -v wpa_cli >/dev/null
command -v ip >/dev/null
state=/run/pocketos-wifi
mkdir -p "$state"
[ ! -L "$state" ] && [ "$(/usr/bin/pocketos-stat -c '%u:%a' "$state")" = 0:700 ]
ready(){ [ -S "$state/wlan0" ] && timeout 2 wpa_cli -p "$state" -i wlan0 ping 2>/dev/null | grep -Fxq PONG; }
if ready; then exit 0; fi
mkdir "$state/start.lock" || exit 1
trap 'rmdir "$state/start.lock"' EXIT
trap 'exit 1' HUP INT TERM
if ready; then exit 0; fi
# Do not replace an unknown socket/process or reset a shared Bluetooth power rail.
[ ! -e "$state/wlan0" ] && [ ! -L "$state/wlan0" ] && [ ! -e "$state/pid" ]
if [ ! -d /sys/class/net/wlan0 ]; then
    [ -d /sys/module/brcmutil ] || insmod /module_driver/brcmutil.ko
    [ -d /sys/module/brcmfmac ] || insmod /module_driver/brcmfmac.ko
    n=0
    while [ ! -d /sys/class/net/wlan0 ] && [ "$n" -lt 8 ]; do n=$((n+1)); sleep 1; done
fi
[ -d /sys/class/net/wlan0 ] || { echo 'wlan0 absent; inspect kernel firmware/SDIO/rfkill status' >&2; exit 1; }
# Refuse an existing conventional supplicant socket rather than commandeering it.
[ ! -S /var/run/wpa_supplicant/wlan0 ]
ip link set dev wlan0 up
printf 'ctrl_interface=%s\nupdate_config=0\n' "$state" > "$state/wpa.conf"
# No saved password, DHCP daemon, or automatic network selection.
timeout 8 wpa_supplicant -B -P "$state/pid" -i wlan0 -D nl80211 -c "$state/wpa.conf" >/dev/null 2>&1
n=0
while [ "$n" -lt 4 ]; do
    if ready; then exit 0; fi
    n=$((n+1)); sleep 1
done
exit 1
