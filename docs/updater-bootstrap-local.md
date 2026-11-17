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
and run the read-only tools/r1-update-preflight.sh via ADB shell. Save its output
locally; require the final POCKETOS-R1-RECOVERY-PREFLIGHT-OK line. Do not reboot
or write to MTD during preflight.

With the kit extracted to a regular Linux filesystem, run:

    bash finalize-r1-bootstrap.sh /absolute/path/to/preflight.log /absolute/path/to/preview/r1.upt

The script checks the pinned preview digest and reconstructs its rootfs locally.
It stages only updated apps, provisions a locally held persistent signing key,
builds a rootfs-only candidate, and prints the resulting UPT path and hashes.
No automatic install or publication is performed. Never upload the private key.
A physical preflight is not a cold-boot or OTA installation test. Review the
candidate, including recovery behavior, before installing. No generically
downloadable new firmware is provided by this kit.
