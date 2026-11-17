#!/usr/bin/env python3
"""Build and sign R1 primary-rootfs-only UPT assets; never install or publish."""
import argparse
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--rootfs", type=Path, required=True)
    p.add_argument("--candidate", type=Path, required=True)
    p.add_argument("--key", type=Path, required=True)
    p.add_argument("--sequence", type=int, required=True)
    p.add_argument("--channel", choices=("stable", "test"), default="test")
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    if not 1 <= a.sequence <= 999999999:
        p.error("Invalid release sequence")
    if not a.key.is_file() or a.key.is_symlink() or a.key.resolve().is_relative_to(REPO):
        p.error("Use a private signing key outside the repository")
    if a.output.exists() or a.output.is_symlink():
        p.error("Use a new output directory")
    # Rebuild from the reviewed rootfs and candidate report, never sign arbitrary UPT bytes.
    spec = importlib.util.spec_from_file_location("build_r1_upt", REPO / "tools/build-r1-upt.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    candidate = json.loads(a.candidate.read_text())
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".pocketos-release-", dir=a.output.parent) as temp:
        work = Path(temp)
        upt = work / "pocketos-r1.upt"
        report = module.package(a.rootfs, candidate, work / "iso", upt)
        if not all(report[name] is False for name in
                   ("kernel_payload", "bootloader_payload", "recovery_payload", "userdata_payload")):
            raise ValueError("Unexpected protected partition payload")
        if report["rootfs_target_when_in_software_recovery"] != "/dev/mtd2":
            raise ValueError("Unexpected recovery target")
        report_file = work / "data-r1-report.json"
        report_file.write_text(json.dumps(report, indent=2) + "\n")
        staged = work / "signed"
        subprocess.run([sys.executable, str(REPO / "apps/pack-update.py"),
                        "--firmware", "--sequence", str(a.sequence), "--channel", a.channel, "--key", str(a.key.resolve()),
                        "--output", str(staged), str(upt), str(report_file)], check=True)
        if a.output.exists() or a.output.is_symlink():
            raise ValueError("Output directory appeared")
        staged.rename(a.output)
    print("Signed R1 staging assets created; cold boot and installation are not certified.")

if __name__ == "__main__":
    main()
