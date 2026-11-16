#!/bin/sh
set -eu
start=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/etc/pocketos" "$tmp/run"
printf '%s\n' 'NAME=PocketOS' 'TARGET=hiby-r1' 'STATE=development-userspace-only' 'FLASHABLE=no' > "$tmp/etc/pocketos-release"
cat > "$tmp/fail" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$0.log"
exit 7
EOF
chmod 755 "$tmp/fail"
"$start" --fixture "$tmp" "$tmp/fail"
test ! -e "$tmp/fail.log"
printf 'POCKETOS-DEV-AUTOSTART-1 ../event0\n' > "$tmp/etc/pocketos/autostart"
if "$start" --fixture "$tmp" "$tmp/fail"; then exit 1; fi
test ! -e "$tmp/fail.log"
printf 'POCKETOS-DEV-AUTOSTART-1 event3\n' > "$tmp/etc/pocketos/autostart"
if "$start" --fixture "$tmp" "$tmp/fail"; then exit 1; fi
test "$(wc -l < "$tmp/fail.log")" -eq 3
test "$(grep -c '^--boot-splash --fb /dev/input/event3$' "$tmp/fail.log")" -eq 1
test "$(cat "$tmp/run/pocketos-ui/status")" = recovery-required-exit-7
test ! -e "$tmp/run/pocketos-ui/pid"
test ! -d "$tmp/run/pocketos-ui/lock"
cat > "$tmp/ok" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod 755 "$tmp/ok"
"$start" --fixture "$tmp" "$tmp/ok"
test "$(cat "$tmp/run/pocketos-ui/status")" = shell-exited
