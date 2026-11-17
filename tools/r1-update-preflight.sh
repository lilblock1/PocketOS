#!/bin/sh
# Physical read-only verification. No reboot, mounts or MTD writes.
set -eu
[ "$(id -u)" = 0 ]
case "$#:${1:-}" in
    0:) grep -Fxq NAME=PocketOS /etc/pocketos-release ;;
    1:--bootstrap) : ;; # The first PocketOS-only install can start from stock userspace.
    *) echo "Usage: r1-update-preflight.sh [--bootstrap]" >&2; exit 2 ;;
esac
[ "$(uname -r)" = 4.4.94-sonix ]
grep -Eq '(^| )root=/dev/mtdblock_bbt_ro2( |$)' /proc/cmdline
for entry in 'mtd1: 00500000 00020000 "kernel"' 'mtd2: 02d00000 00020000 "rootfs"' 'mtd3: 00500000 00020000 "kernel2"' 'mtd4: 01800000 00020000 "rootfs2"' 'mtd5: 00080000 00020000 "ota"'; do
    grep -Fxq "$entry" /proc/mtd
done
for spec in '8604858bd975e272f5e238e1721656adbcd9e67eace049d096d049eff578a364 /dev/mtd1ro' 'ebcb4491fd67c5d479d4a7efccf0a7d5871f140634890b33ca1aeb83418aa297 /dev/mtd3ro' 'd4059f7cade350bfe0f53221ba2e886b8e45d6d625f7042bdd8cd4c39c149358 /dev/mtd4ro'; do
    set -- $spec
    [ -c "$2" ]
    actual=$(sha256sum "$2")
    [ "${actual%% *}" = "$1" ]
done
[ "$#" -eq 2 ] # set by the final digest pair above, no caller-supplied command
helper=/tmp/pocketos-recovery-preflight-helper
[ -f "$helper" ] && [ ! -L "$helper" ] && [ -x "$helper" ]
"$helper" --check
echo POCKETOS-R1-RECOVERY-PREFLIGHT-OK
