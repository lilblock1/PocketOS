#!/usr/bin/env python3
"""Validate SDK and rootfs provenance before using either."""
import hashlib
import json
import os
from pathlib import Path

def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f,"sha256").hexdigest()

sdk=Path("previous-sdk")
root=Path("previous-foundation")
m=json.loads((sdk/"sdk-manifest.json").read_text())
r=json.loads((root/"compatibility.json").read_text())
assert m["schema"]==1 and m["flashable"] is False
assert m["run_id"]==int(os.environ["FOUNDATION_RUN"])
assert m["commit"]==os.environ["FOUNDATION_COMMIT"]
assert r["flashable"] is False and r["fp_abi_matches"]
assert m["rootfs_sha256"]==digest(root/"PocketOS-rootfs-NOT-FIRMWARE.tar.gz")==r["candidate_tar_gz"]["sha256"]
assert m["config_sha256"]==digest(root/"buildroot.config")
assert m["reference_sha256"]==digest(root/"reference.json")==digest(Path("boards/hiby-r1/reference.json"))
assert m["sdk_sha256"]==digest(sdk/"pocketos-sdk.tar.gz")
print("Verified matching compiled SDK and independent root filesystem")
