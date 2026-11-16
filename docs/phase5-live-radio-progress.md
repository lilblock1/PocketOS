# Phase 5 radio integration progress

Base: `phase5-cumulative-source`, commit `f67271e`.

Physical R1 observations, using RAM-only service/application overlays:

- Matching firmware/NVRAM and the existing kernel module pair create WLAN;
  native supplicant scans return nearby networks.
- Bluetooth firmware download on the observed UART succeeds with one transport
  owner. BlueZ exposes a powered controller and discovery succeeds.
- Current foundation lacks the standalone timeout utility assumed by earlier
  service scripts. A small bounded child runner now supplies that function.
- Bluetooth audio registration fails with the foundation's existing daemon:
  its Media1 interface is absent. Building an audio-enabled BlueZ daemon and
  SBC-only BlueALSA using the existing SDK; no Linux/kernel rebuild.
- Added independent radio permission, DHCP event plumbing and RAM live staging.
  Wi-Fi association/address acquisition, Bluetooth pairing/audio and persistence
  still need physical tests. Do not describe these as completed features yet.
- Bootloader, kernel, recovery and boot-selection hashes match their saved
  values. No MTD writes or recovery operations were performed.

Keep device identifiers, network names/passwords, books/music, flash backups,
private paths and raw logs out of commits. Only source and sanitized status
belong here. Next: verify real Wi-Fi connectivity and wireless audio, then
measure memory/CPU/battery and optimize without changing the tested kernel.
