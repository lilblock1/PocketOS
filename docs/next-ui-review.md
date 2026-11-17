# Next UI review: accessible navigation within the current RAM budget

Owner direction: an entirely new classic music-player interface, with clean
list-based menus and a strong selection bar. Use the selected classic iPod-style
direction rather than extending the prior card grid. Retain the approved fonts,
native display proportions and lightweight renderer, and redesign navigation,
status information and Now Playing as one visual system.

Show the proposed UI and its memory comparison, and obtain the owner's approval
before applying the new UI to the device. Firmware installation also requires
review/approval. Preparing source, host previews and tests is permitted; a
published or staged file is not permission to install it.

The verified sequence-2 UI was observed at 2,312 KiB RSS and a 2,704 KiB process
high-water mark. This is the UI process, not whole-system memory. Page/state was
not normalized for a benchmark; the proposal still needs an approved temporary
device test for a matching-state RAM comparison.

The redesign target is the same or lower memory. Compare identical Home,
Settings, library and reader states on the same hardware, fonts and content.
Keep full-frame buffer count and dimensions unchanged; reuse existing lists
and navigation state. Report writable static memory/code footprint in the
preview review, then measure actual device RSS and peak during an approved
test. Do not claim the hardware budget is proven by host previews alone.

This document records the requested design and approval constraints. A new
user-friendly UI has not yet been applied or certified to meet the RAM target.
