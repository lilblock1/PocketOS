# Phase 5 pre-release package

The requested pre-release is a complete main-system/rootfs package for the R1,
using the existing proven kernel. It is not a UI-only replacement. Its UPT must
contain only the primary rootfs payload; no kernel, bootloader, recovery or
userdata payload may be added.

**This preview has no on-device update access.** Do not provision an update public
key or enable the updater UI. Online and microSD installation from PocketOS is
work for the next release. Retain the existing read-only update status code.

Build with the reused SDK, reviewed board sequence, USB-only ADB, current apps,
on-demand radio services and existing-userdata settings support. Enable book
preparation/resume and microSD data writes explicitly, while keeping the card
`nosuid,nodev,noexec`. Optional FFmpeg codecs, reader font atlases and their
sources/licenses are separate microSD additions rather than internal rootfs data.

Publish only the reviewed UPT, optional SD additions, checksums and public build
sources/licenses. Never package live settings, saved networks, Bluetooth device
records, raw NAND backups, signing keys, personal files or host logs.

Before uploading: verify all core ELF ABIs and runtime dependencies, native/font
checks, rootfs compression/size, absence of private data and updater enablement,
and reconstruction through the captured recovery updater in an inert fixture.
State cold-boot validation honestly; RAM-only feature tests do not establish it.

## Packaged checkpoint

- Build ID: `2849219495a83c35`.
- UPT size: 13,289,472 bytes.
- UPT SHA-256: `fa36d95736555dba9ce95e33a29227668270d524f1cb419a751506f59d10a074`.
- Native MIPS application version checks passed with the packaged libraries.
- Captured recovery scripts reconstructed the exact image in an inert fixture,
  selected only primary rootfs writes and returned boot selection to primary.
- Private-data and disabled-updater checks passed. No live settings/backups were
  packaged. No kernel, bootloader, recovery or userdata payload is present.
- Uploaded `r1.upt`, `SHA256SUMS.txt` and `README.txt` to the pre-release draft.
- Cold boot of this newly packaged image remains unverified. No installation was
  performed to produce this release file; the device's current live session was
  preserved. Bluetooth reconnect/disconnect work remains deferred.
