# PocketOS Doom — first HiBy R1 port

Native software rendering and touch/button input for the existing R1 kernel.
The upstream engine is [doomgeneric](https://github.com/ozkl/doomgeneric), pinned
to `e257a09527062bfb986b6e5e0780839b6cbd5b71`. The engine and backend are licensed
under GPL-2.0-or-later; the included COPYING file and source bundle accompany
the executable. There is no SDL/X server, extra UI canvas or resident game
service. This first playable version has no sound or music.

## Storage and launch

Store `pocketos-doom` and `doom1.wad` in `SD / PocketOS / Games / doom`.
The immutable system holds the approved engine's SHA-256. The launcher verifies
the SD executable and original shareware data before copying only the executable
to a fresh private RAM directory. The SD mount remains noexec. RAM execution
files are removed on exit. Game configuration and saves stay in the SD game's
`data` folder. PocketOS stops music before handing over the display and input,
waits for the game, and redraws its Games screen afterward.

The original shareware distribution is `doom19s.zip` (2,450,688 bytes), from the
[idgames archive](https://www.gamers.org/pub/idgames/idstuff/doom/).
Archive SHA-256: `cacf0142b31ca1af00796b4a0339e07992ac5f21bc3f81e7532fe1b5e1b486e6`.
Extract the ZIP, concatenate `DOOMS_19.1` and `DOOMS_19.2` in that order, then
extract the resulting LZH archive with 7-Zip. No DOS installer needs execution.
Retain the complete original archive and its notices. The extracted `DOOM1.WAD`
is 4,196,020 bytes, SHA-256
`1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`.
Game data is not part of the GPL engine source bundle or firmware image.

## Controls

- Lower-left directional pad: move forward/back and turn, including diagonals.
- FIRE or physical Volume Up: shoot.
- USE or physical Volume Down: doors and switches.
- WEAPON: cycle weapons the player owns.
- MENU: Doom's menu. ENTER: choose. MAP: automap.
- EXIT: leave Doom and return to PocketOS.

The 320x200 engine image is displayed in a 480x360 viewport with 4:3 aspect
correction. Touch controls occupy the remaining panel. The current framebuffer
format and page are reused; bootloader, recovery and display mode are untouched.
The UI's light/dark colour tables add no writable canvas or image cache. Doom's
separate process uses additional memory only while playing; a 6 MiB zone limit
is passed explicitly, and its actual RSS must be measured separately from UI RAM.

## Build

Clone upstream at the pinned commit, then run `tools/build-r1-doom.py` with
`--source UPSTREAM_DIRECTORY --output NEW_OUTPUT_DIRECTORY --cc SDK_GCC`.
Reuse the existing MIPS SDK; do not resolve the compiler wrapper symlink.
The output contains the executable, digest, GPL license, provenance and complete
`pocketos-doom-source.tar.gz`. To rebuild from that source archive, extract it
on Linux and run `make CC=/absolute/path/to/mipsel-buildroot-linux-gnu-gcc`.

`--self-test` checks direction, action merging and weapon press/release behavior.
`--headless FRAMES OUTPUT.ppm -iwad /path/doom1.wad ...` renders without device
access. Headless host/MIPS first-level output was compared byte-for-byte.
The owner tested the first build on the R1 and confirmed Doom and EXIT worked.
