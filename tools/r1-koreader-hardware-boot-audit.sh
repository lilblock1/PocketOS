#!/bin/sh
# Read-only physical boot readiness audit for the HiBy R1 KOReader edition.
# Safe on the currently installed OS: no mounts, flashes, reboot, or writes.
set -eu
if [ "$#" -eq 1 ] && [ "$1" = --self-test ]; then
    echo 'HiBy R1 KOReader read-only boot audit v1'
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
status=0
if [ "$(uname -r)" = 4.4.94-sonix ]; then echo 'PASS: reviewed kernel 4.4.94-sonix'; else echo 'BLOCKED: unknown kernel'; status=1; fi
if grep -Fxq NAME=PocketOS /etc/pocketos-release &&
   grep -Fxq TARGET=hiby-r1 /etc/pocketos-release; then
    echo 'PASS: installed independent PocketOS'
else
    echo 'BLOCKED: not currently booted into independent PocketOS'; status=1
fi
if grep -Eq '^[[:space:]]*squashfs$' /proc/filesystems; then
    echo 'PASS: kernel supports SquashFS'
else
    echo 'BLOCKED: SquashFS filesystem support missing'; status=1
fi
if grep -Eq '^[[:space:]]*[0-9]+[[:space:]]+loop$' /proc/devices; then
    echo 'PASS: kernel registers a loop block device driver'
else
    echo 'BLOCKED: no loop block driver registered (check builtin/module support)'; status=1
fi
if awk '$2=="/mnt/sd_0" && $3=="vfat" &&
     $4 ~ /(^|,)noexec(,|$)/ {ok=1} END {exit !ok}' /proc/mounts; then
    echo 'PASS: SD mounted vfat,noexec'
else
    echo 'BLOCKED: SD mount absent/unknown or noexec not active'; status=1
fi
if [ -d /run ] &&
   awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts; then
    echo 'PASS: RAM-only /run available'
else
    echo 'BLOCKED: required /run tmpfs unavailable'; status=1
fi
if [ -x /usr/sbin/pocketos-r1-boot ] && [ -x /usr/bin/pocketos-start ]; then
    echo 'PASS: installed boot and UI launch chain present'
else
    echo 'BLOCKED: boot or UI entry point missing'; status=1
fi
if [ "$status" -eq 0 ]; then
    echo 'READ-ONLY PREFLIGHT PASS (not a KOReader boot confirmation)'
else
    echo 'READ-ONLY PREFLIGHT BLOCKED; do not install an unverified image'
fi
exit "$status"
