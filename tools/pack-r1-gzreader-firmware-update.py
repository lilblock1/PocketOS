#!/usr/bin/env python3
"""Sign R1 PocketOS system + byte-identical proven kernel; never install/publish."""
import argparse
import hashlib
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
                   help="Require native GZreader as the only UI, with verified read-only SD runtime")
    p.add_argument("--sd-runtime", type=Path,
                   help="Complete immutable GZreader-R1-SD-RUNTIME.squashfs; mandatory with --reader-only")
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
        # The native reader runtime exceeds the 16 MiB internal rootfs cap.
        # It is stored in one pinned read-only SquashFS on the SD card; the
        # trusted internal rootfs includes its digest and the only boot UI.
        required = ("usr/bin/pocketos-start", "usr/bin/pocketos-update",
                    "usr/bin/pocketos-r1-touch-probe", "usr/sbin/pocketos-recovery-arm",
                    "etc/init.d/S95pocketos", "etc/pocketos/gzreader-sd.sha256",
                    "etc/pocketos/gzreader-runtime-files.sha256",
                    "etc/pocketos/reader-edition-source")
        prohibited = ("usr/bin/pocketos-shell", "usr/bin/pocketos-music",
                      "usr/bin/pocketos-game", "usr/bin/pocketos-decoder",
                      "etc/pocketos/games-enable", "etc/pocketos/doom.sha256",
                      "etc/pocketos/classic-ui-enable", "opt/gzreader")
        if any(name in paths for name in prohibited):
            p.error("Reader-only image contains legacy UI or internal GZreader")
        if not a.sd_runtime or a.sd_runtime.is_symlink() or not a.sd_runtime.is_file():
            p.error("Reader signing requires a real --sd-runtime SquashFS file")
        if a.sd_runtime.name != "GZreader-R1-SD-RUNTIME.squashfs":
            p.error("Unexpected external GZreader runtime filename")
        startup = read_image_file("usr/bin/pocketos-start").decode()
        for token in ("exec ./luajit ./reader.lua", "export KO_PLATFORM=hibyr1",
                      "export KO_HOME=", 'sha256sum -c "$manifest"',
                      "mount -t squashfs -o ro,nosuid,nodev,loop,exec",
                      "mknod -m 0600 /dev/loop0 b 7 0"):
            if token not in startup:
                p.error("Reader-only launcher missing boot or verification step: " + token)
        if not any("0.1.0-gzreader." in line for line in release):
            p.error("Reader-only rootfs identity missing")
        reader = json.loads(a.candidate.read_text())
        if (reader.get("reader_edition") is not True or
                reader.get("source_application") != "GZreader" or
                reader.get("ui_shell") != "ZenOS" or
                reader.get("source_application_sha") != "936f66b753e2a65427595920c4e8f36c10ec29e5" or
                reader.get("only_visible_ui") != "GZreader" or
                reader.get("sd_mount_noexec_preserved") is not True or
                reader.get("requires_kernel_loop_support") is not True or
                reader.get("signed") is not False or
                reader.get("safe_to_flash") is not False or
                reader.get("sequence") != a.sequence or
                reader.get("bootloader_payload") is not False or
                reader.get("recovery_payload") is not False or
                reader.get("userdata_payload") is not False or
                reader.get("kernel_modified") is not False):
            p.error("Reader edition report is missing required safety metadata")
        image_sha = hashlib.sha256(a.rootfs.read_bytes()).hexdigest()
        runtime_sha = hashlib.sha256(a.sd_runtime.read_bytes()).hexdigest()
        if (reader.get("rootfs_sha256") != image_sha or
                reader.get("rootfs_bytes") != a.rootfs.stat().st_size or
                a.rootfs.stat().st_size > 16 * 1024 * 1024 or
                reader.get("sd_payload_sha256") != runtime_sha or
                reader.get("sd_payload_bytes") != a.sd_runtime.stat().st_size):
            p.error("Reader report does not match the actual built images")
        manifest = read_image_file("etc/pocketos/gzreader-sd.sha256")
        image_location = "/mnt/sd_0/PocketOS/GZreaderRuntime/GZreader-R1-SD-RUNTIME.squashfs"
        if (manifest != (runtime_sha + "  " + image_location + "\n").encode() or
                reader.get("sd_runtime_path") != image_location or
                reader.get("sd_runtime_mount_path") != "/run/pocketos-reader" or
                reader.get("sd_manifest_sha256") != hashlib.sha256(manifest).hexdigest()):
            p.error("Trusted internal SD image digest or mount mapping mismatch")
        sd_entries = subprocess.check_output(
            ["unsquashfs", "-ls", str(a.sd_runtime)], text=True).splitlines()
        sd_paths = {line.removeprefix("squashfs-root/") for line in sd_entries
                    if line.startswith("squashfs-root/")}
        for name in ("luajit", "reader.lua", "frontend/device.lua",
                     "frontend/device/hibyr1/device.lua"):
            if name not in sd_paths:
                p.error("Native SD GZreader is missing " + name)
        plugins = {Path(name).parts[1] for name in sd_paths
                   if name.startswith("plugins/") and
                   len(Path(name).parts) >= 2 and
                   Path(name).parts[1].endswith(".koplugin")}
        if (len(plugins) < 5 or
                not {"gzreader.koplugin", "zenos.koplugin"}.issubset(plugins) or
                len(plugins) != reader.get("installed_gzreader_plugin_count")):
            p.error("Full GZreader SD plugin tree is missing or differs from report")
        if (read_image_file("etc/pocketos/reader-edition-source").decode().strip()
                != reader.get("source_commit")):
            p.error("Reader commit identity differs")
        # Translate the already-verified reader report into the existing strict
        # R1 UPT packer schema. The proven kernel is revalidated separately.
        candidate = {"image": {"sha256": image_sha},
                     "primary_target": "/dev/mtd2 (rootfs)",
                     "kernel_reused": True,
                     "build_id": next((line.removeprefix("BUILD_ID=")
                                       for line in release if line.startswith("BUILD_ID=")), None)}
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
    if not a.reader_only:
        if a.sd_runtime is not None:
            p.error("--sd-runtime is only valid with --reader-only")
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
