#!/usr/bin/env python3
"""Repack and round-trip-test an R1 UPT ISO *on the CI runner only*.

This tool does NOT certify bootability or produce a release. All build outputs
are disposable. Only the pre-verified, pinned R1 reference is accepted by the
calling workflow. Never publish its generated image without physical R1 gates.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

CHUNK = 524288
IMAGES = {"kernel": "xImage", "rootfs": "rootfs.squashfs"}
MAX_IMAGE = 128 * 1024 * 1024


def md5(data):
    return hashlib.md5(data).hexdigest()


def regular(path):
    return path.is_file() and not path.is_symlink()


def metadata(path):
    if not regular(path) or path.stat().st_size > 16384:
        raise ValueError("Missing or oversized OTA image manifest")
    records, current = [], None
    for raw in path.read_text(encoding="ascii").splitlines():
        if "=" not in raw:
            continue
        key, value = map(str.strip, raw.split("=", 1))
        if key == "img_type":
            current = {"img_type": value}
            records.append(current)
        elif key.startswith("img_"):
            if current is None or key in current:
                raise ValueError("Invalid image record")
            current[key] = value
    if len(records) != 2:
        raise ValueError("Wrong number of images")
    if {r.get("img_type"): r.get("img_name") for r in records} != IMAGES:
        raise ValueError("Image layout mismatch")
    for r in records:
        if set(r) != {"img_type", "img_name", "img_size", "img_md5"}:
            raise ValueError("Invalid OTA record fields")
        if not r["img_size"].isdigit() or not 0 < int(r["img_size"]) <= MAX_IMAGE:
            raise ValueError("Invalid image length")
        if len(r["img_md5"]) != 32 or any(c not in "0123456789abcdef" for c in r["img_md5"]):
            raise ValueError("Invalid OTA digest")
    return {r["img_type"]: r for r in records}


def verify_chunks(ota, record):
    name, size, checksum = record["img_name"], int(record["img_size"]), record["img_md5"]
    manifest = ota / ("ota_md5_" + name + "." + checksum)
    if not regular(manifest):
        raise ValueError("Missing hash manifest: " + name)
    expected = manifest.read_text(encoding="ascii").splitlines()
    count = (size + CHUNK - 1) // CHUNK
    if count != len(expected):
        raise ValueError("Wrong chunk count")
    overall = hashlib.md5()
    previous = checksum
    for idx, digest in enumerate(expected):
        if len(digest) != 32 or any(c not in "0123456789abcdef" for c in digest):
            raise ValueError("Invalid chunk digest")
        path = ota / f"{name}.{idx:04d}.{previous}"
        if not regular(path) or path.stat().st_size != min(CHUNK, size - idx * CHUNK):
            raise ValueError("Invalid chunk size or name: " + str(path))
        chunk = path.read_bytes()
        if md5(chunk) != digest:
            raise ValueError("Chunk digest mismatch: " + str(path))
        overall.update(chunk)
        previous = digest
    if overall.hexdigest() != checksum:
        raise ValueError("Overall image checksum mismatch")
    other = list(ota.glob(name + ".*.*"))
    if len(other) != count:
        raise ValueError("Unexpected image chunks")
    return {"bytes": size, "md5": checksum, "chunks": count}


def validate_tree(iso):
    if not iso.is_dir() or iso.is_symlink():
        raise ValueError("Invalid ISO tree")
    # Reject all symlinks and unexpected top-level paths.
    for p in iso.rglob("*"):
        if p.is_symlink():
            raise ValueError("Symlink in ISO tree")
    if set(p.name for p in iso.iterdir()) != {"ota_config.in", "ota_v0"}:
        raise ValueError("Unexpected top-level paths")
    ota = iso / "ota_v0"
    for filename in ("ota_update.in", "ota_v0.ok", "ota_compatibility.in"):
        if not regular(ota / filename):
            raise ValueError("Missing OTA compatibility marker: " + filename)
    if not regular(iso / "ota_config.in"):
        raise ValueError("Missing config")
    records = metadata(ota / "ota_update.in")
    return {name: verify_chunks(ota, record) for name, record in records.items()}


def replace_rootfs(iso, image):
    if not regular(image) or not 0 < image.stat().st_size <= MAX_IMAGE:
        raise ValueError("Invalid replacement rootfs image")
    ota = iso / "ota_v0"
    before = metadata(ota / "ota_update.in")
    # Keep reference kernel and compatibility entries byte-for-byte.
    previous_kernel = verify_chunks(ota, before["kernel"])
    for p in ota.glob("rootfs.squashfs.*.*"):
        p.unlink()
    for p in ota.glob("ota_md5_rootfs.squashfs.*"):
        p.unlink()
    whole = hashlib.md5()
    with image.open("rb") as f:
        for chunk in iter(lambda: f.read(CHUNK), b""):
            whole.update(chunk)
    checksum = whole.hexdigest()
    previous, hashes = checksum, []
    with image.open("rb") as f:
        for idx, chunk in enumerate(iter(lambda: f.read(CHUNK), b"")):
            (ota / f"rootfs.squashfs.{idx:04d}.{previous}").write_bytes(chunk)
            previous = md5(chunk)
            hashes.append(previous)
    (ota / f"ota_md5_rootfs.squashfs.{checksum}").write_text("\n".join(hashes) + "\n", encoding="ascii")
    original = (ota / "ota_update.in").read_text(encoding="ascii")
    lines, rootfs = [], False
    seen = set()
    for line in original.splitlines(keepends=True):
        if line.strip() == "img_type=rootfs":
            rootfs = True
        elif line.strip() == "img_type=kernel":
            rootfs = False
        key = line.split("=", 1)[0].strip() if "=" in line else ""
        if rootfs and key == "img_size":
            if key in seen:
                raise ValueError("Duplicate rootfs size")
            line = f"img_size={image.stat().st_size}\n"
            seen.add(key)
        elif rootfs and key == "img_md5":
            if key in seen:
                raise ValueError("Duplicate rootfs checksum")
            line = f"img_md5={checksum}\n"
            seen.add(key)
        lines.append(line)
    if seen != {"img_size", "img_md5"}:
        raise ValueError("Rootfs manifest fields not found")
    (ota / "ota_update.in").write_text("".join(lines), encoding="ascii")
    after = validate_tree(iso)
    if after["kernel"] != previous_kernel or after["rootfs"]["bytes"] != image.stat().st_size:
        raise ValueError("Kernel changed or rootfs size mismatch")
    return {"original_kernel": before["kernel"]["img_md5"], "rootfs": after["rootfs"]}


def build_and_verify(reference_iso, rootfs, output, report):
    if output.exists() or report.exists():
        raise ValueError("Refuse existing output")
    if not regular(rootfs):
        raise ValueError("Missing rootfs")
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        clone = work / "iso"
        shutil.copytree(reference_iso, clone, symlinks=True)
        original = validate_tree(clone)
        changed = replace_rootfs(clone, rootfs)
        subprocess.run(["genisoimage", "-quiet", "-iso-level", "3", "-R", "-J",
                        "-o", str(output.resolve()), str(clone)], check=True)
        # Extract the actual output as the updater would see it, and verify
        # every chunk and full MD5 again before considering packaging successful.
        extracted = work / "unpacked"
        extracted.mkdir()
        subprocess.run(["7z", "x", "-y", str(output.resolve()), "-o" + str(extracted)],
                       check=True, stdout=subprocess.DEVNULL)
        found = validate_tree(extracted)
        if original["kernel"] != found["kernel"] or changed["rootfs"] != found["rootfs"]:
            raise ValueError("ISO round-trip changed images")
    report.write_text(json.dumps({
        "format": "R1 OTA v0 ISO", "iso_bytes": output.stat().st_size,
        "original_kernel": original["kernel"], "candidate_rootfs": changed["rootfs"],
        "format_roundtrip_verified": True, "flashable": False,
        "installation_allowed": False, "physical_boot_verified": False,
        "physical_recovery_verified": False,
        "warning": "Offline format test only. This is NOT a firmware release."
    }, indent=2) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("reference_iso", type=Path, help="Already verified reference/iso directory")
    p.add_argument("rootfs", type=Path, help="Disposable squashfs from R1 assembly")
    p.add_argument("output_iso", type=Path, help="Temporary output, never published")
    p.add_argument("report", type=Path)
    a = p.parse_args()
    build_and_verify(a.reference_iso, a.rootfs, a.output_iso, a.report)


if __name__ == "__main__":
    main()
