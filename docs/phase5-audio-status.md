# Phase 5 headphone playback bring-up

The second physical build reports Playback unavailable. Its staged rootfs included aplay/amixer but not the audio permission marker or optional decoder. Current device flags, mixer and PCM state still require read-only diagnostics.

Local commit `6c15d801887981b1a21dec609d16b32c205494e3` adds independent guarded audio approval, specific playback errors, the verified jack prerequisite and pause on unplug, bounded software gain, and named ALSA PCM device selection. Prior conservative route/attenuation settings are retained, not claimed validated. Host application checks passed with the unchanged previously generated reader-font header (system Python Pillow missing); 27 hardware-tool tests passed; MIPS shell and music player compiled. No physical playback test or firmware build. Optional compressed decoder remains to be installed and verified separately.

The exact six-file zero-context source diff is archived in `integration/phase5-audio.patch.xz` (matching Git blob `4472e8ec6f3d49b3af93b5ad35edc691670b560a`). Apply only on exact local parent `e4325e1b63f2605f79313492437b51f5bb01a2db` after `xz -dc phase5-audio.patch.xz | git apply --unidiff-zero --check -`. This patch is not applied to the incomplete remote branch. Use the cumulative source bundle for complete synchronization. No device or flash writes.
