#!/bin/sh
# Test the new daemon's FunctionFS descriptors without changing the active USB gadget.
set -eu
umask 077
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
[ "$#" -eq 2 ] || { [ "$#" -eq 3 ] && [ "$3" = --reconnect-30 ]; } || exit 2
reconnect=0
[ "$#" -ne 3 ] || reconnect=1
image=$1; expected=$2
case "$image" in /mnt/sd_0/PocketOS-test/userspace-test-NOT-FIRMWARE.squashfs) ;; *) exit 2;; esac
[ "$(id -u)" -eq 0 ] && [ -f "$image" ] && [ ! -L "$image" ] || exit 2
[ "${#expected}" -eq 64 ] || exit 2
case "$expected" in *[!0-9a-f]*) exit 2;; esac
awk '$2=="/tmp" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
[ "$(sha256sum "$image" | cut -d ' ' -f1)" = "$expected" ] || exit 1
original=/sys/kernel/config/usb_gadget/adb_demo
[ -r "$original/UDC" ] && [ -n "$(cat "$original/UDC")" ] || exit 1
controller=$(cat "$original/UDC")
g=/sys/kernel/config/usb_gadget/pocketos_descriptor_probe
[ ! -e "$g" ] || exit 1
state=$(mktemp -d /tmp/pocketos-usb-probe.XXXXXX)
root=$state/root
mkdir "$root"
loop=; child=; root_mounted=0; dev_mounted=0; ffs_mounted=0; gadget=0
pts_mounted=0; suspended=0; switched=0; original_daemon=; original_wrapper=
cleanup(){
    code=$?
    trap - EXIT HUP INT TERM
    if [ "$switched" -eq 1 ]; then printf '\n' > "$g/UDC" || :; fi
    if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi
    if [ "$suspended" -eq 1 ]; then
        kill -CONT "$original_wrapper" "$original_daemon" || :
        if [ "$(cat "$original/UDC")" != "$controller" ]; then
            printf '%s\n' "$controller" > "$original/UDC" || :
        fi
    fi
    if [ "$ffs_mounted" -eq 1 ]; then umount "$root/dev/usb-ffs/adb" || exit 1; fi
    if [ "$gadget" -eq 1 ]; then
        [ ! -L "$g/configs/c.1/ffs.pocketos_probe" ] || rm "$g/configs/c.1/ffs.pocketos_probe"
        [ ! -d "$g/functions/ffs.pocketos_probe" ] || rmdir "$g/functions/ffs.pocketos_probe"
        [ ! -d "$g/configs/c.1/strings/0x409" ] || rmdir "$g/configs/c.1/strings/0x409"
        [ ! -d "$g/configs/c.1" ] || rmdir "$g/configs/c.1"
        [ ! -d "$g/strings/0x409" ] || rmdir "$g/strings/0x409"
        rmdir "$g" || exit 1
    fi
    if [ "$pts_mounted" -eq 1 ]; then umount "$root/dev/pts" || exit 1; fi
    if [ "$dev_mounted" -eq 1 ]; then umount "$root/dev" || exit 1; fi
    if [ "$root_mounted" -eq 1 ]; then umount "$root" || exit 1; fi
    if [ -n "$loop" ]; then losetup -d "$loop" || exit 1; fi
    rmdir "$root"
    [ "$(cat "$original/UDC")" = "$controller" ] || { echo 'Original USB controller changed unexpectedly' >&2; exit 1; }
    echo 'USB_PROBE_CLEANUP_OK: active gadget unchanged; test gadget and mounts removed'
    echo "log=$state/adbd.log"
    exit "$code"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
loop=$(losetup -f)
losetup -r "$loop" "$image"
mount -t squashfs -o ro,nosuid,nodev "$loop" "$root"
root_mounted=1
grep -Fxq NAME=PocketOS "$root/etc/pocketos-release"
mount -t tmpfs -o size=1m,mode=0755,nosuid tmpfs "$root/dev"
dev_mounted=1
mkdir -p "$root/dev/usb-ffs/adb"
mknod "$root/dev/null" c 1 3
if [ "$reconnect" -eq 1 ]; then
    mkdir "$root/dev/pts"
    mount -t devpts -o newinstance,ptmxmode=0666,mode=0620 devpts "$root/dev/pts"
    pts_mounted=1
    ln -s pts/ptmx "$root/dev/ptmx"
    mknod "$root/dev/tty" c 5 0
fi
mkdir "$g"
gadget=1
printf '0x18d1\n' > "$g/idVendor"
printf '0xd002\n' > "$g/idProduct"
mkdir "$g/strings/0x409" "$g/configs/c.1" "$g/configs/c.1/strings/0x409"
printf 'ingenic_\n' > "$g/strings/0x409/serialnumber"
printf 'PocketOS descriptor test\n' > "$g/strings/0x409/product"
mkdir "$g/functions/ffs.pocketos_probe"
ln -s "$g/functions/ffs.pocketos_probe" "$g/configs/c.1/ffs.pocketos_probe"
mount -t functionfs pocketos_probe "$root/dev/usb-ffs/adb"
ffs_mounted=1
chroot "$root" /usr/bin/adbd > "$state/adbd.log" 2>&1 &
child=$!
i=0
while [ ! -e "$root/dev/usb-ffs/adb/ep1" ] || [ ! -e "$root/dev/usb-ffs/adb/ep2" ]; do
    kill -0 "$child" 2>/dev/null || { cat "$state/adbd.log"; exit 1; }
    i=$((i+1)); [ "$i" -lt 50 ] || { cat "$state/adbd.log"; echo 'Descriptor readiness timeout' >&2; exit 1; }
    sleep 0.1
done
[ -z "$(cat "$g/UDC")" ]
[ "$(cat "$original/UDC")" = "$controller" ]
echo 'NATIVE_FUNCTIONFS_DESCRIPTORS_OK: kernel accepted new daemon descriptors and created both bulk endpoints'
if [ "$reconnect" -eq 0 ]; then
    echo 'The test gadget was never bound. Host connection through the new daemon remains untested.'
    exit 0
fi
# Keep the existing daemon and its restart wrapper intact, frozen for this bounded test.
for candidate in $(pidof adbd); do
    if [ "$(readlink /proc/"$candidate"/exe)" = /usr/bin/adbd ]; then
        [ -z "$original_daemon" ] || { echo 'Multiple original daemons' >&2; exit 1; }
        original_daemon=$candidate
    fi
done
case "$original_daemon" in ''|*[!0-9]*) echo 'Expected exactly one original daemon' >&2; exit 1;; esac
original_wrapper=$(awk '/^PPid:/ {print $2}' /proc/"$original_daemon"/status)
tr '\000' ' ' < /proc/"$original_wrapper"/cmdline | grep -Fq '/sbin/adbserver.sh 440'
kill -STOP "$original_wrapper" "$original_daemon"
suspended=1
printf '\n' > "$original/UDC"
switched=1
printf '%s\n' "$controller" > "$g/UDC"
echo 'USB_RECONNECT_WINDOW: new daemon connected for 30 seconds; original will resume automatically'
sleep 30
echo 'USB_RECONNECT_WINDOW_ENDED'
