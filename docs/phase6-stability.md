# Phase 6 reliability

- Wi-Fi join now snapshots enabled profiles, waits up to 15 seconds for the requested profile ID to reach COMPLETED, then restores prior enabled profiles before saving. Wrong passwords and stale associations do not save the new profile; a failed join removes the temporary profile and requests reconnection to prior enabled networks. An interrupted process is still a physical-test case. Association does not prove DHCP or internet connectivity.
- The UI shows installed and staged sequences separately; low-battery firmware downloads are rejected before payload transfer, with the existing independent battery check before recovery.
- The single-confirmation installer preserves signature and partition checks and uses the original recovery. Progress before reboot is preparation progress, not NAND-write progress.
- Test on the R1: successful and failed Wi-Fi passwords, network switching, DHCP renewal, reconnect after reboot and sleep; then one full Wi-Fi firmware update with visible recovery progress and confirmed installed build ID. Do not modify recovery partition to simulate progress.
