# Independent PocketOS updater bootstrap

**Superseded update layout:** bootstrap 1 physically booted, but its rootfs-only
package leaves the stock recovery monitor waiting for a kernel-complete flag.
See [the reproduced cause and same-kernel system fix](r1-recovery-monitor-fix.md).
The current schema is SYSTEM-1 and rewrites the byte-identical primary kernel
plus PocketOS rootfs. The rootfs-only statements below describe bootstrap 1.

This branch continues `phase5-r1-updater` and the independently booted Phase 5
preview. The newer reference-based `main`/Phase 6 firmware retains the original
player as fallback and is not the base of this release.

The complete primary system is PocketOS. Its native full-screen UI, display
unblanking, touchscreen navigation, EPUB preparation/fonts, music helper and
radio services remain in the independent rootfs. The separate Phase 6 File
Transfer screen is not included. The proven existing kernel, hardware modules,
bootloader and recovery are retained; this is not a replacement kernel.

## Update isolation

- Independent signed manifest: `POCKETOS-R1-ROOTFS-1`, separate retained local
  signing key, matching installed sequence and channel checks.
- Independent online tags: `pocketos-test-latest`, `pocketos-stable-latest`.
  Neither uses the hybrid `test-latest` nor GitHub's generic latest release.
- Independent card staging: `PocketOS-system-updates`. Legacy hybrid selection
  history remains intact and cannot block or select this channel.
- Signed packages contain only the reviewed rootfs UPT and its report. Packing
  rejects Sonix player, launcher, server and recovery-daemon executables and
  requires the PocketOS startup/UI/update services.
- The existing recovery handles primary-rootfs installation. No kernel,
  bootloader, recovery or userdata payload is included. Only the reviewed
  boot-selection flag is temporarily changed when installation is confirmed.
- Smart Sleep checks the updater's RAM lock throughout SD unmount/handoff.

## Evidence and remaining hardware gate

The connected unit currently runs Sonix. A fresh read-only bootstrap preflight
verified the known primary kernel, both recovery partition hashes, NAND
geometry and primary boot flag. No recovery, bootloader or kernel writes were
performed. The preflight helper is static because stock libc is older than the
PocketOS SDK libc; the installed PocketOS applications use the SDK normally.

The complete application host checks pass, including validly signed legacy
manifest rejection, separate online URLs, legacy history isolation, signature
and checksum failure handling, sequence/channel checks, staged retry and
fixture prohibition on installation. Target applications build with the
existing MIPS SDK. The approved SD decoder still matches its pinned digest.

This checkpoint does not establish a cold boot of the new bootstrap or an
end-to-end Wi-Fi installation. After the owner installs it, verify the new
build identity, display/navigation, Wi-Fi and a signed online channel check;
then build a higher sequence with the same private key and test one complete
download/recovery/return-to-PocketOS cycle. Keep recovery untouched and report
pre-reboot progress separately from actual recovery installation progress.

Bluetooth reconnect/disconnect fixes remain deferred at the owner's request.
No personal settings, credentials, device identifiers, backups, private keys
or raw device logs belong in the public repository or release.
