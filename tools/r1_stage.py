#!/usr/bin/env python3
"""Offline-only R1 integration staging. Never execute or publish vendor code."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

REQUIRED = {
    "display": ("soc_fb.ko", "lcd_lg35583.ko", "pwm_backlight.ko"),
    "input": ("cst8xx_touch.ko", "keyboard_adc_multifunc.ko", "keyboard_gpio_add.ko"),
    "audio": ("soc_aic.ko", "codec_cs43131.ko", "x1600_hiby_r1_sound_card.ko"),
    "power": ("axp2101.ko", "cw2015.ko"),
    "storage": ("soc_msc.ko",),
    "radio": ("bcm_wlbt_power.ko", "brcmfmac.ko", "brcmutil.ko"),
}

def digest(path):
    with path.open("rb") as fd:
        return hashlib.file_digest(fd, "sha256").hexdigest()

def copy_checked(source, destination, boundary, expected=None):
    if source.is_symlink() or not source.is_file() or not source.resolve().is_relative_to(boundary.resolve()):
        raise ValueError("Invalid reference file: " + str(source))
    actual = digest(source)
    if expected is not None and actual != expected:
        raise ValueError("Reference digest mismatch: " + str(source))
    if destination.exists() or destination.is_symlink():
        raise ValueError("Staging destination already exists: " + str(destination))
    shutil.copyfile(source, destination)
    if digest(destination) != actual:
        raise ValueError("Staging copy mismatch: " + str(destination))
    return {"sha256": actual, "bytes": destination.stat().st_size}

def literal_order(path):
    return [m.group(1) for line in path.read_text(errors="replace").splitlines()
            if (m := re.fullmatch(r"\s*sh\s+([A-Za-z0-9_.-]+\.sh)\s*(?:#.*)?", line))]

def prepare_new_directory(path, boundary):
    if path.exists() or path.is_symlink() or not path.parent.resolve().is_relative_to(boundary.resolve()):
        raise ValueError("Unsafe staging directory: " + str(path))
    path.mkdir()
    return path

def main():
    p = argparse.ArgumentParser()
    for name in ("reference", "audit", "foundation", "staging", "probe", "output"):
        p.add_argument(name, type=Path)
    a = p.parse_args()
    audit = json.loads(a.audit.read_text())
    foundation = json.loads(a.foundation.read_text())
    if audit["flashable"] or audit["hardware_verified"] or not foundation["fp_abi_matches"]:
        raise ValueError("Unexpected compatibility status")
    root, staging = a.reference.resolve(), a.staging.resolve()
    target = staging / "rootfs"
    if not target.is_dir() or target.is_symlink():
        raise ValueError("Extract a verified foundation rootfs into a fresh staging directory first")
    drivers = root / "module_driver"
    if not drivers.is_dir() or drivers.is_symlink():
        raise ValueError("Reference module_driver directory unavailable")
    module_target = prepare_new_directory(target / "module_driver", target)
    # Preserve scripts as inert evidence only, outside the root filesystem.
    evidence = prepare_new_directory(staging / "reference-startup-evidence", staging)
    expected = {item["path"]: item for items in audit["groups"].values() for item in items
                if item["path"].endswith(".ko")}
    kernel = copy_checked(root.parent / "reference-kernel.bin", staging / "reference-kernel.bin",
                          root.parent, audit["reference_kernel"]["sha256"])
    modules, scripts = {}, {}
    for source in sorted(drivers.iterdir()):
        if source.suffix == ".ko":
            key = "module_driver/" + source.name
            record = expected.get(key, {})
            if not record.get("sha256"):
                raise ValueError("Module missing verified audit digest: " + key)
            modules[source.name] = copy_checked(source, module_target / source.name, root, record["sha256"])
            modules[source.name]["vermagic"] = record.get("vermagic")
            modules[source.name]["depends"] = record.get("depends")
        elif source.suffix == ".sh":
            scripts[source.name] = copy_checked(source, evidence / source.name, root)
    entrypoint = evidence / "driver_default_init_script.sh"
    order = literal_order(entrypoint) if entrypoint.is_file() else []
    groups = {name: {"staged": [m for m in required if m in modules],
                     "missing": [m for m in required if m not in modules]}
              for name, required in REQUIRED.items()}
    missing = [m for group in groups.values() for m in group["missing"]]
    missing_scripts = sorted(set(order) - scripts.keys())
    vermagic = sorted({m["vermagic"] for m in modules.values() if m["vermagic"]})
    unknown_vermagic = [name for name, m in modules.items() if not m["vermagic"]]
    # A diagnostic entry point, NOT an init service. No module loading or sysfs writes.
    usr_sbin = target / "usr" / "sbin"
    if not usr_sbin.is_dir() or not usr_sbin.resolve().is_relative_to(target):
        raise ValueError("Missing safe foundation /usr/sbin")
    probe_target = usr_sbin / "pocketos-r1-probe"
    copy_checked(a.probe, probe_target, a.probe.parent)
    probe_target.chmod(0o755)
    report = {
        "phase": "2B", "flashable": False, "hardware_verified": False,
        "kernel": kernel, "foundation_rootfs": foundation["candidate_tar_gz"],
        "modules": modules, "subsystems": groups,
        "startup_evidence": scripts, "literal_reference_order": order,
        "missing_literal_scripts": missing_scripts, "module_vermagic_values": vermagic,
        "unknown_module_vermagic": unknown_vermagic,
        "unresolved_dependency_evidence": [d for d in audit.get("module_dependency_evidence", [])
                                           if d["status"] != "reference_module_found"],
        "physical_test_required": True, "automatic_module_loading": False,
        "userspace_radio_firmware_and_services_integrated": False,
        "blocked": bool(missing or missing_scripts or not order or unknown_vermagic or len(vermagic) != 1),
        "notes": ["Kernel and modules staged only on the CI runner; no vendor bytes uploaded.",
                  "No userdata mounts, flash, OTA, bootloader changes, or executed vendor scripts.",
                  "Literal startup order is evidence, not a validated boot sequence.",
                  "Driver presence does not demonstrate hardware operation."],
    }
    a.output.write_text(json.dumps(report, indent=2) + "\n")
    print("Offline staged", len(modules), "modules;", len(order), "startup hints")
    print("Missing key modules:", missing, "; missing startup scripts:", missing_scripts)
    if missing or not modules:
        raise SystemExit("Required R1 hardware modules missing; do not proceed")

if __name__ == "__main__":
    main()
