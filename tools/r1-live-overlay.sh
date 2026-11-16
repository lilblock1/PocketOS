#!/bin/sh
# Reversible developer overlay. RAM only; never formats or writes an MTD device.
set -eu
[ "$(id -u)" = 0 ]
grep -Fxq NAME=PocketOS /etc/pocketos-release
awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts
base=/run/pocketos-radio-test
mkdir -p "$base"
chmod 700 "$base"
overlay(){
    target=$1; name=$2
    awk -v p="$target" '$2==p {found=1} END {exit !found}' /proc/mounts && return 0
    [ -d "$target" ] && [ ! -L "$target" ]
    mkdir "$base/base-$name" "$base/$name"
    mount --bind "$target" "$base/base-$name"
    for path in "$base/base-$name"/*; do
        [ -e "$path" ] || [ -L "$path" ] || continue
        if [ -L "$path" ]; then source=$(readlink -f "$path"); else source=$path; fi
        ln -s "$source" "$base/$name/${path##*/}"
    done
    mount --bind "$base/$name" "$target"
}
overlay /usr/bin bin
overlay /usr/sbin sbin
if ! awk '$2=="/etc/pocketos" {found=1} END {exit !found}' /proc/mounts; then
    mkdir "$base/config"
    cp -a /etc/pocketos/. "$base/config/"
    chmod 755 "$base/config"
    mount --bind "$base/config" /etc/pocketos
fi
overlay /usr/share/dbus-1/system.d dbus-policy
# D-Bus runs without root privileges; policy files must be readable directly,
# not symlinks through the private RAM staging directory.
for path in "$base/base-dbus-policy"/*; do
    [ -f "$path" ] || continue
    cp "$path" "$base/dbus-policy/.next"
    chmod 644 "$base/dbus-policy/.next"
    mv -f "$base/dbus-policy/.next" "$base/dbus-policy/${path##*/}"
done
chmod 755 "$base/dbus-policy"
# The startup supervisor deliberately requires a regular trusted executable.
if [ -L /usr/bin/pocketos-shell ]; then
    cp "$base/base-bin/pocketos-shell" "$base/bin/pocketos-shell.next"
    chmod 500 "$base/bin/pocketos-shell.next"
    mv -f "$base/bin/pocketos-shell.next" "$base/bin/pocketos-shell"
fi
echo 'RAM overlays ready; reboot discards them.'
