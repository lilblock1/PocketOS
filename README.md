# PocketOS

**0.1.0 Alpha — independent embedded Linux for HiBy R1**

PocketOS targets efficient music playback, reading and retro gaming on constrained portable hardware. It is a new distribution, separate from Sonix Books and its stable/test releases. The initial reference platform is HiBy R1 (MIPS32 little-endian, 64 MiB RAM, 128 MiB flash).

## Current stage

Independent Buildroot Linux userspace with glibc, BusyBox init, minimal services and kernel headers from the 4.4 series. The known Update 13 image is downloaded from Sonix-Books, verified by SHA-256 and used only for a read-only ABI/driver inventory. The workflow does not republish the reference binaries.

**The alpha foundation artifact is NOT firmware. Do not flash it or install it through the HiBy updater.** It does not yet contain a bootable R1 system or the music/reading/gaming UI.

## Build on GitHub

The PocketOS foundation workflow builds the MIPS root filesystem, smoke-tests BusyBox using QEMU user mode, runs the compatibility audit, and uploads an explicitly non-flashable artifact and license/source information. It does not create a release or touch internal storage.

## Incremental development builds

| Workflow | What it builds | When to use |
| --- | --- | --- |
| PocketOS foundation | Buildroot Linux, target libraries, reusable compiled MIPS SDK | Explicit dependency/toolchain changes only. Run manually or use the foundation build marker. |
| PocketOS hardware audit | Read-only hardware compatibility report | Board or hardware audit changes; uses a successful existing foundation artifact, never invokes Buildroot. |
| PocketOS applications | Host application checks and, when available, MIPS app binaries via the exported SDK | Application changes; never invokes Buildroot. |

Phase 4C foundation run 43804882930 exported the relocatable SDK and its exact rootfs/config pair. Automatic assembly run 43809187583 compiled and tested the MIPS applications and optional SD decoder successfully. Earlier standalone green application/decoder runs had skipped their target builds while the SDK was unavailable. See [verified build results](docs/phase4c-verification.md) and [Phase 5A physical diagnostic results](hardware/hiby-r1/phase5.md). SDK/rootfs artifacts are retained for up to 90 days. Foundation builds reuse source and compiler caches when available. Select an existing matching foundation for incremental work; expired or mismatched artifacts fail safely without starting a full Linux rebuild.

No workflow here publishes R1 firmware. The exported SDK and application smoke binary are development tools, not a bootable operating system.

## Next stages

See [development roadmap](docs/roadmap.md). The existing kernel and board-specific drivers will be retained initially; replacement components will be validated one at a time. Hardware performance claims require measurements on the actual device.

Existing project: [Sonix Books](https://github.com/lilblock1/Sonix-Books). Buildroot and upstream software retain their respective licenses.
