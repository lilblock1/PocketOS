#!/usr/bin/env python3
"""Read-only R1 boot-readiness and partition sizing report; cannot enable flashing."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path

MTD = re.compile(r'^mtd([0-9]+):\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"([A-Za-z0-9_.-]+)"$')

def parse_mtd(text):
    result = {}
    indexes = set()
    for line in text.splitlines():
        if not line.strip() or line.strip().startswith("dev:"):
            continue
        m = MTD.fullmatch(line.strip())
        if not m:
            raise ValueError("Unrecognized MTD record")
        index, size, erase, name = m.groups()
        size, erase = int(size, 16), int(erase, 16)
        if name in result or index in indexes or not erase or not size or size % erase:
            raise ValueError("Invalid/duplicate MTD partition")
        indexes.add(index)
        result[name] = {"index": int(index), "bytes": size, "erase_bytes": erase}
    return result

def size_check(candidate, partitions, name):
    # Do not guess the partition from the total flash or a similar name.
    if name not in partitions:
        return {"image_bytes": candidate, "partition_bytes": None, "fits_raw": None}
    limit = partitions[name]["bytes"]
    return {"image_bytes": candidate, "partition_bytes": limit, "fits_raw": candidate <= limit,
            "raw_margin_bytes": limit-candidate,
            "note": "Raw partition size alone does not account for bad blocks, UBI or updater overhead."}

def sha(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()

def inspect_init(root):
    init = root / "sbin/init"
    # Never resolve an absolute target symlink against the host filesystem.
    if init.is_symlink():
        link = os.readlink(init)
        target = os.path.normpath(os.path.join("/sbin", link))
        init = root / target.lstrip("/")
    return {"init_binary_exists": init.is_file() and init.resolve().is_relative_to(root.resolve()),
            "inittab_present": (root/"etc/inittab").is_file(),
            "runtime_binary_present": (root/"usr/sbin/pocketos-r1-runtime").is_file()}

def report(root, reference_rootfs, candidate_rootfs, kernel, integration, plan, partitions):
    identity = sha(kernel)
    if identity != integration["kernel"]["sha256"]:
        raise ValueError("Reference kernel digest mismatch")
    if integration["flashable"] or plan["flashable"] or plan["apply_enabled"]:
        raise ValueError("Unsafe upstream development state")
    init = inspect_init(root)
    gates = [
        {"name":"independent_sd_boot", "state":"unverified"},
        {"name":"stock_recovery_file_and_restore_test", "state":"unverified"},
        {"name":"actual_partition_mapping_and_usable_capacity", "state":"unverified"},
        {"name":"module_activation_and_runtime_services", "state":"unverified"},
        {"name":"physical_boot_and_repeated_recovery", "state":"unverified"},
    ]
    if not init["init_binary_exists"] or not init["inittab_present"]:
        gates.append({"name":"userspace_init_layout", "state":"missing"})
    if not init["runtime_binary_present"]:
        gates.append({"name":"target_runtime_binary", "state":"not_staged"})
    if integration.get("blocked") or plan.get("blockers") or plan.get("deferred"):
        gates.append({"name":"driver_plan_review", "state":"unresolved",
                      "blockers":len(plan.get("blockers", [])), "deferred":len(plan.get("deferred", []))})
    candidate_bytes = candidate_rootfs.stat().st_size
    reference_bytes = reference_rootfs.stat().st_size
    return {
        "phase":"2E", "flashable":False, "hardware_verified":False,
        "boot_verified":False, "recovery_verified":False,
        "official_recovery_documented":True, "sd_boot_verified":False,
        "kernel_sha256":identity, "init_layout":init,
        "rootfs_size_comparison":{"candidate_squashfs_bytes":candidate_bytes,
                                  "reference_squashfs_bytes":reference_bytes,
                                  "difference_bytes":candidate_bytes-reference_bytes,
                                  "not_partition_capacity":True},
        "partition_evidence":partitions,
        "raw_size_checks":{"kernel":size_check(kernel.stat().st_size,partitions,"kernel"),
                           "rootfs":size_check(candidate_bytes,partitions,"rootfs")},
        "gates":gates, "installation_allowed":False,
        "notes":["The scratch squashfs is for sizing only; it is not a boot image or OTA.",
                 "No bootloader, partition, recovery or device writes.",
                 "Recovery mode installs firmware; it does not establish independent microSD boot.",
                 "No release or installation path exists in this tool."]
    }

def main():
    p = argparse.ArgumentParser()
    for key in ("root", "reference_rootfs", "candidate_rootfs", "kernel", "integration", "plan", "output"):
        p.add_argument(key, type=Path)
    p.add_argument("--mtd", type=Path, help="Optional isolated /proc/mtd capture; not proof of updater mapping")
    a = p.parse_args()
    partitions = parse_mtd(a.mtd.read_text()) if a.mtd else {}
    r = report(a.root.resolve(), a.reference_rootfs, a.candidate_rootfs, a.kernel,
               json.loads(a.integration.read_text()), json.loads(a.plan.read_text()), partitions)
    a.output.write_text(json.dumps(r, indent=2)+"\n")
    print("Phase 2E scratch rootfs:", r["rootfs_size_comparison"])
    print("Installation blocked; independent boot and recovery not verified.")

if __name__ == "__main__":
    main()
