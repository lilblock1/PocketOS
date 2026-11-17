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

## Later physical checks

Wi-Fi association, DHCP, DNS and an HTTPS request succeeded. Native HTTPS
returned HTTP 200. Connection labels now use Connected / Connecting / Not
connected. Password Show/Hide is available only on the device and resets on exit.

The SDK build of BlueZ with audio support and SBC-only BlueALSA starts in RAM;
the SBC source endpoints register successfully. Pairing and audible Bluetooth
playback are still unverified. Bluetooth discovery now streams cached names and
new discoveries, allowing the user to select/pair without waiting for a full
classic inquiry. Routine controller queries have bounded execution, and the
pairing command registers a NoInputNoOutput agent. These latest UI/helper
changes are in testing; do not confuse source synchronization with deployment.

The selected earbuds were subsequently rediscovered in new-device pairing mode.
Pairing and connection succeeded, and BlueALSA exposed an A2DP source PCM for
that device. Music routing and an HCI link check are being integrated next;
audible wireless playback and disconnect behavior are not verified yet.
The incremental build recipe pins all three public source archives and provides
their sources/licenses alongside the resulting audio components.

The owner then confirmed wireless music, Pause and physical volume controls work.
An explicit disconnect request succeeded on the device. The UI disconnect action
is being corrected to use the selected connected output independently of scan
selection; a Disconnect button is also being added to Sound settings. These UI
changes still need physical verification. No persistent radio setup or firmware
update has been installed by this work; live changes remain in RAM.

Follow-up testing found that the earlier one-off connection had not been trusted;
its temporary device entry disappeared after disconnecting. Pair and Connect now
both explicitly trust the selected device, and audio selection verifies Trusted
as well as Connected. A fresh pair/disconnect/reconnect cycle remains to be tested.

After Bluetooth is fixed, the requested next feature is on-device online and
microSD updates, with recovery/bootloader preserved. Then measure and optimize
memory, CPU/display idle work, startup and battery use. Do not enable kernel sleep.

Keep device identifiers, network names/passwords, books/music, flash backups,
private paths and raw logs out of commits. Only source and sanitized status
belong here. Next: verify real Wi-Fi connectivity and wireless audio, then
measure memory/CPU/battery and optimize without changing the tested kernel.
