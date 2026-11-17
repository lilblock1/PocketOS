# PocketOS physical R1 boot status

The local project archive contains post-installation device diagnostics from the first independent PocketOS userspace boot, retaining the existing 4.4.94-sonix kernel. This supersedes the Phase 4C CI-only boot status; it does not authorize flashing current CI artifacts.

## Observations

- Physical display: 480 x 800 RGB565, virtual 480 x 1600, stride 960 bytes. Explicit framebuffer unblank succeeded; page flipping reported device busy and the UI used the current page.
- The touchscreen was detected as hyn_ts; calibration ranges were 0–480 and 0–800.
- Linux reports 58,560 KiB total and 42,500 KiB available RAM in the captured post-boot snapshot. The recorded UI process RSS was 2,368 KiB, high-water 2,992 KiB. These are single snapshots, not a battery or performance benchmark.
- The microSD was mounted at /mnt/sd_0 read-only, with nosuid,nodev,noexec. Persistent book preparation, bookmarks, decoder installation and signed package staging require a separately verified writable media path.
- Radio snapshot shows only loopback and no Bluetooth HCI; the combo power module is loaded, but /lib/firmware/brcm was missing. The presence of bluetoothctl and wpa_supplicant does not establish working radios.

## Integration priorities

1. Synchronize local Phase 5 source and tests; do not publish raw device logs, downloaded firmware, keys, or generated binaries.
2. Validate cold-start UI supervisor and explicit display unblank in the next approved development build.
3. Resolve verified SD mount/write policy and audio routing; then bring up Wi-Fi and Bluetooth with matching driver/firmware provenance.
4. Preserve existing bootloader and recovery partitions; stock restore has not been demonstrated. No automatic firmware update or release until separately reviewed.

The full local source synchronization is pending. This status file is sanitized documentation only, not an installable update.
