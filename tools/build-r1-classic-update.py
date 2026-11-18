#!/usr/bin/env python3
"""Build an offline Classic UI system update from the verified update-2 image.

Uses compiled SDK applications; never contacts the R1, installs or publishes.
The retained private key must stay outside the repository and output directory.
"""
import argparse
import hashlib
import json
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BASELINE_SHA = "fd2a763277941163a8b52a767b3fdcfe96b3530107a6f72a30fe0ccce365da8a"
CHANGED = {"usr/bin/pocketos-shell", "usr/bin/pocketos-start",
           "etc/pocketos-release", "etc/pocketos/classic-ui-enable",
           "etc/pocketos/source-commit"}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inventory(root):
    result = {}
    for path in root.rglob("*"):
        mode = stat.S_IMODE(path.lstat().st_mode)
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            result[name] = ("link", path.readlink().as_posix(), mode)
        elif path.is_file():
            result[name] = ("file", digest(path), mode)
        elif not path.is_dir():
            raise ValueError("Unexpected special file in offline rootfs")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline", "apps", "kernel", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--key", type=Path, help="Original trusted signing key (required unless --unsigned)")
    parser.add_argument("--unsigned", action="store_true", help="Only assemble and test unsigned rootfs; never publish as firmware")
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--sequence", type=int, required=True)
    parser.add_argument("--doom", type=Path, help="Reviewed optional SD game executable; only its digest enters the system")
    args = parser.parse_args()
    if not 3 <= args.sequence <= 999999999 or not re.fullmatch("[a-f0-9]{40}", args.source_commit):
        parser.error("Invalid update sequence or source commit")
    if args.output.exists() or args.output.is_symlink():
        parser.error("Use a new output directory")
    if not args.baseline.is_file() or digest(args.baseline) != BASELINE_SHA:
        parser.error("Baseline must be the verified, installed PocketOS update-2 rootfs")
    if not args.unsigned and args.key is None:
        parser.error("Signing requires the retained trusted key")
    if args.unsigned and args.key is not None:
        parser.error("Do not supply a signing key to an unsigned build")
    if args.key is not None and (not args.key.is_file() or args.key.is_symlink() or
            args.key.resolve().is_relative_to(REPO) or
            args.key.resolve().is_relative_to(args.output.resolve())):
        parser.error("Keep the retained signing key outside repository and output")
    applications = {name: args.apps / name for name in ("pocketos-shell", "pocketos-start")}
    for name, path in applications.items():
        if not path.is_file() or path.is_symlink():
            parser.error("Missing regular compiled application: " + name)
    elf = subprocess.check_output(["readelf", "-h", str(applications["pocketos-shell"])], text=True)
    if not all(token in elf for token in ("ELF32", "little endian", "MIPS")):
        parser.error("Shell must use the working MIPS SDK ABI")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pocketos-classic-build-", dir="/var/tmp") as temp:
        work = Path(temp)
        root = work / "root"
        subprocess.run(["unsquashfs", "-no-progress", "-d", str(root), str(args.baseline.resolve())], check=True)
        before = inventory(root)
        release = root / "etc/pocketos-release"
        old = release.read_text().splitlines()
        if not {"NAME=PocketOS", "TARGET=hiby-r1", "UPDATE_SEQUENCE=2",
                "BUILD_ID=3b57c9b46524dc6d"}.issubset(old):
            raise ValueError("Unexpected baseline identity")
        app_hashes = {name: digest(path) for name, path in applications.items()}
        if args.doom is not None:
            if args.doom.is_symlink() or not args.doom.is_file():
                raise ValueError("Doom must be the reviewed regular SDK executable")
            app_hashes["doom_sd_executable"] = digest(args.doom)
            app_hashes["pocketos-game"] = digest(REPO / "apps/pocketos-game.sh")
        identity = json.dumps({"baseline": BASELINE_SHA, "apps": app_hashes,
                               "source": args.source_commit, "sequence": args.sequence}, sort_keys=True)
        build_id = hashlib.sha256(identity.encode()).hexdigest()[:16]
        replacements = {"VERSION": f"0.1.0-alpha.{args.sequence}", "BUILD_ID": build_id,
                        "UPDATE_SEQUENCE": str(args.sequence)}
        release.write_text("\n".join(key + "=" + replacements[key] if
                           (key := line.split("=", 1)[0]) in replacements else line for line in old) + "\n")
        for name, source in applications.items():
            destination = root / "usr/bin" / name
            if destination.is_symlink():
                raise ValueError("Unexpected application symlink")
            shutil.copyfile(source, destination)
            destination.chmod(0o755)
        marker = root / "etc/pocketos/classic-ui-enable"
        marker.write_text("POCKETOS-R1-CLASSIC-1\n")
        marker.chmod(0o400)
        provenance = root / "etc/pocketos/source-commit"
        provenance.write_text(args.source_commit + "\n")
        provenance.chmod(0o444)
        allowed = set(CHANGED)
        if args.doom is not None:
            if args.doom.is_symlink() or not args.doom.is_file():
                raise ValueError("Doom must be the reviewed regular SDK executable")
            game_elf = subprocess.check_output(["readelf", "-h", str(args.doom)], text=True)
            if not all(token in game_elf for token in ("ELF32", "little endian", "MIPS")):
                raise ValueError("Wrong Doom target ABI")
            helper = root / "usr/bin/pocketos-game"
            shutil.copyfile(REPO / "apps/pocketos-game.sh", helper)
            helper.chmod(0o755)
            trust = root / "etc/pocketos/doom.sha256"
            trust.write_text(digest(args.doom) + "\n")
            trust.chmod(0o400)
            games = root / "etc/pocketos/games-enable"
            games.write_text("POCKETOS-R1-GAMES-1\n")
            games.chmod(0o400)
            allowed.update({"usr/bin/pocketos-game", "etc/pocketos/doom.sha256", "etc/pocketos/games-enable"})
        after = inventory(root)
        changed = {name for name in before.keys() | after.keys() if before.get(name) != after.get(name)}
        if changed != allowed:
            raise ValueError("Unexpected rootfs changes: " + repr(sorted(changed)))
        image = work / "pocketos-rootfs.squashfs"
        subprocess.run(["mksquashfs", str(root), str(image), "-noappend", "-all-root",
                        "-comp", "lzo", "-processors", "2", "-no-progress"], check=True)
        if not 0 < image.stat().st_size <= 16 * 1024 * 1024:
            raise ValueError("Image exceeds the retained conservative rootfs budget")
        candidate = work / "candidate.json"
        candidate.write_text(json.dumps({"image": {"sha256": digest(image)},
                                        "primary_target": "/dev/mtd2 (rootfs)",
                                        "kernel_reused": True, "build_id": build_id}))
        signed = work / "signed"
        if not args.unsigned:
            subprocess.run([sys.executable, str(REPO / "tools/pack-r1-firmware-update.py"),
                            "--rootfs", str(image), "--candidate", str(candidate),
                            "--kernel", str(args.kernel.resolve()), "--key", str(args.key.resolve()),
                            "--sequence", str(args.sequence), "--channel", "test", "--output", str(signed)], check=True)
        args.output.mkdir()
        if not args.unsigned:
            shutil.copytree(signed, args.output / "signed")
        shutil.copyfile(image, args.output / image.name)
        shutil.copyfile(candidate, args.output / candidate.name)
        (args.output / "source-report.json").write_text(json.dumps({
            "source_commit": args.source_commit, "baseline_sha256": BASELINE_SHA,
            "build_id": build_id, "sequence": args.sequence,
            "changed_rootfs_files": sorted(changed), "application_sha256": app_hashes,
            "kernel_replaced": False, "bootloader_payload": False,
            "recovery_payload": False, "userdata_payload": False,
            "device_installation_performed": False}, indent=2) + "\n")
        print(("Unsigned" if args.unsigned else "Signed") + " offline Classic/Doom update prepared; no device write or publication.")


if __name__ == "__main__":
    main()
