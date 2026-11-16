#!/usr/bin/env python3
"""Match the pinned player float ABI conservatively, before cross-compiling."""
import re
import subprocess
import sys
from pathlib import Path

reference, original, output = map(Path, sys.argv[1:])
player = reference / "usr/bin/sonix_player"
assert player.is_file() and not player.is_symlink(), "Missing pinned player"
report = subprocess.check_output(["readelf","-h","-A",str(player)], text=True)
assert "ELF32" in report and "little endian" in report and "MIPS" in report
match = re.search(r"^\s*FP ABI:\s*(.+)$", report, re.M)
if not match:
    match = re.search(r"Tag_GNU_MIPS_ABI_FP:\s*(.+)", report)
assert match, "Missing FP ABI: inspect the reference before choosing toolchain flags"
abi = match.group(1)
config = original.read_text()
assert "BR2_MIPS_SOFT_FLOAT" not in config
if "Soft float" in abi:
    config += "\nBR2_MIPS_SOFT_FLOAT=y\n"
elif "Hard float" in abi:
    config += "\n# BR2_MIPS_SOFT_FLOAT is not set\n"
    # FPXX can interoperate with many o32 hard-float binaries, but matching
    # the exact ABI and full library dependencies is a separate later gate.
    if "32-bit CPU, 32-bit FPU" in abi:
        config += "BR2_MIPS_FP32_MODE_32=y\n"
    elif "Any FPU" in abi:
        config += "BR2_MIPS_FP32_MODE_XX=y\n"
    elif "64-bit FPU" in abi:
        config += "BR2_MIPS_FP32_MODE_64=y\n"
    else:
        raise SystemExit("Unsupported hard-float ABI: "+abi)
else:
    raise SystemExit("Unsupported FP ABI: "+abi)
overlay=original.resolve().parent/"rootfs-overlay"
assert overlay.is_dir()
config += 'BR2_ROOTFS_OVERLAY="'+str(overlay)+'"\n'
output.write_text(config)
print("Reference FP ABI:",abi)
