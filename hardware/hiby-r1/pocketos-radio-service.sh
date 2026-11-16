#!/bin/sh
# On-demand R1 services. No MTD, updater, boot-selection or recovery access.
set -eu
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
umask 077
[ "$(id -u)" = 0 ]
[ "$#" -eq 1 ] || exit 2
case "$1" in wifi-start|bluetooth-start) action=$1;; *) exit 2;; esac
grep -Fxq NAME=PocketOS /etc/pocketos-release
grep -Fxq TARGET=hiby-r1 /etc/pocketos-release
[ "$(uname -r)" = 4.4.94-sonix ]
[ "$(cat /sys/devices/platform/bcm_wlbt_power/chipvendor)" = 0x81 ]
awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
trusted(){
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    case "$(/usr/bin/pocketos-stat -c '%u:%a' "$1")" in 0:400|0:600) ;; *) return 1;; esac
}
trusted /etc/pocketos/radio-enable
grep -Fxq POCKETOS-R1-RADIO-1 /etc/pocketos/radio-enable
package=/usr/lib/pocketos/radio
if [ -e /etc/pocketos/radio-package ]; then
    trusted /etc/pocketos/radio-package
    read -r package extra < /etc/pocketos/radio-package
    [ -z "$extra" ]
    case "$package" in /run/pocketos-radio-test/package) ;; *) exit 1;; esac
fi
trusted /etc/pocketos/radio.sha256
[ -d "$package" ] && [ ! -L "$package" ]
sha256sum -c /etc/pocketos/radio.sha256 >/dev/null
state=/run/pocketos-radio
mkdir -p "$state"
[ ! -L "$state" ] && [ "$(/usr/bin/pocketos-stat -c '%u:%a' "$state")" = 0:700 ]
mkdir "$state/start.lock" || { echo 'Radio startup already active' >&2; exit 1; }
trap 'rmdir "$state/start.lock"' EXIT
trap 'exit 1' HUP INT TERM
alive(){
    [ -r "$1" ] || return 1
    read -r pid < "$1"
    case "$pid" in ''|*[!0-9]*) return 1;; esac
    [ -r "/proc/$pid/comm" ] && grep -Fxq "$2" "/proc/$pid/comm"
}
wifi_start(){
    wifi=/run/pocketos-wifi
    mkdir -p "$wifi"
    [ ! -L "$wifi" ] && [ "$(/usr/bin/pocketos-stat -c '%u:%a' "$wifi")" = 0:700 ]
    if ! /usr/bin/pocketos-timeout 2 wpa_cli -p "$wifi" -i wlan0 ping 2>/dev/null | grep -Fxq PONG; then
        [ ! -S /var/run/wpa_supplicant/wlan0 ]
        if [ ! -d /sys/class/net/wlan0 ]; then
            printf 1 > /sys/devices/platform/bcm_wlbt_power/wifi_power
            printf '%s' "$package/firmware" > /sys/module/firmware_class/parameters/path
            [ -d /sys/module/brcmutil ] || insmod "$package/modules/brcmutil.ko"
            [ -d /sys/module/brcmfmac ] || insmod "$package/modules/brcmfmac.ko" scan_assoc_time=20
        fi
        n=0
        while [ ! -d /sys/class/net/wlan0 ] && [ "$n" -lt 50 ]; do n=$((n+1)); sleep 0.1; done
        [ -d /sys/class/net/wlan0 ]
        ip link set wlan0 up
        [ ! -e "$wifi/wlan0" ] && [ ! -L "$wifi/wlan0" ]
        printf 'ctrl_interface=%s\nupdate_config=0\n' "$wifi" > "$wifi/wpa.conf"
        wpa_supplicant -B -P "$wifi/pid" -i wlan0 -D nl80211 -c "$wifi/wpa.conf" -f "$wifi/wpa.log"
    fi
    if ! alive "$wifi/monitor.pid" wpa_cli; then
        wpa_cli -B -P "$wifi/monitor.pid" -p "$wifi" -i wlan0 -a /usr/sbin/pocketos-network-event
    fi
    if ! alive "$wifi/dhcp.pid" udhcpc && wpa_cli -p "$wifi" -i wlan0 status | grep -Fxq wpa_state=COMPLETED; then
        /usr/sbin/pocketos-network-event wlan0 CONNECTED
    fi
}
bluetooth_start(){
    bt=/run/pocketos-bluetooth
    mkdir -p "$bt/storage" "$bt/lib/bluealsa"
    [ ! -L "$bt" ] && [ "$(/usr/bin/pocketos-stat -c '%u:%a' "$bt")" = 0:700 ]
    if [ ! -d /sys/class/bluetooth/hci0 ]; then
        # Refuse a second UART owner. A failed transport needs diagnosis,
        # rather than an automatic reset under an existing process.
        for p in /proc/[0-9]*/comm; do
            if grep -Fxq brcm_patchram_p "$p" 2>/dev/null; then echo 'UART transport already running' >&2; return 1; fi
        done
        [ "$(cat /sys/class/rfkill/rfkill0/type)" = bluetooth ]
        printf 1 > /sys/class/rfkill/rfkill0/state
        # Stable local address from the existing board identity; never logged.
        identity=$(sha256sum /proc/jz/efuse/efuse_chip_id); identity=${identity%% *}
        suffix=$(printf '%.6s' "$identity" | sed 's/../&:/g;s/:$//')
        address=02:50:4f:$suffix
        nohup setsid "$package/bin/brcm_patchram_plus" --enable_hci --baudrate 3000000 --no2bytes \
            --patchram "$package/firmware/bt/BCM4343A1_001.002.009.0122.0538.hcd" /dev/ttyS0 \
            --tosleep=50000 --use_baudrate_for_download --enable_lpm --bd_addr "$address" > "$bt/transport.log" 2>&1 </dev/null &
        echo "$!" > "$bt/transport.pid"
        n=0
        while [ ! -d /sys/class/bluetooth/hci0 ] && [ "$n" -lt 150 ]; do n=$((n+1)); sleep 0.1; done
        [ -d /sys/class/bluetooth/hci0 ] || return 1
    fi
    if ! awk '$2=="/var/lib/bluetooth" {ok=1} END {exit !ok}' /proc/mounts; then
        mount --bind "$bt/storage" /var/lib/bluetooth
    fi
    mkdir -p /tmp/dbus /run/dbus
    dbus-uuidgen --ensure=/var/lib/dbus/machine-id
    if [ ! -S /run/dbus/system_bus_socket ]; then dbus-daemon --system; fi
    if ! alive "$bt/bluez.pid" bluetoothd; then
        nohup setsid "$package/bin/bluetoothd" -n -C > "$bt/bluez.log" 2>&1 </dev/null &
        echo "$!" > "$bt/bluez.pid"
    fi
    n=0
    while ! /usr/bin/pocketos-timeout 2 bluetoothctl show 2>/dev/null | grep -q '^Controller '; do
        n=$((n+1)); [ "$n" -lt 10 ] || return 1; sleep 0.2
    done
    /usr/bin/pocketos-timeout 5 bluetoothctl power on >/dev/null
    /usr/bin/pocketos-timeout 5 bluetoothctl system-alias PocketOS >/dev/null
    if ! alive "$bt/bluealsa.pid" bluealsa; then
        LD_LIBRARY_PATH="$package/lib" nohup setsid "$package/bin/bluealsa" -p a2dp-source -c SBC > "$bt/bluealsa.log" 2>&1 </dev/null &
        echo "$!" > "$bt/bluealsa.pid"
    fi
    n=0
    until LD_LIBRARY_PATH="$package/lib" "$package/bin/bluealsa-cli" list-pcms >/dev/null 2>&1; do
        n=$((n+1)); [ "$n" -lt 30 ] || return 1; sleep 0.1
    done
}
case "$action" in wifi-start) wifi_start;; bluetooth-start) bluetooth_start;; esac
