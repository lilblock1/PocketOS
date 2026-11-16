# Phase 4B — guarded startup
The custom boot splash is rendered by the native shell: one RGB565 canvas, existing font, four frames and at most 240ms of artificial delay. Pending input skips the remainder. No image file, animation daemon or extra process. Host preview frames ship with the apps CI artifact; MIPS preview comparison runs only with a matching SDK.

The rootfs overlay includes inert S95pocketos init script. Its /usr/bin/pocketos-start supervisor is shipped in the application output, not automatically placed into the older foundation rootfs. Once a trusted integrator has verified a non-flash device boot method, hardware initialization, rootfs layout and recovery, staging may install only the matching target shell and supervisor into the development rootfs. It must also provision protected /etc/pocketos/autostart with one line POCKETOS-DEV-AUTOSTART-1 eventN, where N comes from actual verified display/input mapping. No marker is supplied, no default event number is guessed, and no stock OS replacement occurs. The supervisor requires development rootfs identity, trusted executable, /run tmpfs and real char devices. Wait for devices up to 15 seconds, start shell with splash, retry at most twice with two-second backoff, then stop. No hardware mutation flags are passed. No vendor modules, codec activation, persistent config or update activation are started by this service. Status and PID are RAM-only files under /run/pocketos-ui; no unbounded logs. The status records launch and exit, not proof of working UI. Normal shutdown signals the supervisor which forwards SIGTERM to shell; shell restores its opt-in power settings and cleans up. A hard kill may leave a stale lock; only inspect on an independently recoverable test rootfs. No claim that the actual R1 init system or touch mapping is ready.

Tests exercise disabled startup, invalid marker, exact fixed app arguments, bounded retries, normal exit and deterministic splash frames. Host tests are not physical boot verification. Boot gate remains closed, no partition or firmware writes. SDK export still required for target apps. Full independent boot/recovery and physical driver integration remain Phase 4C prerequisites.

## Phase 5 follow-up — permanent startup and display fallback

The first independent-userspace boot log recorded `stat: not found` from the
startup supervisor; a manually launched UI later reported `hyn_ts`, successful
framebuffer unblank, and `FBIOPAN_DISPLAY` failing with EBUSY. The existing
Phase 5 metadata helper is now also used in the init service stop path. New
first-boot candidates provision the explicit `auto` input marker rather than
assuming event1. The supervisor waits for exactly one sysfs input named `hyn_ts`
and its character device, refuses missing/ambiguous matches, and records the
selected node under `/run/pocketos-ui/input`. Explicit legacy eventN markers
remain supported. No touchscreen name is guessed for other boards.

After EBUSY, EINVAL, ENOTTY or EOPNOTSUPP on a page flip, the shell copies the
already-rendered frame to the visible page and retains direct rendering for the
rest of that session. This avoids repeated failing ioctls and redundant copies;
actual display smoothness and power impact are unmeasured. The existing explicit
unblank path remains enabled. No change to flash layout, bootloader, recovery,
radio or writable SD policy is introduced. Host fixtures verify renumbered,
missing and duplicate touch nodes, the existing bounded restart behavior, and
first-boot marker generation. A new cold-boot test on the physical R1 is still
required before calling these fixes verified on hardware.
