# PocketOS R1 updater bootstrap kit (not firmware)

GitHub Actions retains read-only repository permissions. The unpublished draft
preview cannot be read with the workflow's integration token (HTTP 403).
The kit therefore carries compiled updater applications and offline tools only.
The repository owner supplies the exact existing Phase 5 draft r1.upt locally.

Required preview SHA-256:
fa36d95736555dba9ce95e33a29227668270d524f1cb419a751506f59d10a074

On Debian WSL install python3, openssl, p7zip-full, squashfs-tools, genisoimage,
and binutils. Verify the kit SHA256SUMS. Push the supplied recovery helper to
/tmp/pocketos-recovery-preflight-helper using USB ADB, chmod 500 on the device,
and run the read-only tools/r1-update-preflight.sh via ADB shell. Use its
`--bootstrap` argument when the R1 currently runs stock/Sonix userspace; it
still verifies the actual kernel, recovery hashes, geometry and boot flag.
Push the script as a file and execute that file, rather than piping commands
into an interactive ADB shell. The supplied preflight helper is statically
linked so it also runs with the stock system's older libc. Save its output
locally; require the final POCKETOS-R1-RECOVERY-PREFLIGHT-OK line. Do not reboot
or write to MTD during preflight.

With the kit extracted to a regular Linux filesystem, run:

    bash finalize-r1-bootstrap.sh /absolute/path/to/preflight.log /absolute/path/to/preview/r1.upt

The script checks the pinned preview digest and reconstructs its rootfs locally.
It stages only updated apps, provisions a locally held persistent signing key,
builds a rootfs-only candidate, and prints the resulting UPT path and hashes.
No automatic install or publication is performed. Never upload the private key.
The independent channel uses `POCKETOS-R1-ROOTFS-1` manifests and its own
locally retained key under `~/.local/share/pocketos-only-signing`. Pre-release
and Stable resolve to `pocketos-test-latest` and `pocketos-stable-latest` release
tags. They never follow the repository's hybrid `test-latest` or generic latest
release. Old `POCKETOS-R1-FW-1` packages are rejected even with a valid signature.
Staging uses `PocketOS-system-updates` on the card, so old hybrid selections
and their sequence numbers cannot prevent or select independent updates.
The packer rejects Sonix player/launcher/server/recovery-daemon binaries and
requires the independent PocketOS UI, startup and updater services in the image.
A physical preflight is not a cold-boot or OTA installation test. Review the
candidate, including recovery behavior, before installing. No generically
downloadable new firmware is provided by this kit.

The Updates screen has three sources: microSD (signed assets in PocketOS-inbox), Pre-release and Stable. A successful staged firmware check presents a separate install confirmation; a matching already-staged firmware may be reverified and retried. The recovery action remains gated by the confirmed sequence and channel. The local builder accepts an optional third argument (release sequence); use 2 or higher for an update to the first bootstrap, and retain the existing private key. The default is 1 for a first bootstrap only.
