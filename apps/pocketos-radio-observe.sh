#!/bin/sh
# Read-only observations for Phase 6 radio policy. Never start, stop or power radios.
# Deliberately does not emit SSIDs, IP addresses, MACs or other connection details.
set -eu
[ "$#" -eq 0 ] || exit 2
wifi=unknown
bt=unknown
# A successful wpa_cli exchange is mandatory. Missing/ambiguous status
# must never be interpreted as idle.
if command -v wpa_cli >/dev/null 2>&1; then
    result=$(wpa_cli -i wlan0 status 2>/dev/null) || result=
    state=$(printf '%s\n' "$result" | sed -n 's/^wpa_state=//p')
    case "$state" in
        COMPLETED|ASSOCIATING|ASSOCIATED|AUTHENTICATING|4WAY_HANDSHAKE|GROUP_HANDSHAKE|SCANNING)
            wifi=active ;;
        DISCONNECTED|INACTIVE) wifi=idle ;;
        *) wifi=unknown ;;
    esac
fi
# BlueZ versions differ in how they expose connected devices. Only claim
# idle after a successful 'devices Connected' query with no devices listed.
# Never log the returned Bluetooth identities.
if command -v bluetoothctl >/dev/null 2>&1; then
    result=$(bluetoothctl --timeout 5 devices Connected 2>/dev/null) && ok=1 || ok=0
    if [ "$ok" -eq 1 ]; then
        case "$result" in
            '') bt=idle ;;
            Device\ *) bt=active ;;
            *) bt=unknown ;;
        esac
    fi
fi
printf 'wifi=%s\nbluetooth=%s\n' "$wifi" "$bt"
