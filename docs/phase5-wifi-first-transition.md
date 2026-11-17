# Wi-Fi-first firmware update transition

Owner requirement: the next manually installed firmware should be the final
routine removal of the microSD card. Stable and test online updates are the
primary workflow. microSD remains an offline fallback, not a requirement to
remove the card for normal updates. Keep downloads and optional large packages
on the installed card, not internal userdata.

Do not describe this checkpoint as the final manual installation until all
of the following are verified:

- Independently provision a release public key in the read-only rootfs and
  retain the corresponding private key outside the repository.
- Offer only signed matching-channel firmware, compare against installed
  firmware identity rather than staged history alone, and validate rootfs-only
  payload and actual compatible kernel/partition capacity.
- Reverify selected bytes at installation, with explicit confirmation and
  sufficient battery, and use the reviewed existing recovery handoff.
- Verify recovery finds the staged file with the card left inserted, writes
  only the intended primary rootfs, returns to PocketOS, and preserves the
  existing userdata, music and books.
- Cold-boot the new build and test one further complete online update on the
  physical R1. Retain model-correct offline recovery fallback.
- Report download and verification progress separately from recovery writes;
  never claim recovery progress or success before physical evidence.

This CI change compiles and tests the updater application branch with the
existing SDK. Its artifacts are NOT FIRMWARE. It does not ship an update key,
arm recovery, sign or publish a firmware package, or change Bluetooth.
