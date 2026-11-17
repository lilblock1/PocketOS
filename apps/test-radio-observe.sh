#!/bin/sh
set -eu
script=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/bin"
cat > "$tmp/bin/wpa_cli" <<'EOF'
#!/bin/sh
[ "$1" = -i ] && [ "$2" = wlan0 ] && [ "$3" = status ] || exit 2
printf 'wpa_state=%s\n' "${WIFI_STATE:-INACTIVE}"
EOF
cat > "$tmp/bin/bluetoothctl" <<'EOF'
#!/bin/sh
[ "$1" = --timeout ] && [ "$2" = 5 ] && [ "$3" = devices ] && [ "$4" = Connected ] || exit 2
case "${BT_STATE:-idle}" in
 active) printf 'Device 00:11:22:33:44:55 Private headset\n' ;;
 unknown) printf 'Invalid command\n' ;;
 fail) exit 1 ;;
esac
EOF
chmod +x "$tmp/bin/"*
export PATH="$tmp/bin:$PATH"
test "$(sh "$script")" = "$(printf 'wifi=idle\nbluetooth=idle')"
test "$(WIFI_STATE=COMPLETED BT_STATE=active sh "$script")" = "$(printf 'wifi=active\nbluetooth=active')"
test "$(WIFI_STATE=SCANNING BT_STATE=fail sh "$script")" = "$(printf 'wifi=active\nbluetooth=unknown')"
test "$(WIFI_STATE=BOGUS BT_STATE=unknown sh "$script")" = "$(printf 'wifi=unknown\nbluetooth=unknown')"
if sh "$script" unexpected >/dev/null 2>&1; then exit 1; fi
