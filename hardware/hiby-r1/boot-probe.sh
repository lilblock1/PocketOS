#!/bin/sh
# Read-only evidence on a currently working R1; never invoke any recovery mode.
set -eu
printf '[partition table: /proc/mtd]\n'
if [ -r /proc/mtd ]; then cat /proc/mtd; else printf 'unavailable\n'; fi
printf '\n[filesystem mount points and types; no contents]\n'
awk '$2=="/" || $2=="/usr/data" || $2=="/mnt/sd_0" {print $2,$3,$4}' /proc/mounts
printf '\n[UBI volume capacity; no raw flash access]\n'
for f in /sys/class/ubi/ubi[0-9]*/total_eraseblocks /sys/class/ubi/ubi[0-9]*/avail_eraseblocks /sys/class/ubi/ubi[0-9]*/eraseblock_size /sys/class/ubi/ubi[0-9]*/reserved_ebs; do
    if [ -r "$f" ]; then printf '%s=' "$f"; cat "$f" || printf 'unavailable\n'; fi
done
printf '\nNo partitions written or mounted. Recovery has not been tested.\n'
