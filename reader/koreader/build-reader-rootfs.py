#!/usr/bin/env python3
"""Assemble an offline KOReader-only HiBy R1 rootfs, never flash or publish.

Baseline must be the pinned, independent PocketOS system-update-2 filesystem.
KOReader runtime must be built for little-endian MIPS32, with full plugins.
This builder deliberately does not sign or create a flashable image.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

BASELINE_SHA256 = "fd2a763277941163a8b52a767b3fdcfe96b3530107a6f72a30fe0ccce365da8a"
BASELINE_BUILD_ID = "3b57c9b46524dc6d"
MAX_ROOTFS_BYTES = 16 * 1024 * 1024
SD_IMAGE_PATH = "/mnt/sd_0/PocketOS/KOReaderRuntime/KOReader-R1-SD-RUNTIME.squashfs"
MOUNT_PATH = "/run/pocketos-reader"
DISALLOWED_FRONTENDS = (
    "usr/bin/pocketos-shell",
    "usr/bin/pocketos-music",
    "usr/bin/pocketos-game",
    "usr/bin/pocketos-decoder",
)
DISALLOWED_FEATURES = (
    "etc/pocketos/classic-ui-enable",
    "etc/pocketos/games-enable",
    "etc/pocketos/audio-enable",
    "etc/pocketos/doom.sha256",
    "etc/pocketos/decoder.sha256",
)


def sha(path):
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def checked_file(path, executable=False):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Missing regular file: {path}")
    if executable and not path.stat().st_mode & stat.S_IXUSR:
        raise ValueError(f"Not executable: {path}")


def check_mips(path):
    checked_file(path)
    info = subprocess.check_output(["readelf", "-h", str(path)], text=True)
    if not all(token in info for token in ("ELF32", "little endian", "MIPS")):
        raise ValueError(f"Not an R1-compatible MIPS32 ELF: {path}")


def make_sd_manifest(koreader):
    """Pin the entire external runtime with hashes stored inside trusted rootfs."""
    entries = []
    for item in sorted(koreader.rglob("*")):
        if item.is_symlink():
            raise ValueError(f"Unsafe runtime symlink: {item}")
        if item.is_dir():
            continue
        if not item.is_file():
            raise ValueError(f"Unexpected runtime inode: {item}")
        relative = item.relative_to(koreader).as_posix()
        if any(ch in relative for ch in ("\n", "\r", "\\")):
            raise ValueError(f"Unsafe manifest filename: {relative!r}")
        entries.append(f"{sha(item)}  {relative}")
    if len(entries) < 100 or not any(x.endswith("  luajit") for x in entries):
        raise ValueError("KOReader SD payload is incomplete")
    return "\n".join(entries) + "\n", len(entries)


def stage(baseline, koreader, probe, launcher, output, sequence, source):
    for name in (baseline, probe, launcher):
        checked_file(name)
    if not re.fullmatch(r"[a-f0-9]{40}", source):
        raise ValueError("Source commit must be full git SHA")
    if sequence <= 3:
        raise ValueError("Reader Edition sequence must advance the independent system")
    if output.exists() or output.is_symlink():
        raise ValueError("Refusing to overwrite an existing output directory")
    if sha(baseline) != BASELINE_SHA256:
        raise ValueError("Wrong independent PocketOS rootfs; refuse Sonix or hybrid images")
    if not koreader.is_dir() or koreader.is_symlink():
        raise ValueError("KOReader must be a reviewed, regular directory")
    for p in ("luajit", "reader.lua", "frontend/device.lua",
              "frontend/device/hibyr1/device.lua"):
        checked_file(koreader / p)
    check_mips(koreader / "luajit")
    check_mips(probe)
    plugins = sorted((koreader / "plugins").glob("*.koplugin"))
    if len(plugins) < 37 or any(not p.is_dir() or p.is_symlink() for p in plugins):
        raise ValueError("KOReader's upstream plugin tree must be complete")
    if "exec ./luajit ./reader.lua" not in launcher.read_text():
        raise ValueError("Launcher must start genuine KOReader")
    if "/usr/bin/pocketos-shell" in launcher.read_text():
        raise ValueError("Launcher cannot start old PocketOS UI")

    # The rootfs is reconstructed offline from known-good independent PocketOS.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="reader-r1-") as tmp:
        work = Path(tmp)
        root = work / "root"
        # SD is intentionally mounted noexec. Build a separate, immutable
        # read-only SquashFS to be hash-verified *before* a dedicated loop
        # mount. Keep the primary rootfs small and never relax SD noexec.
        sd_payload = work / "KOReader-R1-SD-RUNTIME.squashfs"
        subprocess.run(["mksquashfs", str(koreader), str(sd_payload),
                        "-noappend", "-all-root", "-comp", "lzo",
                        "-processors", "2", "-no-progress"], check=True)
        if not sd_payload.is_file() or sd_payload.stat().st_size == 0:
            raise ValueError("Missing SD SquashFS runtime image")
        sd_image_hash = sha(sd_payload)
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(root),
                        str(baseline)], check=True)
        release = root / "etc/pocketos-release"
        checked_file(release)
        lines = release.read_text().splitlines()
        expected = {"NAME=PocketOS", "TARGET=hiby-r1",
                    "UPDATE_SEQUENCE=2", f"BUILD_ID={BASELINE_BUILD_ID}"}
        if not expected.issubset(set(lines)):
            raise ValueError("Wrong rootfs identity")
        for p in ("etc/pocketos/update-enable", "etc/pocketos/update-public.pem",
                  "etc/pocketos/recovery-enable", "etc/init.d/S95pocketos",
                  "usr/bin/pocketos-update", "usr/sbin/pocketos-recovery-arm"):
            checked_file(root / p)
        if (root / "etc/pocketos/update-enable").read_bytes() != b"POCKETOS-R1-UPDATES-1\n":
            raise ValueError("Signed update recovery gate missing")
        if (root / "etc/pocketos/recovery-enable").read_bytes() != b"POCKETOS-R1-RECOVERY-1\n":
            raise ValueError("Recovery gate missing")
        # Never change the kernel, update manager, recovery arm, or flash partitions.
        protected = ("usr/bin/pocketos-update", "usr/sbin/pocketos-recovery-arm",
                     "etc/pocketos/update-public.pem", "etc/init.d/S95pocketos")
        protected_hashes = {name: sha(root / name) for name in protected}

        for rel in (*DISALLOWED_FRONTENDS, *DISALLOWED_FEATURES):
            dest = root / rel
            if dest.is_symlink():
                raise ValueError(f"Unsafe symlink at frontend path: {rel}")
            if dest.is_file():
                dest.unlink()
        # The full native KOReader tree is ~200 MB uncompressed and must never
        # be embedded in the R1's 16 MiB internal rootfs partition. Stage it
        # as a separate SD payload and pin EVERY file hash in the protected
        # rootfs. Missing/modified SD files cause the only reader UI to fail
        # closed; the signed updater and recovery stay on internal flash.
        system_ko = root / "opt/koreader"
        if system_ko.exists() or system_ko.is_symlink():
            raise ValueError("Unexpected existing KOReader runtime")
        manifest, sd_file_count = make_sd_manifest(koreader)
        # One trusted image checksum authenticates *all* inner KOReader files.
        # The complete per-file listing remains in the immutable rootfs for
        # audit, but need not be hashed one-by-one on every cold boot.
        manifest_path = root / "etc/pocketos/koreader-sd.sha256"
        file_manifest_path = root / "etc/pocketos/koreader-runtime-files.sha256"
        if (manifest_path.exists() or manifest_path.is_symlink() or
                file_manifest_path.exists() or file_manifest_path.is_symlink()):
            raise ValueError("Unexpected existing KOReader SD manifest")
        manifest_path.write_text(sd_image_hash + "  " + SD_IMAGE_PATH + "\n")
        file_manifest_path.write_text(manifest)
        manifest_path.chmod(0o444)
        file_manifest_path.chmod(0o444)
        startup = root / "usr/bin/pocketos-start"
        checked_file(startup)
        shutil.copy2(launcher, startup)
        startup.chmod(0o755)
        probe_dest = root / "usr/bin/pocketos-r1-touch-probe"
        if probe_dest.exists() or probe_dest.is_symlink():
            raise ValueError("Touch detector destination already exists")
        shutil.copy2(probe, probe_dest)
        probe_dest.chmod(0o755)

        replacements = {"VERSION": f"0.1.0-reader.{sequence}",
                        "UPDATE_SEQUENCE": str(sequence),
                        "BUILD_ID": hashlib.sha256((source + sha(koreader / "luajit") +
                                                    sha(probe)).encode()).hexdigest()[:16]}
        release.write_text("\n".join(
            key + "=" + replacements[key] if (key := line.split("=", 1)[0])
            in replacements else line for line in lines
        ) + "\n")
        src = root / "etc/pocketos/reader-edition-source"
        src.write_text(source + "\n")
        src.chmod(0o444)
        if any((root / name).exists() or (root / name).is_symlink()
               for name in DISALLOWED_FRONTENDS + DISALLOWED_FEATURES):
            raise ValueError("Legacy music, game, or PocketOS GUI survived")
        for name, expected_hash in protected_hashes.items():
            if sha(root / name) != expected_hash:
                raise ValueError("Independent updater or recovery component modified")
        check_mips(root / "usr/bin/pocketos-r1-touch-probe")
        if system_ko.exists() or system_ko.is_symlink():
            raise ValueError("KOReader binary must not be inside internal rootfs")
        if manifest_path.read_text() != sd_image_hash + "  " + SD_IMAGE_PATH + "\n":
            raise ValueError("Pinned SD image manifest changed during staging")
        if file_manifest_path.read_text() != manifest:
            raise ValueError("KOReader source file manifest changed during staging")
        image = work / "reader-rootfs.squashfs"
        subprocess.run(["mksquashfs", str(root), str(image), "-noappend",
                        "-all-root", "-comp", "lzo", "-processors", "2",
                        "-no-progress"], check=True)
        size = image.stat().st_size
        if not 0 < size <= MAX_ROOTFS_BYTES:
            raise ValueError(f"Reader rootfs {size} exceeds {MAX_ROOTFS_BYTES} internal budget; "
                             "evaluate an SD split rather than writing beyond flash capacity")
        report = {
            "reader_edition": True, "only_visible_ui": "KOReader",
            "sd_runtime_required": True,
            "sd_runtime_path": SD_IMAGE_PATH,
            "sd_runtime_mount_path": MOUNT_PATH,
            "sd_runtime_format": "read-only-squashfs-lzo",
            "sd_mount_noexec_preserved": True,
            "requires_kernel_loop_support": True,
            "sd_manifest_rootfs_path": "/etc/pocketos/koreader-sd.sha256",
            "sd_manifest_file_count": 1,
            "sd_runtime_file_count": sd_file_count,
            "sd_file_manifest_rootfs_path": "/etc/pocketos/koreader-runtime-files.sha256",
            "sd_manifest_sha256": sha(manifest_path),
            "sd_payload_bytes": sd_payload.stat().st_size,
            "sd_payload_sha256": sha(sd_payload),
            "source_commit": source, "sequence": sequence,
            "installed_koreader_plugin_count": len(plugins),
            "rootfs_bytes": size, "rootfs_sha256": sha(image),
            "baseline_sha256": BASELINE_SHA256,
            "removed_other_user_interfaces": True,
            "update_and_recovery_hashes_unchanged": protected_hashes,
            "bootloader_payload": False, "recovery_payload": False,
            "userdata_payload": False, "kernel_modified": False,
            "device_installation_performed": False,
            "physical_boot_verified": False, "signed": False,
            "safe_to_flash": False,
        }
        output.mkdir(parents=True)
        shutil.copy2(image, output / image.name)
        shutil.copy2(sd_payload, output / sd_payload.name)
        (output / "reader-edition-report.json").write_text(json.dumps(report, indent=2) + "\n")
        (output / "NOT-FIRMWARE.txt").write_text(
            "Unsigned offline KOReader-only rootfs with separate SD runtime. "
            "Copy KOReader-R1-SD-RUNTIME.squashfs unmodified to "
            "/mnt/sd_0/PocketOS/KOReaderRuntime/ on a FAT32 card. "
            "A hash-verified RO loop mount requires physical R1 kernel support. "
            "DO NOT FLASH. Runtime ABI, input, recovery and signing need "
            "physical verification.\n")
        print(json.dumps(report, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "koreader", "probe", "launcher", "output"):
        p.add_argument("--" + name, required=True, type=Path)
    p.add_argument("--sequence", required=True, type=int)
    p.add_argument("--source-commit", required=True)
    a = p.parse_args()
    stage(a.baseline, a.koreader, a.probe, a.launcher, a.output,
          a.sequence, a.source_commit)


if __name__ == "__main__":
    main()
