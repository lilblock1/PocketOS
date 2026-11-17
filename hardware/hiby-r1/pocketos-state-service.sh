#!/bin/sh
# Reuse an existing userdata volume. Never creates a volume or formats storage.
set -eu
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
umask 077
[ "$#" -eq 1 ] && [ "$1" = start ] && [ "$(id -u)" = 0 ] || exit 2
grep -Fxq NAME=PocketOS /etc/pocketos-release
ubi=$(/usr/sbin/pocketos-userdata --attach)
case "$ubi" in ubi[0-9]|ubi[0-9][0-9]) ;; *) exit 1;; esac
[ "$(cat "/sys/class/ubi/$ubi/mtd_num")" = 6 ]
volume=
for path in /sys/class/ubi/"$ubi"_[0-9]*; do
    [ -r "$path/name" ] || continue
    [ "$(cat "$path/name")" = user_data ] || continue
    [ -z "$volume" ] || exit 1
    volume=${path##*/}
done
[ -n "$volume" ] || { echo 'Existing userdata volume absent; refusing to create one' >&2; exit 1; }
/sbin/mdev -s
root=/run/pocketos-data
mkdir -p "$root"
[ ! -L "$root" ]
if ! awk -v node="/dev/$volume" '$1==node && $2=="/run/pocketos-data" && $3=="ubifs" {ok=1} END {exit !ok}' /proc/mounts; then
    ! awk '$2=="/run/pocketos-data" {ok=1} END {exit !ok}' /proc/mounts
    mount -t ubifs -o sync,noatime,nosuid,nodev,noexec "/dev/$volume" "$root"
fi
[ ! -L "$root/PocketOS" ]
mkdir -p "$root/PocketOS"
chmod 700 "$root/PocketOS"
echo 'Existing userdata mounted for PocketOS settings; other data preserved.'
