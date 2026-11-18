#!/usr/bin/env python3
"""Read only: reconstruct a pinned R1 SYSTEM-1 OTA's kernel/rootfs chunks.

Requires an already extracted, hash-checked UPT ISO directory. No flash, MTD,
recovery, bootloader or device access. Refuses truncated/reordered chunks.
"""
import argparse
import hashlib
import re
from pathlib import Path


def md5(data):
    return hashlib.md5(data).hexdigest()


def extract_one(root: Path, name: str, output: Path, expected_sha: str):
    ota = root / "ota_v0"
    if not ota.is_dir() or ota.is_symlink():
        raise ValueError("Missing OTA directory")
    data = (ota / "ota_update.in").read_text()
    sections = re.split(r"\n\s*\n", data)
    entries = {}
    for section in sections:
        entry = {}
        for line in section.splitlines():
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            entry[key.strip()] = value.strip()
        if "img_type" in entry:
            entries[entry.get("img_name", "")] = entry
    if name not in entries:
        raise ValueError("Missing expected image in OTA plan: " + name)
    entry = entries[name]
    expected_bytes = int(entry["img_size"])
    whole_md5 = entry["img_md5"]
    if not re.fullmatch("[0-9a-f]{32}", whole_md5):
        raise ValueError("Invalid image MD5")
    if expected_bytes <= 0 or expected_bytes > 48 * 1024 * 1024:
        raise ValueError("OTA image size outside conservative limits")
    chunks_file = ota / ("ota_md5_" + name + "." + whole_md5)
    if chunks_file.is_symlink() or not chunks_file.is_file():
        raise ValueError("Missing OTA chunk list")
    lines = chunks_file.read_text().splitlines()
    count = (expected_bytes + 524287) // 524288
    if len(lines) != count or not all(re.fullmatch("[0-9a-f]{32}", x) for x in lines):
        raise ValueError("Invalid OTA chunk digest listing")
    if output.exists() or output.is_symlink():
        raise ValueError("Output exists")
    previous = whole_md5
    output.parent.mkdir(parents=True, exist_ok=True)
    image_sha = hashlib.sha256()
    image_md5 = hashlib.md5()
    total = 0
    try:
        with output.open("xb") as out:
            for index, chunk_hash in enumerate(lines):
                path = ota / f"{name}.{index:04d}.{previous}"
                if path.is_symlink() or not path.is_file():
                    raise ValueError(f"Missing indexed OTA chunk {name} {index}")
                block = path.read_bytes()
                if md5(block) != chunk_hash:
                    raise ValueError(f"Wrong chunk MD5 {name} {index}")
                if len(block) != (524288 if index < count - 1 else
                                  expected_bytes - total):
                    raise ValueError("Wrong chunk size")
                total += len(block)
                image_md5.update(block)
                image_sha.update(block)
                out.write(block)
                previous = chunk_hash
        if total != expected_bytes or image_md5.hexdigest() != whole_md5:
            raise ValueError("Reconstructed OTA image MD5/size mismatch")
        if image_sha.hexdigest() != expected_sha:
            raise ValueError("Pinned image SHA256 mismatch")
    except Exception:
        output.unlink(missing_ok=True)
        raise
    return {"name": name, "size": total, "sha256": image_sha.hexdigest()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--extracted", required=True, type=Path)
    p.add_argument("--rootfs", required=True, type=Path)
    p.add_argument("--kernel", required=True, type=Path)
    p.add_argument("--rootfs-sha256", required=True)
    p.add_argument("--kernel-sha256", required=True)
    args = p.parse_args()
    records = [
        extract_one(args.extracted, "rootfs.squashfs", args.rootfs,
                    args.rootfs_sha256),
        extract_one(args.extracted, "xImage", args.kernel, args.kernel_sha256),
    ]
    for record in records:
        print(record)


if __name__ == "__main__":
    main()
