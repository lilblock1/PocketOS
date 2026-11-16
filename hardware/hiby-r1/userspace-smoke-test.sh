#!/bin/sh
# Isolated compatibility check under the existing OS, NOT an independent boot.
# No init scripts, module loads, USB replacement, display or audio access.
set -eu
umask 077
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
[ "$#" -eq 2 ] || { echo 'Usage: userspace-smoke-test.sh SD_TEST_IMAGE SHA256' >&2; exit 2; }
image=$1; expected=$2
candidate=0
case "$image" in
    /mnt/sd_0/PocketOS-test/userspace-test-NOT-FIRMWARE.squashfs) ;;
    /mnt/sd_0/PocketOS-test/pocketos-r1-firstboot-rootfs.squashfs) candidate=1;;
    *) exit 2;;
esac
[ "$(id -u)" -eq 0 ] && [ -f "$image" ] && [ ! -L "$image" ] || exit 2
[ "${#expected}" -eq 64 ] || exit 2
case "$expected" in *[!0-9a-f]*) exit 2;; esac
awk '$2=="/tmp" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
awk '$2=="/mnt/sd_0" && $3=="vfat" {ok=1} END {exit !ok}' /proc/mounts
[ "$(sha256sum "$image" | cut -d ' ' -f1)" = "$expected" ] || { echo 'Image hash mismatch' >&2; exit 1; }
root=$(mktemp -d /tmp/pocketos-userspace.XXXXXX)
loop=
mounted=0
cleanup(){
    code=$?
    trap - EXIT HUP INT TERM
    if [ "$mounted" -eq 1 ]; then
        if ! umount "$root"; then echo 'Test mount cleanup failed' >&2; exit 1; fi
    fi
    if [ -n "$loop" ]; then losetup -d "$loop" || exit 1; fi
    rmdir "$root" || exit 1
    echo 'CLEANUP_OK: test mount removed and loop detached'
    exit "$code"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
loop=$(losetup -f)
losetup -r "$loop" "$image"
mount -t squashfs -o ro,nosuid,nodev "$loop" "$root"
mounted=1
for identity in NAME=PocketOS TARGET=hiby-r1 STATE=development-userspace-only FLASHABLE=no; do
    grep -Fxq "$identity" "$root/etc/pocketos-release"
done
if [ "$candidate" -eq 0 ]; then
    [ ! -e "$root/etc/pocketos/autostart" ]
    [ ! -e "$root/module_driver" ]
else
    [ -c "$root/dev/console" ] && [ -c "$root/dev/null" ]
    [ -d "$root/mnt/sd_0" ]
    chroot "$root" /usr/sbin/pocketos-r1-boot --check
    chroot "$root" /usr/sbin/pocketos-r1-board-init --check
fi
# Deliberately do not bind the host's /dev, /proc, /sys, /run or microSD.
chroot "$root" /bin/busybox echo NATIVE_POCKETOS_LIBC_OK
for name in pocketos-info pocketos-shell pocketos-music pocketos-wifi; do
    chroot "$root" "/usr/bin/$name" --version
done
chroot "$root" /usr/sbin/pocketos-r1-runtime --version
chroot "$root" /usr/bin/adbd --pocketos-build-info
chroot "$root" /usr/bin/pocketos-shell --self-test
chroot "$root" /usr/bin/pocketos-shell --test-radio
chroot "$root" /usr/bin/pocketos-shell --test-update-ui
echo 'NATIVE_USERSPACE_SMOKE_OK: own libc, five applications, USB daemon identity and UI tests'
echo 'Firmware boot and new USB endpoint operation remain untested.'
