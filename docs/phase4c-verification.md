# Phase 4C verification

Verified foundation commit: `3efd419d78d48786a6fe09c2c5d3dca6b3f58fae`.

| Run | Actual result |
| --- | --- |
| [Foundation 43804882930](https://github.com/lilblock1/PocketOS/actions/runs/43804882930) | Success; reusable compiled SDK, paired rootfs/config and legal materials exported |
| [Automatic R1 assembly 43809187583](https://github.com/lilblock1/PocketOS/actions/runs/43809187583) | Success; all five MIPS programs compiled and ran version checks; shell self/power/radio/update UI checks and host/target boot preview comparison passed |
| [Standalone applications 43804568243](https://github.com/lilblock1/PocketOS/actions/runs/43804568243) | Host checks passed; MIPS compile/test/upload skipped because no SDK was available at selection time |
| [Standalone decoder 43803528304](https://github.com/lilblock1/PocketOS/actions/runs/43803528304) | Host checks passed; MIPS compile/test/upload skipped because no SDK was available at selection time |
| [Hardware audit 43804882944](https://github.com/lilblock1/PocketOS/actions/runs/43804882944) | Success; offline/synthetic checks, not a physical hardware pass |

The automatic assembly subsequently built and tested the actual optional MIPS
decoder with the new paired SDK/rootfs. All 12 generated test formats decoded:
FLAC, MP3, AAC, ALAC/M4A, Vorbis/Ogg, Opus, WMA, AIFF, WavPack, TTA, AC3 and
FLAC/Matroska. Lossless cases were compared byte-for-byte with source PCM.
This is short-fixture QEMU validation, not R1 playback, memory or battery testing.

Downloaded artifacts were independently checked against their GitHub ZIP
SHA-256 digests. SDK archive, rootfs archive, config, reference identity,
application SHA256SUMS and assembly core records, decoder checksum and paired
SDK manifest were also checked locally. The five application ELFs are MIPS32
little-endian, and the downloaded host/target boot previews are byte-identical.

| Artifact ID | Artifact | ZIP SHA-256 |
| --- | --- | --- |
| 14200095779 | Compiled MIPS SDK | `8334c692cefd785944dfb8d56b8856271c476109b9e0c31d22b6e2d56d58511f` |
| 14200095786 | Foundation, NOT FIRMWARE | `4ad118d2a7cef892c8e53bdd03a84675594cf098a918a32554184ec6d0fea882` |
| 14200101220 | R1 MIPS applications | `b16523f24424e7af0bd4657ba36bbc7706526710e74525d99dca1a3edd62e41b` |
| 14199996359 | R1 assembly report | `d0a58197860c0ac8e3108916a80c721bdc4bbc872a57b8441548ab88b36fbf47` |
| 14199528222 | Optional SD decoder | `b7fc75412b3a5dca6000ec3634189e5bcefe1219152109df25cc09150d555290` |

The legal-info artifact was listed as present and unexpired; it was not
downloaded or audited locally. The assembly had no failed steps. Logs include
upstream compiler, optional-platform-decoder, pkg-config and Actions Node
deprecation warnings; the selected decoder tests completed successfully.

The scratch combined squashfs was 7,974,912 bytes versus the reference's
44,564,480 bytes. Core applications totaled 494,210 bytes. These are engineering
measurements, not usable partition capacity. The scratch image was removed by
CI and was not published. There are still 40 driver-plan review blockers and
two deferred radio drivers; all boot/recovery/installation gates remain closed.

See [Phase 5A physical results](../hardware/hiby-r1/phase5.md) for the subsequent
read-only board baseline and RAM-only diagnostic smoke test. Use the retained
SDK for incremental work rather than rebuilding Linux.
