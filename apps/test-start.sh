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
test "$(grep -c '^--boot-splash --unblank-display --fb /dev/input/event3$' "$tmp/fail.log")" -eq 1
test "$(grep -c -- '--unblank-display --fb /dev/input/event3$' "$tmp/fail.log")" -eq 3
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

# A trusted feature marker adds only the explicit development flags.
printf 'POCKETOS-DEV-FEATURES-1\n' > "$tmp/etc/pocketos/features"
cat > "$tmp/features-ok" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" > "$0.log"
EOF
chmod 755 "$tmp/features-ok"
"$start" --fixture "$tmp" "$tmp/features-ok"
grep -Fx -- '--boot-splash --unblank-display --allow-audio --allow-bookmarks --allow-book-preparation --allow-radio --allow-backlight --fb /dev/input/event3' "$tmp/features-ok.log"
printf 'invalid\n' > "$tmp/etc/pocketos/features"
rm "$tmp/features-ok.log"
if "$start" --fixture "$tmp" "$tmp/features-ok"; then exit 1; fi
test ! -e "$tmp/features-ok.log"

# Audio approval must not unlock networking, storage or updates.
rm "$tmp/etc/pocketos/features"
printf 'POCKETOS-R1-AUDIO-1\n' > "$tmp/etc/pocketos/audio-enable"
"$start" --fixture "$tmp" "$tmp/features-ok"
grep -Fx -- '--boot-splash --unblank-display --allow-audio --fb /dev/input/event3' "$tmp/features-ok.log"
printf 'invalid\n' > "$tmp/etc/pocketos/audio-enable"
if "$start" --fixture "$tmp" "$tmp/features-ok"; then exit 1; fi
rm "$tmp/etc/pocketos/audio-enable"
# Recreate general marker for the existing Smart Sleep test setup.
printf 'POCKETOS-DEV-FEATURES-1\n' > "$tmp/etc/pocketos/features"

# Independent opt-in for software Smart Sleep, not part of the general feature marker.
rm "$tmp/etc/pocketos/features"
printf 'POCKETOS-R1-SMART-SLEEP-1\n' > "$tmp/etc/pocketos/smart-sleep-enable"
"$start" --fixture "$tmp" "$tmp/features-ok"
grep -Fx -- '--boot-splash --unblank-display --allow-smart-sleep --fb /dev/input/event3' "$tmp/features-ok.log"
printf "invalid\n" > "$tmp/etc/pocketos/smart-sleep-enable"
if "$start" --fixture "$tmp" "$tmp/features-ok"; then exit 1; fi
rm "$tmp/etc/pocketos/smart-sleep-enable"
# Stale kernel-suspend marker must not enable any kernel sleep mode.
printf 'POCKETOS-R1-SUSPEND-1\n' > "$tmp/etc/pocketos/suspend-enable"
"$start" --fixture "$tmp" "$tmp/features-ok"
grep -Fx -- '--boot-splash --unblank-display --fb /dev/input/event3' "$tmp/features-ok.log"
rm "$tmp/etc/pocketos/suspend-enable"
# Cold boot can renumber input nodes. Auto mode accepts only one exact name.
printf 'POCKETOS-DEV-AUTOSTART-1 auto\n' > "$tmp/etc/pocketos/autostart"
mkdir -p "$tmp/sys/class/input/event0/device" "$tmp/sys/class/input/event7/device"
printf 'gpio-keys\n' > "$tmp/sys/class/input/event0/device/name"
printf 'hyn_ts\n' > "$tmp/sys/class/input/event7/device/name"
rm -f "$tmp/features-ok.log"
"$start" --fixture "$tmp" "$tmp/features-ok"
grep -Fx -- '--boot-splash --unblank-display --fb /dev/input/event7' "$tmp/features-ok.log"
test "$(cat "$tmp/run/pocketos-ui/input")" = event7
mkdir -p "$tmp/sys/class/input/event8/device"
printf 'hyn_ts\n' > "$tmp/sys/class/input/event8/device/name"
rm "$tmp/features-ok.log"
if "$start" --fixture "$tmp" "$tmp/features-ok"; then exit 1; fi
test ! -e "$tmp/features-ok.log"
test "$(cat "$tmp/run/pocketos-ui/status")" = hardware-not-ready
rm "$tmp/sys/class/input/event7/device/name" "$tmp/sys/class/input/event8/device/name"
if "$start" --fixture "$tmp" "$tmp/features-ok"; then exit 1; fi
test ! -e "$tmp/features-ok.log"
# Both independent approvals can coexist without broader permissions.
printf 'hyn_ts\n' > "$tmp/sys/class/input/event7/device/name"
printf 'POCKETOS-R1-AUDIO-1\n' > "$tmp/etc/pocketos/audio-enable"
printf 'POCKETOS-R1-SMART-SLEEP-1\n' > "$tmp/etc/pocketos/smart-sleep-enable"
"$start" --fixture "$tmp" "$tmp/features-ok"
grep -Fx -- '--boot-splash --unblank-display --allow-audio --allow-smart-sleep --fb /dev/input/event7' "$tmp/features-ok.log"
printf 'POCKETOS-R1-RADIO-1\n' > "$tmp/etc/pocketos/radio-enable"
"$start" --fixture "$tmp" "$tmp/features-ok"
grep -F -- '--allow-radio' "$tmp/features-ok.log"
printf 'invalid\n' > "$tmp/etc/pocketos/radio-enable"
if "$start" --fixture "$tmp" "$tmp/features-ok"; then exit 1; fi
