#!/usr/bin/env python3
"""Sign R1 PocketOS system + byte-identical proven kernel; never install/publish."""
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
    p.add_argument("--kernel", type=Path, required=True, help="Exact proven running kernel; no replacement kernel accepted")
    p.add_argument("--key", type=Path, required=True)
    p.add_argument("--sequence", type=int, required=True)
    p.add_argument("--channel", choices=("stable", "test"), default="test")
    p.add_argument("--reader-only", action="store_true",
                   help="Require real KOReader as only UI; reject legacy PocketOS shell/music/games")
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
    # Check immutable installed identity and trust in the actual compressed image.
    def read_image_file(name):
        return subprocess.check_output(["unsquashfs", "-cat", str(a.rootfs), name])
    release = read_image_file("etc/pocketos-release").decode("utf-8").splitlines()
    if [line for line in release if line.startswith("UPDATE_SEQUENCE=")] != [f"UPDATE_SEQUENCE={a.sequence}"]:
        p.error("Rootfs installed sequence must equal signed release sequence")
    if read_image_file("etc/pocketos/update-enable") != b"POCKETOS-R1-UPDATES-1\n":
        p.error("Rootfs is not independently approved for signed updates")
    embedded = subprocess.check_output(["openssl", "pkey", "-pubin", "-pubout", "-outform", "DER"],
                                       input=read_image_file("etc/pocketos/update-public.pem"))
    signer = subprocess.check_output(["openssl", "pkey", "-in", str(a.key.resolve()), "-pubout", "-outform", "DER"])
    if embedded != signer:
        p.error("Signing key does not match embedded update trust")
    # This channel is exclusively the independent PocketOS userspace. A signed
    # hybrid/reference image must never silently bring back the Sonix launcher.
    listing = subprocess.check_output(["unsquashfs", "-ls", str(a.rootfs)], text=True)
    paths = {line.removeprefix("squashfs-root/") for line in listing.splitlines()
             if line.startswith("squashfs-root/")}
    forbidden = {"sonix_player", "sonix_launch", "sys_server", "recoveryd"}
    if any(Path(name).name in forbidden for name in paths):
        p.error("Sonix userspace or recovery daemon present in primary rootfs")
    if a.reader_only:
        required = ("usr/bin/pocketos-start", "usr/bin/pocketos-update",
                    "usr/bin/pocketos-r1-touch-probe", "usr/sbin/pocketos-recovery-arm",
                    "etc/init.d/S95pocketos", "opt/koreader/luajit",
                    "opt/koreader/reader.lua", "opt/koreader/frontend/device.lua",
                    "opt/koreader/frontend/device/hibyr1/device.lua",
                    "etc/pocketos/reader-edition-source")
        prohibited = ("usr/bin/pocketos-shell", "usr/bin/pocketos-music",
                      "usr/bin/pocketos-game", "usr/bin/pocketos-decoder",
                      "etc/pocketos/games-enable", "etc/pocketos/doom.sha256",
                      "etc/pocketos/classic-ui-enable")
        if any(name in paths for name in prohibited):
            p.error("Reader-only image contains a legacy PocketOS, music, or game GUI")
        startup = read_image_file("usr/bin/pocketos-start").decode()
        if ("exec ./luajit ./reader.lua" not in startup or
            "export KO_PLATFORM=hibyr1" not in startup or
            "export KO_HOME=" not in startup):
            p.error("Reader-only launcher does not start full KOReader on R1")
        if not any("0.1.0-reader." in line for line in release):
            p.error("Reader-only rootfs identity missing")
        plugins = {Path(name).parts[3] for name in paths
                   if name.startswith("opt/koreader/plugins/") and
                   len(Path(name).parts) >= 5 and
                   Path(name).parts[3].endswith(".koplugin")}
        if len(plugins) < 20:
            p.error("Reader-only firmware must retain the complete KOReader plugins")
    else:
        required = ("usr/bin/pocketos-shell", "usr/bin/pocketos-start",
                    "usr/bin/pocketos-update", "usr/sbin/pocketos-recovery-arm",
                    "etc/init.d/S95pocketos")
    for name in required:
        if name not in paths:
            p.error("Missing independent PocketOS service: " + name)
    if not {"NAME=PocketOS", "TARGET=hiby-r1"}.issubset(release):
        p.error("Wrong PocketOS identity")
    if read_image_file("etc/pocketos/recovery-enable") != b"POCKETOS-R1-RECOVERY-1\n":
        p.error("Reviewed recovery handoff approval missing")
    candidate = json.loads(a.candidate.read_text())
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".pocketos-release-", dir=a.output.parent) as temp:
        work = Path(temp)
        upt = work / "pocketos-r1.upt"
        report = module.package(a.rootfs, candidate, work / "iso", upt, a.kernel)
        if not all(report[name] is False for name in
                   ("bootloader_payload", "recovery_payload", "userdata_payload")):
            raise ValueError("Unexpected protected partition payload")
        if not (report["kernel_payload"] and report["kernel_unchanged"] and
                report["stock_recovery_monitor_compatible"] and
                report["kernel_target_when_in_software_recovery"] == "/dev/mtd1" and
                report["kernel_sha256"] == module.KERNEL_SHA):
            raise ValueError("Unverified kernel or legacy recovery monitor plan")
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
