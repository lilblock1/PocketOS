# KOReader R1 — first read-only hardware test

**This is a development runtime, NOT firmware. Do not flash it.**

GitHub QEMU user-mode tests do not emulate the R1's kernel, actual loop driver, LCD, touchscreen or RAM. Test the unmodified SD payload with the already-running independent PocketOS before considering replacement firmware.

## Preconditions

- Final offline CI has no blockers.
- Device is already booted into independent PocketOS; root ADB works.
- Back up the card and preserve a known-booting R1 recovery firmware image.

## On Windows

1. Open https://github.com/lilblock1/PocketOS/actions/runs/44430582561 and download the non-firmware boot-candidate artifact under Artifacts. Extract the ZIP.
2. Copy only KOReader-R1-SD-RUNTIME.squashfs to the existing FAT32 microSD folder PocketOS/KOReaderRuntime/ (create the folder if absent). Do not copy reader-rootfs.squashfs to the updater.
3. Safely eject and reinsert the card in the device; leave the normal OS running.
4. In PowerShell in the extracted ZIP's folder, run:

~~~powershell
adb devices
adb shell id
$hash = (Get-Content .\reader-edition-report.json -Raw | ConvertFrom-Json).sd_payload_sha256
adb push .\r1-koreader-loop-probe.sh /tmp/r1-koreader-loop-probe.sh
adb shell sh /tmp/r1-koreader-loop-probe.sh $hash
~~~

A PASS means the trusted SD image was verified, mounted read-only, the native MIPS LuaJIT version ran, and the mount was cleaned up. This is still not a KOReader graphical boot.

If a check reports BLOCKED, save the complete output and stop. Do not bypass it or install the unsigned image. This test does not require rebooting, flashing, remounting existing flash, changing updater channels or running KOReader's GUI.

## Still only the real R1 can prove

- Native LCD framebuffer and RGB565 pixel layout, touchscreen event mapping, hardware keys.
- Running the full document engine, all desired plugins and fonts within ~57 MiB usable RAM.
- Firmware signing, installer format, recovery and rollback path.

An offline PASS does not authorize flashing. Hardware-specific recovery and signing remain separate gates.
