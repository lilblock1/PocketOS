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

## Published candidate and device staging

System update 2 is published as `pocketos-test-2`, build
`3b57c9b46524dc6d`, 16,271,360 bytes. Its UPT SHA-256 is
`1ba912c86e50d699085657ee28b357db32b27b793b09bc1d0a9ca93c88899b80`.
The implementation commit `e51684c351f50ff83286b3ee0378839e5db85120`
passed the SDK application workflow. Public firmware/signature downloads match
the locally tested files. An altered kernel is rejected before creating a UPT.

On the running sequence-1 device, the corrected helper was supplied in RAM
and temporarily bound over the updater executable. The existing public key
verified the staged system-2 package. A production online channel check on the
R1 resolved the public pointer, verified the signed release, rechecked the
already staged bytes and reported `READY 2 test`. This was a channel/staging
check, not a fresh on-device payload download or an installation.

The corrected signed inbox and card-root `r1.upt` are staged and verified.
The primary boot flag is still normal, installed sequence remains 1, and no
firmware installation/reboot was invoked. The owner now explicitly requires
review and approval before applying UI changes or installing firmware. Physical
automatic completion/reboot remains pending that approval.

## Authorized physical test passed

The owner approved system update 2 while explicitly preserving recovery and
bootloader. The installer reverified the signed package and physical identities,
cleanly unmounted the card, selected existing recovery and requested reboot.
The owner then reported the update worked. PocketOS returned with installed
sequence 2, build `3b57c9b46524dc6d`, and its UI supervisor reported a successful
shell launch. No extra manual-reset command was issued by the development session.

Readback of the exact installed primary rootfs bytes matches the release image
SHA-256 `fd2a763277941163a8b52a767b3fdcfe96b3530107a6f72a30fe0ccce365da8a`.
The full primary kernel is byte-identical to its pre-update reference. Bootloader,
both recovery partitions and the normal boot-selection partition also match
their saved references. Recovery and bootloader were not modified.

The newly booted UI process was observed at 2,312 KiB RSS and 2,704 KiB high-water
mark. This observation is not a controlled before/after UI benchmark. A separate
cold power-cycle and a subsequent fresh online payload-download/install remain
distinct tests; the successful installation used the already verified staged
package after an on-device signed online channel check.
