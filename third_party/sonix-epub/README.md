# Sonix EPUB parsing components

Source: https://github.com/lilblock1/Sonix-Books/tree/2920fe1c5ca3b4145b4d06251492ab87381e9817/sonix-player/src/system/ebook

The arena, ZIP reader and XML scanner are from that exact commit, licensed under
GNU GPL version 3 (see COPYING). PocketOS uses them in a separate GPL-licensed
`pocketos-book-prepare` executable. Include this complete source directory, the
helper source and its build instructions with any distributed helper binary.

Local adaptations replace the player's image inflater with the existing zlib
dependency, use a relative include, bound publication sizes, reject encrypted
archives and unsafe names, and verify ZIP member CRCs. The native UI does not
link these components; it reads the resulting text cache through a file.
