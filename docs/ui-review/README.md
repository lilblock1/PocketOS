# User-friendly UI proposal — not installed

The owner approved the recovery-fix update 2 test, which successfully returned
to PocketOS with exact system-image readback and unchanged protected partitions.
The owner separately requires a UI preview and approval before applying this
redesign. No redesigned UI executable or firmware has been installed on the R1.

![Proposed Home and Settings](home-settings.png)

The four main Home tiles are Music, Books, Files and Settings. They are taller
than the previous six-tile grid and include short plain-language descriptions.
Quick controls and Device info remain accessible below. The unfinished Games
placeholder is no longer a main Home tile. Settings uses clearer names and
chevrons. Back/Home touch controls are consistent across main pages and media
libraries; Music retains Playing, and Bluetooth retains Disconnect. Busy update,
file-operation, radio and volume-confirmation states retain their guards.
Back restores the previous menu selection; Home preserves active music playback.
The approved reading-page layout and font choices remain intact.

## Memory evidence

The same SDK and approved native font pixels are used for both binaries.

| MIPS ELF metric | Update 2 baseline | Proposed UI |
| --- | ---: | ---: |
| Code + read-only data | 564,340 bytes | 342,020 bytes |
| Initialized data | 1,928 bytes | 1,964 bytes |
| BSS / large working buffers | 1,244,176 bytes | 1,244,176 bytes |
| Total static footprint | 1,810,444 bytes | 1,588,160 bytes |
| Writable load pages (4 KiB) | 306 | 306 |

Static footprint is 222,284 bytes (217.1 KiB) smaller. There is no additional
framebuffer, font-decompression cache, heap allocation, UI framework or polling
service. Native UI glyphs use lossless zero-skip/literal storage and are drawn
directly into the existing RGB565 buffer. Every packed glyph is round-trip
checked; six complete screen renders were pixel-identical before changing layout.

This is not an on-device RAM comparison. The freshly booted update-2 UI was
observed at 2,312 KiB RSS and 2,704 KiB peak; page/state was not normalized for
a benchmark. The proposed UI's actual RSS/peak must be measured in matching
states during an owner-approved temporary test. The same-or-lower RAM requirement
is a release gate, not a claim proven by the ELF totals alone.

## Validation

- Complete application host suite and native navigation/updates tests pass.
- MIPS SDK build, QEMU self-test, navigation busy guards and updater UI tests pass.
- Nine host/MIPS preview images match exactly, including Home, Settings, libraries,
  reader and radios. Font-only compaction preserves all six baseline screenshots.
- The device continues running approved system update 2. Recovery, bootloader,
  radio behavior and installation logic are unchanged by this UI draft.

Images contain deterministic sample time/battery and generic demonstration media.
They are host-rendered native-resolution previews, not device screenshots.
No personal files, credentials, device identifiers or raw logs are included.
