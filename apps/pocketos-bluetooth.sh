#!/bin/sh
# Only delegates to an existing BlueZ daemon; never boots or powers hardware.
set -eu
allow=0
if [ "$#" -gt 0 ] && [ "$1" = "--allow-radio" ]; then allow=1; shift; fi
[ "$#" -ge 1 ] || exit 2
action=$1; shift
case "$action" in
    status|devices) [ "$#" -eq 0 ] || exit 2 ;;
    scan|stop-scan|discover) [ "$#" -eq 0 ] && [ "$allow" -eq 1 ] || exit 2 ;;
    pair|connect|disconnect)
        [ "$#" -eq 1 ] && [ "$allow" -eq 1 ] || exit 2
        printf '%s\n' "$1" | grep -Eq '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$' || exit 2 ;;
    *) exit 2 ;;
esac
command -v bluetoothctl >/dev/null 2>&1 || { echo 'BlueZ client unavailable' >&2; exit 1; }
case "$action" in
    status) exec bluetoothctl --timeout 5 show ;;
    devices) exec bluetoothctl --timeout 5 devices ;;
    discover)
        bluetoothctl --timeout 10 scan on >/dev/null 2>&1 || :
        bluetoothctl --timeout 5 scan off >/dev/null 2>&1 || :
        exec bluetoothctl --timeout 5 devices ;;
    scan) exec bluetoothctl --timeout 10 scan on ;;
    stop-scan) exec bluetoothctl --timeout 5 scan off ;;
    pair) exec bluetoothctl --timeout 30 pair "$1" ;;
    connect|disconnect) exec bluetoothctl --timeout 15 "$action" "$1" ;;
esac
