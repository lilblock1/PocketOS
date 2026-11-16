#!/bin/sh
# wpa_cli action; only this device's Wi-Fi interface may obtain an address.
set -eu
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
[ "$#" -eq 2 ] && [ "$1" = wlan0 ] || exit 2
state=/run/pocketos-wifi
[ -d "$state" ] && [ ! -L "$state" ] || exit 1
case "$2" in
    CONNECTED)
        printf 'connecting\n' > "$state/connection"
        # Idempotent across repeated association events. A foreground process
        # in a private session renews its lease until explicitly stopped.
        if [ -r "$state/dhcp.pid" ]; then
            read -r pid < "$state/dhcp.pid"
            case "$pid" in ''|*[!0-9]*) exit 1;; esac
            if [ -r "/proc/$pid/comm" ] && grep -Fxq udhcpc "/proc/$pid/comm"; then kill -USR1 "$pid"; exit 0; fi
        fi
        nohup setsid udhcpc -f -i wlan0 -p "$state/dhcp.pid" -s /usr/sbin/pocketos-udhcpc > "$state/dhcp.log" 2>&1 </dev/null &
        ;;
    DISCONNECTED)
        printf 'disconnected\n' > "$state/connection"
        # The DHCP client remains responsible for renewal after reassociation.
        # Do not leave a usable address/default route on a disconnected link.
        ip addr flush dev wlan0 scope global
        ip route flush dev wlan0 || :
        ;;
    *) exit 0;;
esac
