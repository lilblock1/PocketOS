# KOReader-only PocketOS: first physical HiBy R1 test (NO flashing)

This is the **first real-hardware test for standard KOReader**, not GZreader,
ZenOS, music or games. It does **not** install a root filesystem, change a
Wi-Fi update channel, modify NAND partitions, reboot, launch the reader
graphically, or write to recovery.

The native KOReader MIPS firmware is still experimental. User-mode QEMU
does not emulate the R1's actual kernel, LCD, touchscreen or memory budget.

## Prerequisites

- Your R1 must already be booting **independent PocketOS** with working **root USB ADB** (not SonixOS).
- Use an existing FAT32 microSD card. Back up anything important first.
- Preserve a known-working recovery or restore firmware.
- A failed probe is a **stop** signal, not a reason to bypass checks.

For the **corrected, QEMU-tested** standard upstream KOReader candidate (not GZreader), open the [RO-SD preflight workflow](https://github.com/lilblock1/PocketOS/actions/workflows/pocketos-reader-boot-candidate.yml) and download `KOReader-R1-QEMU-VERIFIED-RO-SD-PREFLIGHT-NOT-FIRMWARE` from its latest successful run. The bundle contains the exact candidate in `candidate/`, the read-only probe at the bundle root, and these instructions. It is produced only after the candidate hashes, GNU MIPS ABI key, original plugin tree, QEMU POSIX/framebuffer/input/RGB565 tests, and original `reader.lua` help test pass. The pinned candidate assembly and QEMU runs are also linked in the workflow's provenance checks. Do **not** copy `candidate/reader-rootfs.squashfs` to the updater.

## Windows PowerShell

1. Copy only `KOReader-R1-SD-RUNTIME.squashfs` onto the FAT32 card under
   `PocketOS/KOReaderRuntime/KOReader-R1-SD-RUNTIME.squashfs`.
   Safely eject and reinsert it in the running R1.
2. Use the included `r1-koreader-loop-probe.sh` at the bundle root. If it is
   missing (for example, when using the original candidate-only artifact),
   retrieve the branch-pinned script from:
   https://raw.githubusercontent.com/lilblock1/PocketOS/reader-edition-koreader/tools/r1-koreader-loop-probe.sh
3. Connect the already booted R1 over USB and run:

```powershell
adb devices
adb shell id
adb push .\r1-koreader-loop-probe.sh /tmp/r1-koreader-loop-probe.sh
adb shell sh /tmp/r1-koreader-loop-probe.sh --audit
$hash = (Get-Content .\candidate\reader-edition-report.json -Raw | ConvertFrom-Json).sd_payload_sha256
adb shell sh /tmp/r1-koreader-loop-probe.sh $hash
```

The `--audit` mode collects read-only kernel, RAM, framebuffer, touchscreen
names and filesystem mount information. It makes no mounts or writes.

The SHA256 mode checks the SD image's exact checksum, makes an optional
temporary `/dev/loop0` node **in volatile devtmpfs** if absent, mounts the
verified image **read-only** under `/run`, checks standard KOReader's original
all 37+ original KOReader plugin directories, executes `luajit -v`, then attempts to unmount and clean up
everything it created. It does not run the graphical KOReader process.

## Interpret results carefully

A `PASS` means only that the **actual HiBy R1** can mount the verified SD
SquashFS and execute native MIPS LuaJIT. This is not a graphical boot or
permission to install the unsigned root filesystem.

A `BLOCKED` result means stop and keep the full ADB output. The real kernel,
SD driver, loop support, noexec policy or native ELF still needs to be fixed.

**Remaining before a signed physical cold boot:** LCD framebuffer addressing
and RGB565 pixels, actual touch event mapping, hardware buttons, GZreader-free
upstream KOReader GUI startup, available RAM and memory pressure, verified
firmware signing, stock recovery and rollback.

**Never flash the unsigned SD/rootfs image or use `dd`/`flash_erase` on MTD.**
