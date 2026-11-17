#!/usr/bin/env python3
"""Stage PocketOS apps onto verified R1 reference rootfs without replacing boot.

Purpose: offline compatibility/boot-preservation study; NOT FLASHABLE.
This does not replace Sonix's GUI, kernel, init, drivers, or updater.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path

# Distinct files only. Never replace a working vendor binary or script.
APPLICATIONS = {
    "pocketos-info": "usr/bin/pocketos-info",
    "pocketos-shell": "usr/bin/pocketos-shell",
    "pocketos-music": "usr/bin/pocketos-music",
    "pocketos-wifi": "usr/bin/pocketos-wifi",
    "pocketos-bluetooth": "usr/bin/pocketos-bluetooth",
    "pocketos-transfer": "usr/bin/pocketos-transfer",
    "pocketos-audio-monitor": "usr/bin/pocketos-audio-monitor",
    "pocketos-radio-observe": "usr/bin/pocketos-radio-observe",
    "pocketos-r1-runtime": "usr/sbin/pocketos-r1-runtime",
    "pocketos-decoder": "usr/bin/pocketos-decoder",
}
CRITICAL_DIRS = ("etc/init.d", "module_driver")
CRITICAL_FILES = ("etc/inittab", "sbin/init", "usr/bin/sonix_player", "usr/bin/sonix_player.sh")
MAX_TOTAL = 12 * 1024 * 1024  # Uncompressed CI staging guard, NOT a flash partition limit


def digest(path):
    with path.open("rb") as fd:
        return hashlib.file_digest(fd, "sha256").hexdigest()


def manifest(root):
    records = {}
    for relative in CRITICAL_FILES:
        p = root / relative
        if not p.exists() and not p.is_symlink():
            raise ValueError("Reference startup file missing: " + relative)
        records[relative] = ("link:" + os.readlink(p)) if p.is_symlink() else digest(p)
    for relative in CRITICAL_DIRS:
        directory = root / relative
        if not directory.is_dir() or directory.is_symlink():
            raise ValueError("Missing hardware startup directory: " + relative)
        for p in sorted(directory.rglob("*")):
            name = str(p.relative_to(root))
            if p.is_symlink():
                records[name] = "link:" + os.readlink(p)
            elif p.is_file():
                records[name] = digest(p)
            elif not p.is_dir():
                raise ValueError("Unsupported device type in boot critical path")
    return records


def stage(reference, apps, destination):
    for path in (reference, apps):
        if not path.is_dir() or path.is_symlink():
            raise ValueError("Invalid input directory")
    if destination.exists() or destination.is_symlink():
        raise ValueError("Refuse to overwrite an existing rootfs")
    if reference.resolve() == Path("/") or destination.resolve() == Path("/"):
        raise ValueError("Cannot stage host root")
    initial = manifest(reference)
    if len(initial) < 15:
        raise ValueError("Reference boot inventory unexpectedly small")
    total = 0
    for source, target in APPLICATIONS.items():
        src = apps / source
        if not src.is_file() or src.is_symlink():
            raise ValueError("Missing or unsafe app: " + source)
        total += src.stat().st_size
        dest = reference / target
        if dest.exists() or dest.is_symlink():
            raise ValueError("Would overwrite existing R1 reference file: " + target)
    if total > MAX_TOTAL:
        raise ValueError("Application memory/storage budget exceeded")
    shutil.copytree(reference, destination, symlinks=True, copy_function=shutil.copy2)
    written = {}
    for name, relative in APPLICATIONS.items():
        src, dest = apps / name, destination / relative
        if not dest.parent.is_dir() or dest.parent.is_symlink():
            raise ValueError("Unsafe application destination: " + relative)
        shutil.copy2(src, dest)
        dest.chmod(0o755)
        written[name] = {"path": relative, "bytes": dest.stat().st_size, "sha256": digest(dest)}
    after = manifest(destination)
    if initial != after:
        raise ValueError("CRITICAL: vendor startup, drivers or UI changed")
    # Preserve the verified stock boot path; switching to PocketOS UI is a
    # *separate* hardware-tested gate, not an implicit step in this function.
    return {
        "mode": "R1 reference-compatible application test",
        "vendor_boot_files_unchanged": True,
        "vendor_boot_inventory": len(initial),
        "vendor_boot_manifest_sha256": hashlib.sha256(
            json.dumps(initial,sort_keys=True).encode()).hexdigest(),
        "existing_launcher_preserved": True, "pocketos_launcher_autostart": False,
        "kernel_replaced": False, "driver_init_replaced": False,
        "core_added_bytes": total, "added_applications": written,
        "flashable": False, "installation_allowed": False,
        "runtime_abi_verified": False, "physical_boot_verified": False,
        "notes": [
            "The known R1 startup path remains identical.",
            "QEMU runtime testing is required before enabling the PocketOS UI.",
            "This rootfs is a disposable offline candidate, not validated firmware."
        ]
    }



def add_guard(root, original_manifest, guard_script, report):
    """Replace ONLY the vendor GUI wrapper, preserving an executable fallback."""
    if not guard_script.is_file() or guard_script.is_symlink():
        raise ValueError("Missing opt-in guard script")
    wrapper = root/"usr/bin/sonix_player.sh"
    fallback = root/"usr/bin/sonix_player.pocketos-fallback.sh"
    if not wrapper.is_file() or wrapper.is_symlink():
        raise ValueError("Vendor player wrapper is not a regular file")
    if fallback.exists() or fallback.is_symlink():
        raise ValueError("Fallback collision")
    if digest(wrapper) != original_manifest["usr/bin/sonix_player.sh"]:
        raise ValueError("Original wrapper changed before boot guard install")
    shutil.copy2(wrapper, fallback)
    shutil.copy2(guard_script, wrapper)
    wrapper.chmod(0o755)
    fallback.chmod(0o755)
    after=manifest(root)
    changed=[name for name in original_manifest if original_manifest[name]!=after.get(name)]
    if changed!=["usr/bin/sonix_player.sh"]:
        raise ValueError("Unexpected vendor startup modifications: "+str(changed))
    if digest(fallback) != original_manifest["usr/bin/sonix_player.sh"]:
        raise ValueError("Fallback copy differs from reference")
    report.update({
        "vendor_boot_files_unchanged": False,
        "vendor_hardware_initialization_unchanged": True,
        "vendor_player_binary_unchanged": True,
        "vendor_launcher_backed_up": True,
        "guarded_boot_opt_in": True,
        "pocketos_launcher_autostart": False,
        "sd_marker_required": "/mnt/sd_0/PocketOS-try-new-ui",
        "sd_marker_content": "POCKETOS-R1-TRY-1",
        "physical_boot_verified": False,
        "installation_allowed": False,
        "flashable": False,
        "guard_sha256": digest(guard_script),
        "loader_sha256": digest(wrapper),
        "expected_sd_guard_path": "/mnt/sd_0/PocketOS/boot-guard.sh",
        "fallback_sha256": digest(fallback)
    })
    return report



def stage_sd_guard(reference, destination, loader_script, guard_script, shell_binary, version):
    """Build a minimal test boot overlay that leaves all PocketOS executables on SD.

    The original vendor init, kernel/drivers, and Sonix player remain untouched.
    Only its launcher wrapper and system-info version are updated. The
    original launcher is copied verbatim as a fallback and the PocketOS binary
    digest is pinned in the read-only rootfs.
    """
    if destination.exists() or destination.is_symlink():
        raise ValueError("Refuse to overwrite staging directory")
    if not reference.is_dir() or reference.is_symlink():
        raise ValueError("Unsafe reference")
    if not loader_script.is_file() or loader_script.is_symlink():
        raise ValueError("Unsafe loader script")
    if not guard_script.is_file() or guard_script.is_symlink():
        raise ValueError("Unsafe guard script")
    if not shell_binary.is_file() or shell_binary.is_symlink():
        raise ValueError("Missing pinned executable")
    if shell_binary.stat().st_size > 4194304:
        raise ValueError("PocketOS shell exceeds SD boot guard limit")
    before = manifest(reference)
    shutil.copytree(reference, destination, symlinks=True, copy_function=shutil.copy2)
    wrapper = destination / "usr/bin/sonix_player.sh"
    fallback = destination / "usr/bin/sonix_player.pocketos-fallback.sh"
    if not wrapper.is_file() or wrapper.is_symlink() or fallback.exists() or fallback.is_symlink():
        raise ValueError("Missing original player wrapper or fallback collision")
    shutil.copy2(wrapper, fallback)
    template = loader_script.read_text(encoding="utf-8")
    if template.count("@SHELL_SHA256@") != 1 or template.count("@GUARD_SHA256@") != 1:
        raise ValueError("Boot loader hash placeholders are invalid")
    wrapper.write_text(template.replace("@SHELL_SHA256@", digest(shell_binary))
                       .replace("@GUARD_SHA256@", digest(guard_script)), encoding="utf-8")
    fallback.chmod(0o755)
    wrapper.chmod(0o755)
    if digest(fallback) != before["usr/bin/sonix_player.sh"]:
        raise ValueError("Stock player fallback differs")
    if digest(shell_binary) not in wrapper.read_text() or digest(guard_script) not in wrapper.read_text():
        raise ValueError("Boot loader did not pin both SD hashes")
    info_path = destination / "usr/resource/sonix/components/system-info.json"
    if not info_path.is_file() or info_path.is_symlink():
        raise ValueError("Missing stock system-info")
    info = json.loads(info_path.read_text())
    if info.get("device-name") != "HiBy R1":
        raise ValueError("Wrong device")
    old = info.get("OS_version", "")
    if not old or old != "0.3.0.99":
        raise ValueError("Reference version mismatch: " + old)
    if version != "0.3.0.100":
        raise ValueError("Only pinned R1 test version allowed")
    info["OS_version"] = version
    # Keep the original Sonix Books OTA channel for the next firmware release.
    if info.get("ota-repo") != "lilblock1/Sonix-Books":
        raise ValueError("Unexpected configured OTA repository")
    info_path.write_text(json.dumps(info, indent=4) + "\n")
    after = manifest(destination)
    differences = [p for p in before if before[p] != after.get(p)]
    if differences != ["usr/bin/sonix_player.sh"]:
        raise ValueError("Unexpected vendor boot modifications " + str(differences))
    for p in CRITICAL_DIRS:
        for name in before:
            if name.startswith(p + "/") and before[name] != after[name]:
                raise ValueError("Vendor hardware init changed")
    return {
        "mode": "opt-in R1 microSD PocketOS boot candidate",
        "device": "HiBy R1",
        "version": version,
        "original_version": old,
        "stock_player_preserved": True,
        "stock_hardware_init_preserved": True,
        "vendor_kernel_preserved": True,
        "fallback_sha256": digest(fallback),
        "guard_sha256": digest(wrapper),
        "expected_sd_path": "/mnt/sd_0/PocketOS/bin/pocketos-shell",
        "expected_sd_shell_sha256": digest(shell_binary),
        "requires_sd_marker": "/mnt/sd_0/PocketOS-try-new-ui",
        "default_boot": "original Sonix player (no marker)",
        "new_boot": "PocketOS screen (only with marker and matching SD binary)",
        "expected_modified_reference_files": [
            "usr/bin/sonix_player.sh",
            "usr/resource/sonix/components/system-info.json"
        ],
        "new_files": ["usr/bin/sonix_player.pocketos-fallback.sh"],
        "bootable_verified": False,
        "recovery_partition_write_targets_verified": False,
        "safe_to_install": False,
    }


def main():
    p = argparse.ArgumentParser()
    p.add_argument("reference", type=Path)
    p.add_argument("applications", type=Path)
    p.add_argument("destination", type=Path)
    p.add_argument("report", type=Path)
    p.add_argument("--boot-guard", type=Path, default=None)
    a=p.parse_args()
    result=stage(a.reference,a.applications,a.destination)
    if a.boot_guard is not None:
        original = manifest(a.reference)
        add_guard(a.destination, original, a.boot_guard, result)
    a.report.write_text(json.dumps(result,indent=2)+"\n")
    print("R1 reference startup preserved;",len(result["added_applications"]),"PocketOS apps staged.")


if __name__=="__main__":
    main()
