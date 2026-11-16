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
command -v bluetoothctl >/dev/null 2>&1 || { echo 'RADIO_ERROR=bt-service'; exit 1; }
case "$action" in
    scan|discover|pair|connect)
        if [ -x /usr/sbin/pocketos-radio-service ]; then
            /usr/sbin/pocketos-radio-service bluetooth-start || { echo 'RADIO_ERROR=bt-start-failed'; exit 1; }
        fi ;;
esac
# Do not attempt to initialize a UART, change shared radio power, or start a
# daemon until the board-specific transport has been verified.
show=$(bluetoothctl --timeout 5 show 2>&1) || { echo 'RADIO_ERROR=bt-service'; exit 1; }
printf '%s\n' "$show" | grep -q 'Controller [0-9A-Fa-f]' || { echo 'RADIO_ERROR=bt-controller'; exit 1; }
if [ "$action" = status ]; then printf '%s\n' "$show"; exit 0; fi
printf '%s\n' "$show" | grep -q 'Powered: yes' || { echo 'RADIO_ERROR=bt-power'; exit 1; }
case "$action" in
    devices) exec bluetoothctl --timeout 5 devices ;;
    discover)
        # Report discovery errors rather than silently returning the old cache.
        result=0
        bluetoothctl --timeout 10 scan on >/dev/null 2>&1 || result=1
        bluetoothctl --timeout 5 scan off >/dev/null 2>&1 || :
        [ "$result" -eq 0 ] || exit 1
        exec bluetoothctl --timeout 5 devices ;;
    scan) exec bluetoothctl --timeout 10 scan on ;;
    stop-scan) exec bluetoothctl --timeout 5 scan off ;;
    pair) exec bluetoothctl --timeout 30 pair "$1" ;;
    connect|disconnect) exec bluetoothctl --timeout 15 "$action" "$1" ;;
esac
