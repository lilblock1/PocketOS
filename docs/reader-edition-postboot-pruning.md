# Reader Edition: post-boot slimming plan (DEFERRED)

## Goal
The HiBy R1 boots directly to the real KOReader interface, with the full upstream
KOReader plugin system. No PocketOS home-screen, Sonix media player, games, Doom,
or music UI should be visible. Linux and a small protected update/recovery
foundation remain underneath.

**Do not remove packages from the tested boot chain before there is a working
physical boot.** The present KOReader port is experimental, not flashable.

## Prerequisites before trimming
1. Native little-endian MIPS KOReader builds with LuaJIT FFI and its actual
   document engines, retaining the upstream `.koplugin` tree.
2. Confirm physical cold boot, display color/rotation and evdev touchscreen on R1.
3. Confirm EPUB, PDF, CBZ and at least one real plugin runs; open, paginate,
   annotate, save, resume, and reboot with and without an SD card.
4. Verify OTA package format, original device signing key, recovery handoff,
   partition capacity and downgrade/rollback behavior before changing the
   active Wi-Fi Test pointer.
5. Keep a known-bootable signed image and physical recovery procedure.

## Post-boot audit and trim
* Capture service/process list, shared-library and dynamic-loader dependencies,
  kernel modules in use, memory usage, rootfs/SD usage and startup timings on
  the working device. Review each item before deleting it.
* Remove abandoned launchers, audio decoder/playback services, Doom/game binaries,
  caches and development previews from the **new Reader Edition image**.
* Remove obsolete UI assets and startup scripts only after verifying KOReader's
  direct-launch path; never remove framebuffer, evdev input, SD storage and
  persistence support.
* Keep Wi-Fi and TLS/certificate dependencies needed for book sync, network
  dictionaries, Calibre/OPDS and plugin download. Keep fonts and text libraries
  necessary for Unicode and rendering.
* Preserve the native loader, libc, LuaJIT, MuPDF/document engines, font and
  image codecs, KOReader's upstream plugins and any shared dependencies they
  require. Do not equate "not loaded during one book" with "unused by plugins".
* Prefer storing large, optional fonts, dictionaries and user-installed plugins
  on microSD; avoid reducing the core KOReader plugin API for a small image.
* Keep the signed updater, update public key, recovery entry/handoff, boot services,
  kernel, flash partition map and bootloader untouched.

## Acceptance tests for each slim candidate
* Compare file manifests, hashes, package sizes, boot times, free RAM and
  standby/battery readings against the last known-working Reader Edition image.
* Test cold boot, touch, KOReader file manager, EPUB/PDF/CBZ, font selection,
  plugin loading, bookmarks/progress and SD removal/reinsertion.
* Confirm NO other visible UI and NO music/games binaries.
* Verify images fit the proven R1 partition limits without temporary storage
  exhaustion during updates; no protected-partition payload.
* Sign only with the private key matching the **installed independent PocketOS
  updater**. Keep releases immutable, promote only after verification, and
  leave a rollback-capable older image available.

**Never auto-strip the current installed firmware from an unverified build.**
