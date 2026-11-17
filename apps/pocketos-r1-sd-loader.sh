#!/bin/sh
# Immutable R1 boot shim. Generated SHA256 values are pinned at image build.
F=/usr/bin/sonix_player.pocketos-fallback.sh
G=/mnt/sd_0/PocketOS/boot-guard.sh
fallback() { exec "$F"; }
[ -f "$F" ] && [ -x "$F" ] && [ ! -L "$F" ] || exit 1
[ -d /mnt/sd_0/PocketOS ] && [ ! -L /mnt/sd_0/PocketOS ] || fallback
[ -f "$G" ] && [ ! -L "$G" ] || fallback
[ "$(wc -c < "$G")" -le 8192 ] || fallback
[ "$(sha256sum "$G" | cut -c1-64)" = "@GUARD_SHA256@" ] || fallback
POCKETOS_SHELL_SHA256="@SHELL_SHA256@" /bin/sh "$G" || :
fallback
