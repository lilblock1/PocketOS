# HiBy R1 hardware integration: phase 2

PocketOS must preserve the exact known working kernel and hardware load order until replacement drivers are proven. Never use the foundation rootfs as a firmware file. The driver audit runs on a verified reference image, does not execute vendor scripts or include vendor binaries in artifacts, and groups the dependencies for later integration.

## Physical-device baseline (optional now)
Run from a Windows terminal with Android platform-tools installed and ADB already enabled on the R1:

    adb devices
    cmd /c "adb shell sh < hardware\hiby-r1\probe.sh > r1-baseline.txt"

This is a read-only inspection; it does not enable ADB, root the device, mount anything or load modules. It collects kernel version, memory, aggregate CPU counters, module names and sizes, device node presence, battery/backlight values, ALSA card names and selected mount details. Review the output before sharing. Run again after booting a validated PocketOS candidate, at comparable idle conditions. Device directories alone do not prove that functionality works.

## Integration acceptance gates
- Kernel/modules: exact kernel digest, module vermagic and dependency order; no flash changes.
- Display/input: framebuffer geometry, touch calibration, physical buttons, suspend/wake.
- Audio/power: CS43131 playback, headphone detection, volume, battery/charger safety.
- Storage: mount existing SD read-only first; protect userdata; never run format or erase helpers as part of probing.
- Networking: maintain original radio firmware/licensing, on-demand Wi-Fi/Bluetooth, no copied MAC addresses or credentials.
- Recovery: verified stock recovery path and independent OTA identity, before any installation.

The audit report is an inventory, not driver integration or a boot test.
