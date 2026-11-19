# GZreader-only PocketOS: first physical R1 test (NO flashing)

This is the **first real-hardware test** for the GZreader/ZenOS-only PocketOS firmware.
It deliberately does **not** install a root filesystem, change an OTA channel,
alter NAND partitions, reboot, start a graphical reader, or write to recovery.

GitHub QEMU user-mode checks have passed the native POSIX, framebuffer, input,
RGB565 and GZreader/ZenOS entry-point tests. They **do not** prove the actual
HiBy R1 kernel can mount an executable SquashFS from the noexec FAT32 card,
or that the LCD, touchscreen or RAM can run the UI.

## Before starting

You need an R1 **already running independent PocketOS** with working *root*
USB ADB; a FAT32 microSD card; a backup of anything important; and a known
recovery/restore route. If it still runs SonixOS or ADB is not root, don't
attempt the loop mount. The read-only `--audit` command can still collect
useful information without mounting anything.

Use the successful unsigned and non-flashable candidate artifact:

https://github.com/lilblock1/PocketOS/actions/runs/44451882960

Under **Artifacts**, download
`GZreader-R1-ONLY-CANDIDATE-AND-BUILD-REPORT-NOT-FIRMWARE` and extract it on
your Windows computer. The relevant files are in the extracted `candidate`
directory: `GZreader-R1-SD-RUNTIME.squashfs` and
`reader-edition-report.json`.

### Windows PowerShell

1. Copy **only** `candidate/GZreader-R1-SD-RUNTIME.squashfs` to
   `PocketOS/GZreaderRuntime/GZreader-R1-SD-RUNTIME.squashfs` on the
   existing FAT32 microSD. Keep the file byte-identical. Safely eject and put
   the SD card back in the running R1.
2. Save
   `https://raw.githubusercontent.com/lilblock1/PocketOS/reader-edition-gzreader/tools/r1-gzreader-hardware-probe.sh`
   as `r1-gzreader-hardware-probe.sh` in PowerShell's current folder.
3. Connect USB ADB to the **already booted** R1. Run:

```powershell
adb devices
adb shell id
adb push .\r1-gzreader-hardware-probe.sh /tmp/r1-gzreader-hardware-probe.sh
adb shell sh /tmp/r1-gzreader-hardware-probe.sh --audit
$hash = (Get-Content .\candidate\reader-edition-report.json -Raw | ConvertFrom-Json).sd_payload_sha256
adb shell sh /tmp/r1-gzreader-hardware-probe.sh $hash
```

The `--audit` mode reads the existing kernel, SD mounts, RAM, framebuffer
attributes and touchscreen device names; it does not mount or write anything.

The SHA256 mode authenticates the SD image before trying a read-only SquashFS
loop mount under `/run`, checks the GZreader and ZenOS plugins, executes
the native MIPS LuaJIT version command and unmounts the temporary image.
Only `/dev/loop0` may be created in **volatile devtmpfs**, and only if
absent; the script cleans up any node/mountpoint that it created.

## Interpretation

`PASS` means that the **real R1** can hash, read-only-mount and execute
the installed native GZreader runtime from microSD. It is not yet a GUI
boot test, and it does **not** mean the unsigned candidate is safe to flash.

`BLOCKED`: save the whole ADB output. Do not circumvent any guard or
replace firmware. We must fix the actual kernel, loop, mount or runtime issue
before attempting a signed installation.

Even with a PASS, the next checks are real LCD framebuffer pixel layout,
touch events, memory use, GZreader GUI startup and recovery/rollback.
A staged signed `.upt` is not proof of a physical boot.

**Never copy `reader-rootfs.squashfs` to the firmware updater, install an
unsigned image, run `flash_erase` or use `dd` against MTD partitions.**
