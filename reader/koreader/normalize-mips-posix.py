#!/usr/bin/env python3
"""Normalize native MIPS POSIX FFI declarations for *upstream KOReader*.

This is a port of the working GZreader MIPS workaround, but uses KOReader's
original GNU-glibc platform key and retains the original KOReader frontend.
The C declarations must have passed real mipsel-linux-gnu-gcc ABI checks
before this script is run. Never alias MIPS to ARM or strip type checking.
"""
import argparse
from pathlib import Path
import re

PRIVATE_TYPES = {
    "__int64_t": "int64_t",
    "__uint64_t": "uint64_t",
    "__suseconds64_t": "int64_t",
}
ASM_SPLIT_LITERAL = re.compile(r'__asm__\("""([A-Za-z_][A-Za-z0-9_]*)"\)')


def normalize(source: str) -> str:
    if source.count("linux_gnu_mipsel=") != 1:
        raise ValueError("Exactly one authentic linux_gnu_mipsel entry required")
    if "linux_mipsel=" in source:
        raise ValueError("Wrong platform map for standard KOReader")
    if "Automatically generated with" not in source[:250]:
        raise ValueError("Only merged, compiler-generated KOReader declarations are supported")
    updated = source
    seen = {}
    for old, replacement in PRIVATE_TYPES.items():
        updated, count = re.subn(r"\b" + re.escape(old) + r"\b", replacement, updated)
        seen[old] = count
    # Old upstream KOReader's setpriority() prototype leaks a glibc-private
    # enum typedef on MIPS. An explicit target-compiled ABI guard verifies
    # its storage/register representation matches int before this rewrite.
    updated, n_priority = re.subn(r"\b__priority_which_t\b", "int", updated)
    if n_priority != 1:
        raise ValueError("Expected one MIPS setpriority() glibc-private enum")
    seen["__priority_which_t -> int"] = n_priority
    missing_aliases = [name for name, count in seen.items() if count == 0]
    if missing_aliases:
        raise ValueError("Expected MIPS ABI aliases absent from compiler output: " +
                         ", ".join(missing_aliases))
    updated, asm_count = ASM_SPLIT_LITERAL.subn(r'__asm__("\1")', updated)
    if '__asm__("""' in updated:
        raise ValueError("Unresolved GNU asm symbol redirection")
    if re.search(r"\b__(?:int64_t|uint64_t|suseconds64_t)\b", updated):
        raise ValueError("Unresolved private libc aliases")
    if sum(seen.values()) + asm_count == 0:
        raise ValueError("Unexpected generator output; review compatibility before proceeding")
    print(f"KOReader MIPS FFI parser cleanup: types={seen}, asm={asm_count}")
    return updated


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    result = normalize(a.source.read_text())
    if a.output.exists() and a.output.resolve() != a.source.resolve():
        p.error("Refusing to overwrite an existing file")
    a.output.write_text(result)


if __name__ == "__main__":
    main()
