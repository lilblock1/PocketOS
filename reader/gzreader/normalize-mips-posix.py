#!/usr/bin/env python3
"""Normalize compiler-verified MIPS glibc FFI declarations for LuaJIT.

The upstream ffi-cdecl generator emits target-specific, ABI-correct C
declarations, but preserves glibc-private aliases that LuaJIT's FFI has not
seen before, and adjacent GCC asm string literals in an invalid form.

Only the GNU Linux *MIPS32* generated output is touched. This script does
NOT alias MIPS to ARM and does not relax the generator's C static assertions.
Callers must first compile the MIPS type-compatibility assertions using the
same cross compiler and ABI flags as the declaration generator.
"""
import argparse
from pathlib import Path
import re

# Verified by target glibc's __builtin_types_compatible_p static assertions,
# NOT by guessing sizes on the host architecture.
PRIVATE_TYPES = {
    "__int64_t": "int64_t",
    "__uint64_t": "uint64_t",
    "__suseconds64_t": "int64_t",
}

ASM_SPLIT_LITERAL = re.compile(r'__asm__\("""([A-Za-z_][A-Za-z0-9_]*)"\)')


def normalize(source: str) -> str:
    if source.count("linux_mipsel=") != 1:
        raise ValueError("Expected exactly one native linux_mipsel platform")
    if "linux_gnu_mipsel=" in source or "linux_arm=0x40" in source:
        raise ValueError("Refusing mismatched or ARM-aliased ABI")
    if "Automatically generated with" not in source[:200]:
        raise ValueError("Expected compiler-generated upstream POSIX cdefs")
    fixed = source
    counts = {}
    for old, new in PRIVATE_TYPES.items():
        fixed, n = re.subn(r"\b" + re.escape(old) + r"\b", new, fixed)
        if n < 1:
            raise ValueError(f"Expected private MIPS glibc type {old}")
        counts[old] = n

    fixed, n_asm = ASM_SPLIT_LITERAL.subn(r'__asm__("\1")', fixed)
    if n_asm < 1 or "__asm__(\"\"\"" in fixed:
        raise ValueError("Unexpected GCC symbol redirect syntax")
    if re.search(r"\b__(?:int64_t|uint64_t|suseconds64_t)\b", fixed):
        raise ValueError("Unresolved MIPS-only internal glibc aliases")
    print(f"Normalized MIPS FFI private type references: {counts}; "
          f"retained {n_asm} GCC symbol redirects")
    return fixed


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    original = a.source.read_text()
    fixed = normalize(original)
    if a.output.exists() and a.output.resolve() != a.source.resolve():
        p.error("Refusing to overwrite existing output")
    a.output.write_text(fixed)


if __name__ == "__main__":
    main()
