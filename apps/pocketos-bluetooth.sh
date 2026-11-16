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
bounded(){
    if [ -x /usr/bin/pocketos-timeout ]; then /usr/bin/pocketos-timeout "$@"; else seconds=$1; shift; program=$1; shift; "$program" --timeout "$seconds" "$@"; fi
}
show=$(bounded 3 bluetoothctl show 2>&1) || { echo 'RADIO_ERROR=bt-service'; exit 1; }
printf '%s\n' "$show" | grep -q 'Controller [0-9A-Fa-f]' || { echo 'RADIO_ERROR=bt-controller'; exit 1; }
if [ "$action" = status ]; then printf '%s\n' "$show"; exit 0; fi
printf '%s\n' "$show" | grep -q 'Powered: yes' || { echo 'RADIO_ERROR=bt-power'; exit 1; }
case "$action" in
    devices) bounded 3 bluetoothctl devices ;;
    discover)
        # Cached names appear immediately; live discoveries stream to the UI.
        # A full classic inquiry can run while the user selects/pairs a result.
        bounded 3 bluetoothctl devices
        scan_state=$(mktemp -d /tmp/pocketos-bt-scan.XXXXXX)
        trap 'rm -f "$scan_state/status"; rmdir "$scan_state"' EXIT
        trap 'exit 1' HUP INT TERM
        (if bluetoothctl --timeout 10 scan on 2>&1; then echo 0 > "$scan_state/status"; else echo 1 > "$scan_state/status"; fi) | awk '
            {gsub(/\033\[[0-9;]*[mK]/, "")}
            /\[NEW\] Device / {sub(/^.*Device /,"Device "); sub(/\r$/,""); print; fflush()}
        '
        [ "$(cat "$scan_state/status")" = 0 ] ;;
    scan) exec bluetoothctl --timeout 10 scan on ;;
    stop-scan) bounded 3 bluetoothctl scan off ;;
    pair)
        info=$(bounded 3 bluetoothctl info "$1")
        printf '%s\n' "$info" | grep -q 'Device .*not available' && { echo 'RADIO_ERROR=bt-device-missing'; exit 1; }
        exec bluetoothctl --agent NoInputNoOutput --timeout 25 pair "$1" ;;
    connect|disconnect) bounded 12 bluetoothctl "$action" "$1" ;;
esac
