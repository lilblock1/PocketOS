#!/bin/sh
# R1 SD refresh. Only the observed FAT partition; never format or force unmount.
set -eu
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH
umask 077
fixture=0
root=
if [ "$#" -eq 3 ] && [ "$1" = --fixture ]; then
    fixture=1; root=$2; shift 2
    [ -d "$root" ] && [ ! -L "$root" ] || exit 2
fi
[ "$#" -eq 1 ] || exit 2
case "$1" in refresh|replace-stale) action=$1;; *) exit 2;; esac
if [ "$fixture" -eq 0 ]; then
    [ "$(id -u)" = 0 ] && [ "$(uname -r)" = 4.4.94-sonix ] || exit 1
    for identity in NAME=PocketOS TARGET=hiby-r1 STATE=development-userspace-only FLASHABLE=no; do
        grep -Fxq "$identity" /etc/pocketos-release || exit 1
    done
    awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
    # Same lock as the firmware updater. Never mount during recovery handoff.
    mkdir /run/pocketos-update.lock 2>/dev/null || exit 1
    trap 'rmdir /run/pocketos-update.lock 2>/dev/null || :' EXIT
    trap 'exit 1' HUP INT TERM
fi
layout=$(awk '$2=="/mnt/sd_0" {n++;if($1=="/dev/mmcblk0p1" && $3=="vfat")good++}
    END{if(n==0)print "empty";else if(n==1&&good==1)print "known";else print "foreign"}' "$root/proc/mounts")
[ "$layout" != foreign ] || exit 1
present=0
if [ -r "$root/sys/class/block/mmcblk0p1/partition" ] &&
   [ "$(cat "$root/sys/class/block/mmcblk0p1/partition")" = 1 ]; then
    if [ "$fixture" -eq 1 ] || [ -b /dev/mmcblk0p1 ]; then present=1; fi
fi
# A known stale filesystem must be unmounted normally, not lazily or forcibly.
if [ "$layout" = known ] && { [ "$present" -eq 0 ] || [ "$action" = replace-stale ]; }; then
    if [ "$fixture" -eq 1 ]; then echo unmount; else umount /mnt/sd_0; fi
    layout=empty
fi
[ "$layout" = empty ] && [ "$present" -eq 1 ] || exit 0
if [ "$fixture" -eq 1 ]; then
    echo mount
    exit 0
fi
[ -d /mnt/sd_0 ] && [ ! -L /mnt/sd_0 ] || exit 1
# No fallback to other disks or filesystems.
mode=ro
marker=/etc/pocketos/sd-write-enable
if [ -f "$marker" ] && [ ! -L "$marker" ] &&
   [ "$(/usr/bin/pocketos-stat -c '%u:%a' "$marker")" = 0:400 ] &&
   [ "$(wc -c < "$marker")" -eq 23 ] &&
   grep -Fxq POCKETOS-R1-SD-WRITE-1 "$marker"; then mode=rw; fi
mount -t vfat -o "$mode",noatime,nosuid,nodev,noexec /dev/mmcblk0p1 /mnt/sd_0
