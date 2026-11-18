# PocketOS system update 4 — modes and SD Doom

The owner confirmed update 3 installed successfully; ADB identifies sequence 3,
build `fc948d28ad584f2c`. They requested Light/Dark throughout the UI and reader,
then Games and a Doom port stored on microSD, followed by the actual update file
and Wi-Fi publication. Theme layout refinements are explicitly deferred.

## Light and Dark with no working-memory increase

Settings → Appearance → Mode toggles Light/Dark using the existing saved theme
field. All normal pages and reading styles follow the mode; fonts, size,
spacing, reading position and stored style are retained. Legacy high-contrast
preferences still load. No image cache, extra canvas or heap allocation is added.

The first draft faulted an extra 4 KiB page. Lossless zero-skip/literal encoding
now also stores the built-in reader glyphs; a streaming cursor draws them
without a glyph cache. Upscaled rows rewind the cursor. Every encoded glyph is
checked byte-for-byte during generation. 504 render matrices cover all 95 ASCII
glyphs, both weights and 21 sizes, including repeated source rows; dense/sparse
pixel hashes match. External Serif/Dyslexic files and their loader are unchanged.
Diagnostic-only functions are kept out of the runtime's hot code path.

A native R1 comparison used fresh processes, identical temporary preferences,
a generic temporary text fixture and the same scripted navigation. Each stage
uses the same fonts/content, with no bookmark writes. Light and Dark both use
less RSS than update 3 in the measured states (KiB):

| State | Update 3 | Light | Dark |
| --- | ---: | ---: | ---: |
| Home | 2,488 | 2,456 | 2,456 |
| Music | 2,496 | 2,464 | 2,464 |
| Settings | 2,496 | 2,464 | 2,464 |
| Books | 2,500 | 2,468 | 2,468 |
| Reader_sans | 2,568 | 2,468 | 2,468 |
| Reader_serif | 2,732 | 2,632 | 2,632 |

Writable working memory is unchanged: 1,408 KiB for normal/sans pages and
1,564 KiB after loading Serif. The final ELF retains the existing canvas and
BSS; no general RAM guarantee for every possible user file is inferred from
this bounded comparison. Raw private logs, preferences and device identifiers
are not published.

## Doom on SD

Home now includes Games → DOOM Shareware. The owner played the first native
port and confirmed Doom and EXIT work. The final backend keeps the menu/exit
row visible and cycles owned weapons; automatic launch/EXIT was rechecked.
The first port is silent. Sound integration is deferred and stated in the UI.
Touch movement/turning, FIRE, USE, MENU, ENTER, MAP, EXIT and physical Volume
Up/Down actions are implemented. Existing display mode/page are reused.

The engine is pinned to doomgeneric
`e257a09527062bfb986b6e5e0780839b6cbd5b71`. Host and MIPS first-level renders match.
Engine self-tests cover actions, press/release merging and weapon selection.
The complete GPL source bundle was rebuilt with the SDK and tested under QEMU.
Game data remains separate from GPL source. The original shareware archive and
verified WAD are on the owner's SD card; personal files are not in releases.

The immutable system pins the optional engine's SHA-256. The executable and
shareware WAD are verified before the engine is copied from the noexec SD card
to a private temporary RAM directory. Configuration/saves remain on SD and the
RAM copy is removed on exit. No game service or WAD is included in the rootfs.
Doom's memory is separate from UI RAM and is used only while the game runs;
the first native game process was observed at 5,136 KiB RSS in gameplay.

Both the signed full-system update and a separate SD engine/source package are
being prepared for pre-release 4. The package retains the exact proven primary
kernel and excludes bootloader, recovery and userdata payloads. Automatic
installation is not part of publication. Sequence-4 cold boot remains untested
until the owner installs it.
