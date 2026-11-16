# PocketOS development roadmap

- Phase 1: independent Buildroot userspace, verified Update 13 reference, ABI comparison, MIPS user-mode tests. No device writes.
- Phase 2: choose a matching libc/float ABI; integrate the exact existing R1 kernel and driver ecosystem without altering bootloader, flash layout, or recovery. Validate display, touch, buttons, battery, audio, SD (including hotplug), USB, Wi-Fi, and Bluetooth individually. Investigate existing device support and licensing before copying any binaries. 
- Phase 3: integrate the player UI with PocketOS-owned settings and update channels. Keep downloaded content on microSD, build per-game settings and preserve user-owned saves.
- Phase 4: isolate games in a supervised child process with bounded shared framebuffer/audio/command IPC. Add on-demand services and measure memory, idle CPU, frame times, audio underruns and battery relative to the tested Sonix Books version, on the same hardware and games.
- Phase 5: verify recovery procedure and partition-size constraints; if a safe offline or SD development boot is supported, use it first. Ship only explicitly approved, hardware-tested images via an independent PocketOS channel.

Performance targets (not measured): less idle memory and CPU usage; more consistent GBA frame times without clock overclocking; no regression in basic playback, reading, networking or recovery. 
