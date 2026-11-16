# Phase 2E: safe boot and recovery

Official R1 documentation: https://guide.hiby.com/en/docs/products/audio_player/hiby_r1/guide/
Official firmware catalog: https://guide.hiby.com/en/docs/products/audio_player/hiby_r1/firmware/
Official firmware download guidance: https://store.hiby.com/apps/help-center

The R1 manual describes recovery/firmware upgrade from microSD when normal boot fails: put the exact model's known-good .upt on the card and use the documented play/pause plus power combination from power-off. This is a firmware installation, not a non-writing SD boot. Do not invoke it for this phase. No restore was tested. Other models and hardware revisions may differ.

Before a device boot experiment, obtain a verified model-specific stock recovery package, back up SD and user data, establish actual kernel/rootfs partition mapping and usable capacity, review unresolved driver initialization, and verify the independent boot/debug method. Never flash the development artifacts. No bootloader or recovery writes are authorized.

The hardware workflow builds a temporary xz-compressed squashfs solely to measure staged rootfs size, compares it with the reference, then deletes it. This does not prove flash capacity; raw partition sizes can omit bad-block/UBI overhead. The boot report cannot enable installation, even if a candidate fits. Only reports and read-only scripts are uploaded. The optional boot-probe.sh collects /proc/mtd and UBI capacity without reading raw flash devices or changing settings; on an already ADB-enabled R1 run it via adb shell sh, e.g. from Windows cmd: adb shell sh < hardware\hiby-r1\boot-probe.sh > r1-boot-baseline.txt . For offline parsing save only the /proc/mtd table to a separate file. Do not enable ADB or modify the device merely to run this CI workflow.
