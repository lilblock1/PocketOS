#!/bin/sh
# PocketOS R1 internal default boot, no microSD activation or SD dependency.
# The proven R1 kernel, hardware startup and original Sonix executable stay intact.
set -u
ORIGINAL=/usr/bin/sonix_player.pocketos-fallback.sh
POCKETOS=/usr/bin/pocketos-shell
fallback() {
    [ -x "$ORIGINAL" ] && [ -f "$ORIGINAL" ] && [ ! -L "$ORIGINAL" ] || exit 1
    exec "$ORIGINAL"
}
[ -f "$POCKETOS" ] && [ -x "$POCKETOS" ] && [ ! -L "$POCKETOS" ] || fallback
[ -c /dev/fb0 ] || fallback
[ "$(id -u)" = 0 ] || fallback
# Catch bad/corrupt PocketOS executables before opening the framebuffer.
"$POCKETOS" --self-test >/dev/null 2>&1 || fallback
# Native UI owns the screen. Input is identified via EVIOCGABS, not names.
# Keep a small RAM-only startup diagnostic to help physical R1 debugging.
"$POCKETOS" --boot-splash --allow-audio --allow-radio --allow-backlight --auto-input \
    </dev/null 2>/tmp/pocketos-boot-error.log
# If PocketOS exits, crashes or hardware is unsupported, boot the old GUI.
fallback
