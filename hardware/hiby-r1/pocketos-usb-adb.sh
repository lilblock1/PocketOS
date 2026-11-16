#!/bin/sh
# Independent PocketOS development USB gadget. Never run on the stock OS.
set -eu
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
die(){ echo "PocketOS USB ADB: $*" >&2; exit 1; }
release=/etc/pocketos-release
[ -f "$release" ] && [ ! -L "$release" ] || die 'not PocketOS userspace'
for identity in NAME=PocketOS TARGET=hiby-r1 STATE=development-userspace-only FLASHABLE=no; do
    grep -Fxq "$identity" "$release" || die 'wrong development rootfs identity'
done
case "${1:---check}" in
  --check)
    [ -x /usr/bin/adbd ] || die 'daemon missing'
    /usr/bin/adbd --pocketos-build-info
    [ -d /sys/class/udc ] || die 'USB controller unavailable'
    exit 0;;
  start) ;;
  *) die 'use --check or start';;
esac
[ "$(id -u)" -eq 0 ] || die 'startup requires root'
[ -f /etc/pocketos/adb-enable ] && [ ! -L /etc/pocketos/adb-enable ] || die 'development USB ADB not enabled'
grep -Fxq POCKETOS-USB-ADB-DEV-1 /etc/pocketos/adb-enable || die 'invalid enable marker'
[ -x /usr/bin/adbd ] || die 'daemon missing'
awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || die '/run must be tmpfs'
if ! awk '$2=="/sys/kernel/config" && $3=="configfs" {ok=1} END {exit !ok}' /proc/mounts; then
    mount -t configfs none /sys/kernel/config
fi
for existing in /sys/kernel/config/usb_gadget/*; do
    [ ! -d "$existing" ] || die 'an existing USB gadget is already configured; refusing to replace it'
done
controller=
for candidate in /sys/class/udc/*; do
    [ -e "$candidate" ] || continue
    [ -z "$controller" ] || die 'more than one USB controller'
    controller=${candidate##*/}
done
[ -n "$controller" ] || die 'USB controller missing'
g=/sys/kernel/config/usb_gadget/pocketos_r1_adb
child=; ffs_mounted=0; gadget_created=0; started=0
cleanup(){
    code=$?
    trap - EXIT HUP INT TERM
    [ "$started" -eq 0 ] || exit "$code"
    if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi
    if [ "$gadget_created" -eq 1 ]; then
        [ -z "$(cat "$g/UDC")" ] || printf '\n' > "$g/UDC" || :
        [ ! -L "$g/configs/c.1/ffs.adb" ] || rm "$g/configs/c.1/ffs.adb"
        if [ "$ffs_mounted" -eq 1 ]; then umount /dev/usb-ffs/adb || :; fi
        [ ! -d "$g/functions/ffs.adb" ] || rmdir "$g/functions/ffs.adb" || :
        [ ! -d "$g/configs/c.1/strings/0x409" ] || rmdir "$g/configs/c.1/strings/0x409" || :
        [ ! -d "$g/configs/c.1" ] || rmdir "$g/configs/c.1" || :
        [ ! -d "$g/strings/0x409" ] || rmdir "$g/strings/0x409" || :
        rmdir "$g" || :
    fi
    rm -f /run/pocketos-adbd.pid
    exit "$code"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
mkdir "$g"
gadget_created=1
printf '0x18d1\n' > "$g/idVendor"
printf '0xd002\n' > "$g/idProduct"
printf '0x0200\n' > "$g/bcdUSB"
printf '0x0100\n' > "$g/bcdDevice"
mkdir "$g/strings/0x409"
printf 'PocketOS\n' > "$g/strings/0x409/manufacturer"
printf 'HiBy R1 development USB ADB\n' > "$g/strings/0x409/product"
# Retain the existing engineering USB identity; never derive it from a MAC.
printf 'ingenic_\n' > "$g/strings/0x409/serialnumber"
mkdir "$g/configs/c.1" "$g/configs/c.1/strings/0x409"
printf '120\n' > "$g/configs/c.1/MaxPower"
printf 'adb\n' > "$g/configs/c.1/strings/0x409/configuration"
mkdir "$g/functions/ffs.adb"
ln -s "$g/functions/ffs.adb" "$g/configs/c.1/ffs.adb"
mkdir -p /dev/usb-ffs/adb
mount -t functionfs adb /dev/usb-ffs/adb
ffs_mounted=1
/usr/bin/adbd > /run/pocketos-adbd.log 2>&1 &
child=$!
printf '%s\n' "$child" > /run/pocketos-adbd.pid
i=0
while [ ! -e /dev/usb-ffs/adb/ep1 ] || [ ! -e /dev/usb-ffs/adb/ep2 ]; do
    kill -0 "$child" 2>/dev/null || die 'daemon exited before endpoint readiness'
    i=$((i+1))
    if [ "$i" -ge 50 ]; then kill -TERM "$child" 2>/dev/null || :; die 'endpoint readiness timed out'; fi
    sleep 0.1
done
printf '%s\n' "$controller" > "$g/UDC"
started=1
echo 'PocketOS USB-only development ADB started.'
