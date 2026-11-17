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

    bash finalize-r1-bootstrap.sh /absolute/path/to/preflight.log /absolute/path/to/preview/r1.upt /absolute/path/to/proven-kernel.xImage [SEQUENCE]

The script checks the pinned preview digest and reconstructs its rootfs locally.
It stages only updated apps, provisions a locally held persistent signing key,
builds a system candidate with the exact same proven primary kernel, and prints
the resulting UPT path and hashes. The kernel input must be 2,940,992 bytes with
SHA-256 `9222276c59d7bc9ffb8594106475b986bf66066c4f8f4db99c6fb34cb4fc64f6`.
It rewrites primary kernel and rootfs; bootloader, recovery and userdata have
no payload. This follows the original recovery monitor's two-image contract.
No automatic install or publication is performed. Never upload the private key.
The independent channel uses `POCKETOS-R1-SYSTEM-1` manifests and its own
locally retained key under `~/.local/share/pocketos-only-signing`. Pre-release
and Stable read the repository's `channels/test` and `channels/stable` pointers
to immutable `pocketos-test-N` and `pocketos-stable-N` releases. The pointer only
discovers a release: signatures, exact channel/sequence, installed sequence and
payload digests still establish trust. They never follow the repository's hybrid `test-latest` or generic latest
release. Old `POCKETOS-R1-FW-1` and rootfs-only `POCKETOS-R1-ROOTFS-1` packages
are rejected even with a valid signature. The rootfs-only bootstrap installs
but leaves the original recovery monitor waiting for a kernel-complete flag.
Staging uses `PocketOS-system-updates` on the card, so old hybrid selections
and their sequence numbers cannot prevent or select independent updates.
The packer rejects Sonix player/launcher/server/recovery-daemon binaries and
requires the independent PocketOS UI, startup and updater services in the image.
A physical preflight is not a cold-boot or OTA installation test. Review the
candidate, including recovery behavior, before installing. No generically
downloadable new firmware is provided by this kit.

The Updates screen has three sources: microSD (signed assets in PocketOS-inbox), Pre-release and Stable. A successful staged firmware check presents a separate install confirmation; a matching already-staged firmware may be reverified and retried. The recovery action remains gated by the confirmed sequence and channel. The local builder accepts an optional fourth argument (release sequence); use 2 or higher for an update to the first bootstrap, and retain the existing private key. The default is 1 for a first bootstrap only. A sequence-1 rootfs-only bootstrap needs the corrected updater helper temporarily supplied over ADB for this transition; after sequence 2, it is in the read-only system.
