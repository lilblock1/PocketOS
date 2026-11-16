#!/usr/bin/env python3
"""Inspect the pinned public reference only. Never construct an OTA or access a device."""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

archive, manifest_path, out = map(Path, sys.argv[1:])
ref = json.loads(manifest_path.read_text())["reference"]
assert hashlib.sha256(archive.read_bytes()).hexdigest() == ref["sha256"], "Reference digest mismatch"
out.mkdir(parents=True, exist_ok=False)
subprocess.run(["7z", "x", "-y", str(archive.resolve()), "-o"+str(out.resolve()/"iso")], check=True, stdout=subprocess.DEVNULL)
ota = out/"iso"/"ota_v0"
records = []
for line in (ota/"ota_update.in").read_text().splitlines():
    if "=" not in line:
        continue
    key, value = map(str.strip, line.split("=", 1))
    if key == "img_type":
        records.append({})
    if key.startswith("img_"):
        assert records and key not in records[-1]
        records[-1][key] = value
assert {(r["img_type"],r["img_name"]) for r in records} == {("kernel","xImage"),("rootfs","rootfs.squashfs")} and len(records)==2
for record in records:
    name, checksum, size = record["img_name"],record["img_md5"],int(record["img_size"])
    assert re.fullmatch("[a-f0-9]{32}",checksum) and 0<size<=128*1024*1024
    hashes=(ota/f"ota_md5_{name}.{checksum}").read_text().splitlines()
    assert len(hashes)==(size+524287)//524288
    previous=checksum
    whole=hashlib.md5()
    output=out/("reference.squashfs" if name=="rootfs.squashfs" else "reference-kernel.bin")
    with output.open("xb") as target:
        for index, expected in enumerate(hashes):
            assert re.fullmatch("[a-f0-9]{32}",expected)
            chunk=(ota/f"{name}.{index:04d}.{previous}").read_bytes()
            assert len(chunk)==min(524288,size-index*524288)
            assert hashlib.md5(chunk).hexdigest()==expected
            whole.update(chunk)
            target.write(chunk)
            previous=expected
    assert output.stat().st_size==size and whole.hexdigest()==checksum
print("Pinned reference and all image chunks verified; no OTA created.")
