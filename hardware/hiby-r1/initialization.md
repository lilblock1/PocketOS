# Phase 2C: hardware initialization draft

The hardware workflow reads the original verified startup scripts in their actual sequence, extracts only literal insmod invocations with restricted parameters, verifies the staged module and script hashes, checks known module dependency order, and reports unsupported statements and unplanned modules. It never executes vendor scripts. Complex scripts are review blockers, not silently converted into shell commands.

r1-init-plan.json records the proposed module sequence and blockers. r1-module-order.txt is a dry-run view. Neither a successful CI run nor a list of module names confirms a successful device boot. The generated shell draft is staged only on the CI runner; its --apply path always exits without loading anything, even if the plan has no blockers. An enable decision needs a verified boot/recovery path and physical device validation.

No storage mount, format, flash, recovery, clock, charger, suspend or radio writes take place. Audio/display/input driver activation, actual SD mounting and on-demand radio userspace are future hardware-dependent integration gates. No firmware is published and no foundation rebuild is needed.
