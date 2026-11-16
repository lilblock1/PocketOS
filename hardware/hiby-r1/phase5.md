# Phase 5A: first physical diagnostic results

The connected, working Sonix Books R1 was inspected without replacing its OS.
Static PocketOS `pocketos-info --version` and `pocketos-r1-runtime --version`,
`--status`, and `--policy 0 0 0` then ran successfully from a new directory on
the verified `/tmp` tmpfs. Transfers were checked by SHA-256 before execution;
the two temporary executables and their directory were removed afterward.
No internal-storage, microSD, bootloader, recovery, module, radio, charger,
governor, framebuffer or mixer writes were requested. This establishes a
physical userspace diagnostic smoke test, not PocketOS boot or playback.

## Observed board and storage

Kernel: `4.4.94-sonix`; machine: `mips`; usable RAM reported by Linux:
58,560 KiB. All 30 files in `/module_driver/*.ko` matched the Phase 4C
reference module hashes. File identity does not prove the bytes of loaded
modules or establish load order; `/proc/modules` is a separate observation.

| Partition | Label | Raw bytes | Treatment |
| --- | --- | ---: | --- |
| mtd0 | uboot | 524,288 | Preserve |
| mtd1 | kernel | 5,242,880 | No writes |
| mtd2 | rootfs | 47,185,920 | No writes |
| mtd3 | kernel2 | 5,242,880 | Preserve; role not established |
| mtd4 | rootfs2 | 25,165,824 | Preserve; role not established |
| mtd5 | ota | 524,288 | Preserve |
| mtd6 | userdata | 47,185,920 | Preserve |

The selected boot arguments identify `init=/linuxrc`,
`root=/dev/mtdblock_bbt_ro2`, and `rootfstype=squashfs`. `/` is mounted
read-only. The bad-block-aware root device means raw partition size is not
proof of usable firmware capacity. Neither secondary partition is assumed
to be a PocketOS slot or a verified recovery system.

`/usr/data` is UBIFS on `/dev/ubi1_0`; sysfs maps UBI1 to MTD6. The
`user_data` volume reports 336 reserved eraseblocks, 126,976 usable bytes per
eraseblock, and 42,663,936 data bytes. Zero unallocated UBI eraseblocks does
not mean the filesystem is full: `df` reported 33,948 KiB available.

The microSD is `/dev/mmcblk0p1`, mounted as VFAT at `/mnt/sd_0`, although
`/sys/block/mmcblk0/removable` reports `0`. Optional codecs, large packages,
books and music remain microSD content; they are not added to internal rootfs.

## Concrete integration findings

- The existing OS uses glibc 2.22. The Phase 4C dynamic shell requires
  GLIBC_2.27, 2.28, 2.33 and 2.34, so it cannot simply be copied to Sonix and
  launched with the existing loader. Foundation/QEMU tests used the matching
  PocketOS rootfs and did not establish compatibility with Sonix's libc.
- The new diagnostic-only build reuses the exported SDK and links statically.
  Both executables have MIPS32r2 little-endian hard-double-float ABI, no
  interpreter, and no dynamic NEEDED libraries. They passed QEMU version
  checks and the physical tests above. This does not validate a static UI.
- Framebuffer and input nodes exist. The framebuffer reports 480 x 1600
  virtual pixels at 16 bpp, consistent with two 480 x 800 buffers; visible
  geometry, stride and pixel layout still need an ioctl-based check.
- ALSA playback node `/dev/snd/pcmC0D0p`, controlC0 and the HiBy sound-card
  sysfs entries exist, and `/usr/bin/aplay` is available. `/proc/asound/cards`
  did not supply a report, but `aplay -l` successfully enumerated card 0,
  `hiby-sound-card`, device 0, `x1600-i2s-pcm cs43131-hifi-0`, with one available
  subdevice. Audio routing, gain and actual playback are untested.
- Two power supplies identify as Battery: `axp_battery` reported 0% and zero
  voltage; `battery` reported 75% and 4,173,000 microvolts during the baseline.
  PocketOS correctly reports `battery_ambiguous=1` and unknown battery/charging
  state. Do not choose a fuel gauge or enable suspend from those values alone.
- `removable_sd_candidate=0` is a discovery limitation, not absence of the
  mounted card. Use the observed mount/block mapping for future SD detection.
- Wi-Fi exists. No Bluetooth HCI interface was detected in the runtime smoke
  test, even though the Bluetooth class exists. Neither radio was toggled.
- The init draft retains 40 review blockers and two deferred radio drivers.
  Inert vendor script inspection explains important blockers: rmem parameters
  derive from boot arguments; I2C bus 3 and GPIO keys require explicit sysfs
  setup; PMIC regulator parameters contain quoted compound values. No vendor
  script was executed and no module was loaded or unloaded.

## Recovery evidence and remaining gates

The user-supplied `Downloads/r1.upt` has SHA-256
`7617a76b212044ea0a3b533eb74cd3d04c28100cc3a08fd058834ef8ba6c4588`,
matching [SonixOS 1.3.0](https://github.com/Jepl4r/sonix-player/releases/tag/1.3.0).
It is not an untouched stock HiBy recovery package.

A separate untouched R1 V1.6 file was downloaded from the
[folder](https://drive.google.com/drive/folders/1A2RIMdvuZRzGCNMY9F81vtGAAs2FJ46W)
linked by [HiBy's R1 V1.6 support entry](https://store.hiby.com/apps/help-center).
It is 41,652,224 bytes, SHA-256
`9aada81995d8d2b2ed80d6cf292c62bc5f0f705e51e4f69c7e766ee67536ba60`.
All image chunks and whole-image MD5 values were checked offline. The manifest
contains only kernel (3,731,520 bytes) and rootfs (37,507,072 bytes) payloads.
The local SHA-256 identifies the saved copy; internal MD5 checks do not
authenticate firmware. No independent vendor signature/checksum was obtained.

The [R1 manual](https://guide.hiby.com/en/docs/products/audio_player/hiby_r1/guide/)
documents forced SD firmware upgrade, but its button table and later failed-boot
instructions describe different button combinations. Confirm the correct
procedure for this unit before use. Upgrade mode writes firmware; it does not
establish independent SD boot. Stock restoration has **not** been tested.

Before a UI smoke test, resolve the libc/runtime strategy, record framebuffer
ioctl geometry and input mapping, and design an explicit return to Sonix without
autostart or simultaneous display ownership. Confirm hardware revision and the
recovery procedure. Keep audio muted and power/radio controls disabled initially.
No PocketOS boot test, flashable image, updater publication or firmware install
is authorized by these results. Recovery restore and independent boot remain
unverified; preserve the existing bootloader and secondary partitions.

## Rebuild just the diagnostics

First verify and relocate the SDK/rootfs pair with the existing provenance
checks. Then, on a Linux host:

```sh
bash tools/build-hardware-diagnostics.sh /path/to/pocketos-sdk /path/to/sdk-manifest.json /new/output/directory
qemu-mipsel /new/output/directory/pocketos-info --version
qemu-mipsel /new/output/directory/pocketos-r1-runtime --version
```

The helper only builds on the host. It does not connect to or install on a
device. Source and binary hashes, SDK identity, ELF examination reports and
explicit non-installable flags accompany the output. No Buildroot rebuild is
needed. Raw device captures remain local because they are device-specific.
