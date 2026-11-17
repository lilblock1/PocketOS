# R1 upgrading screen: kernel completion wait

The independently installed bootstrap 1 reached PocketOS build
`24bfa09b3195e72d`, sequence 1 after the owner's manual reset. ADB confirmed
the PocketOS shell launched. The bootloader, primary kernel, recovery kernel,
recovery rootfs and boot-selection partition still matched the saved references.
This confirms the new primary userspace booted; it does not certify the prior
frozen display as a reliable write-completion signal.

## Reproduced cause

The preserved recovery executable's SHA-256 is
`6a7a9b48eaa530d7d23ff7212c7933acfb59e28b5e98626758dba2f116d48b4d`.
Its monitor starts at `0x40610c`. It unconditionally waits for
`/tmp/ota/xImage.ok` (or a kernel failure marker) before advancing to the
rootfs completion check. A zero kernel-chunk count is clamped to one; it does
not skip the kernel-completion loop. The rootfs-only writer can finish and
restore normal boot selection while this separate monitor waits indefinitely.

`tools/test-r1-recovery-monitor.py` executes that actual MIPS monitor in host
QEMU, after checking the exact executable digest. A fixture-only preload
replaces display, file predicates and command boundaries in process memory.
It never executes the real main program, flash scripts, NAND commands, boot
selection or reboot. With rootfs complete and no kernel marker it reproduces
the endless kernel wait. With both complete it reaches finish state and one
normal-boot request. This is stronger evidence than the earlier hypothesis
about an unregistered progress callback; that hypothesis is not the diagnosis.

The earlier recovery-script-only test missed the separate recovery GUI monitor.
Passing the writer simulation alone was insufficient to certify automatic
completion or reboot.

## Compatible update plan

Keep recovery and bootloader untouched. Follow the original two-image update
layout with the byte-identical proven primary kernel plus PocketOS rootfs.
The only permitted kernel input is 2,940,992 bytes with SHA-256
`9222276c59d7bc9ffb8594106475b986bf66066c4f8f4db99c6fb34cb4fc64f6`.
It matches the running kernel prefix; the rest of its existing partition is
erased padding. No replacement/custom kernel is accepted. This does rewrite
primary kernel `/dev/mtd1` as well as primary rootfs `/dev/mtd2`, even though
the kernel content stays the same. No payload targets bootloader, recovery,
userdata or RTOS. Normal update power-loss precautions still apply.

The signed schema is `POCKETOS-R1-SYSTEM-1`, with the existing independent key.
The repository's small `channels/test` and `channels/stable` discovery pointers
select immutable per-sequence releases. They grant no trust: the signature,
matching channel/sequence, anti-rollback floor and payload hashes are still
checked. Both legacy hybrid and rootfs-only schemas are rejected. The
signer rebuilds the UPT, pins kernel bytes, and rejects Sonix player/launcher/
server/recovery-daemon files in the PocketOS primary userspace. The installer
rechecks the signed plan and existing physical kernel/recovery identities
before handing off. Confirmation explains the identical-kernel rewrite.

The complete writer simulation must now check exact kernel and rootfs bytes,
both final completion markers, primary targets 1/2, temporary boot selection 5,
and no bootloader/recovery/userdata writes. Hardware automatic completion and
reboot remain a separate physical test; simulated success is not that test.
Incremental bar smoothness also remains unverified: the original renderer
polls transient chunk markers, which are not proof of NAND persistence.
