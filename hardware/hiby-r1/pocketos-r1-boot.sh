#!/bin/sh
# First independent-userspace boot with the existing R1 kernel. No firmware writer.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
for identity in NAME=PocketOS TARGET=hiby-r1 STATE=development-userspace-only FLASHABLE=no; do
    grep -Fxq "$identity" /etc/pocketos-release
done
grep -Fxq POCKETOS-R1-FIRSTBOOT-1 /etc/pocketos/firstboot-enable
if [ "${1:-}" = --check ]; then
    /usr/bin/adbd --pocketos-build-info
    test -x /usr/sbin/pocketos-r1-board-init
    test -x /usr/bin/pocketos-start
    test -x /usr/bin/pocketos-shell
    test "$(/usr/bin/pocketos-stat -c '%u:%a' /etc/pocketos)" = 0:755
    echo 'First-boot files present; hardware not activated.'
    exit 0
fi
[ "${1:-start}" = start ] || exit 2
[ "$(id -u)" -eq 0 ]
[ -r /proc/mounts ] || mount -t proc proc /proc
mounted(){ awk -v p="$1" -v t="$2" '$2==p && $3==t {ok=1} END {exit !ok}' /proc/mounts; }
mounted /sys sysfs || mount -t sysfs sysfs /sys
mounted /dev devtmpfs || mount -t devtmpfs devtmpfs /dev
mounted /tmp tmpfs || mount -t tmpfs -o mode=1777,nosuid,nodev tmpfs /tmp
mounted /run tmpfs || mount -t tmpfs -o mode=0755,nosuid,nodev tmpfs /run
mkdir -p /dev/pts /dev/shm /run/lock /mnt/sd_0
mounted /dev/pts devpts || mount -t devpts -o gid=5,mode=620,ptmxmode=0666 devpts /dev/pts
mounted /dev/shm tmpfs || mount -t tmpfs -o mode=1777,nosuid,nodev tmpfs /dev/shm
exec >> /run/pocketos-boot.log 2>&1
echo 'PocketOS first-boot startup'
hostname pocketos-r1
/sbin/mdev -df > /run/pocketos-mdev.log 2>&1 &
/sbin/mdev -s
# Bring debugging up before the board sequence, so driver failures have a log path.
adb_started=0
if /usr/sbin/pocketos-usb-adb start; then
    adb_started=1
    echo usb-adb-started > /run/pocketos-boot.status
else
    echo 'Early USB ADB unavailable; will retry after board initialization'
fi
if ! /usr/sbin/pocketos-r1-board-init start; then
    echo board-init-failed > /run/pocketos-boot.status
    if [ "$adb_started" -eq 0 ]; then /usr/sbin/pocketos-usb-adb start || :; fi
    exit 1
fi
if [ "$adb_started" -eq 0 ] && ! /usr/sbin/pocketos-usb-adb start; then
    echo usb-adb-start-failed > /run/pocketos-boot.status
    exit 1
fi
/sbin/mdev -s
i=0
if [ -f /etc/pocketos/state-enable ] && [ -x /usr/sbin/pocketos-state-service ]; then
    /usr/sbin/pocketos-state-service start || echo 'Saved settings unavailable; retaining RAM-only operation'
fi
while [ ! -b /dev/mmcblk0p1 ] && [ "$i" -lt 10 ]; do i=$((i+1)); sleep 1; done
if [ -b /dev/mmcblk0p1 ] && [ "$(cat /sys/class/block/mmcblk0p1/partition)" = 1 ]; then
    # Explicit media-write marker permits book preparation/resume. No formatting,
    # fsck, executable SD mounts or automatic updater is introduced.
    sd_mode=ro
    if [ -f /etc/pocketos/sd-write-enable ] && [ ! -L /etc/pocketos/sd-write-enable ] &&
       [ "$(/usr/bin/pocketos-stat -c '%u:%a' /etc/pocketos/sd-write-enable)" = 0:400 ] &&
       grep -Fxq POCKETOS-R1-SD-WRITE-1 /etc/pocketos/sd-write-enable; then sd_mode=rw; fi
    mount -t vfat -o "$sd_mode",noatime,nosuid,nodev,noexec /dev/mmcblk0p1 /mnt/sd_0 || echo 'SD mount failed; continuing without media'
fi
if [ -r /sys/class/backlight/backlight_pwm0/max_brightness ]; then
    maximum=$(cat /sys/class/backlight/backlight_pwm0/max_brightness)
    if [ "$maximum" = 101 ]; then printf '35\n' > /sys/class/backlight/backlight_pwm0/brightness; fi
fi
echo starting-ui > /run/pocketos-boot.status
/usr/bin/pocketos-start > /run/pocketos-ui-supervisor.log 2>&1 &
echo "$!" > /run/pocketos-firstboot-ui.pid
echo firstboot-services-started > /run/pocketos-boot.status
