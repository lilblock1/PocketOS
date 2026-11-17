# Next UI review: accessible navigation within the current RAM budget

Owner direction: after verifying the recovery completion fix, combine larger
Home buttons/a simple icon grid, clear list views and labels, and simpler
consistent Back/Home navigation. Retain the approved fonts and native display
proportions. Use the existing lightweight native renderer.

Show the proposed UI and its memory comparison, and obtain the owner's approval
before applying the new UI to the device. Firmware installation also requires
review/approval. Preparing source, host previews and tests is permitted; a
published or staged file is not permission to install it.

Current device observation while sequence 1 is running: PocketOS shell RSS
2,524 KiB (2.46 MiB), process high-water mark 2,768 KiB (2.70 MiB). This is the
UI process, not whole-system memory. The page/state was not reset for this
observation, so it is a baseline reference rather than a controlled benchmark.

The redesign target is the same or lower memory. Compare identical Home,
Settings, library and reader states on the same hardware, fonts and content.
Keep full-frame buffer count and dimensions unchanged; reuse existing lists
and navigation state. Report writable static memory/code footprint in the
preview review, then measure actual device RSS and peak during an approved
test. Do not claim the hardware budget is proven by host previews alone.

This document records the requested design and approval constraints. A new
user-friendly UI has not yet been applied or certified to meet the RAM target.
