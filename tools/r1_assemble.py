#!/usr/bin/env python3
"""Assemble a PocketOS development rootfs offline. No firmware writer."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

CORE = {
    "pocketos-info": "usr/bin/pocketos-info",
    "pocketos-shell": "usr/bin/pocketos-shell",
    "pocketos-music": "usr/bin/pocketos-music",
    "pocketos-wifi": "usr/bin/pocketos-wifi",
    "pocketos-bluetooth": "usr/bin/pocketos-bluetooth",
    "pocketos-update": "usr/bin/pocketos-update",
    "pocketos-decoder": "usr/bin/pocketos-decoder",
    "pocketos-start": "usr/bin/pocketos-start",
    "pocketos-r1-runtime": "usr/sbin/pocketos-r1-runtime",
    "S95pocketos": "etc/init.d/S95pocketos",
    "reader-font-LICENSE.txt": "usr/share/pocketos/reader-font-LICENSE.txt",
}
ELFS = {"pocketos-info","pocketos-shell","pocketos-music","pocketos-wifi","pocketos-r1-runtime"}
CORE_CAP = 4 * 1024 * 1024
ROOTFS_CAP = 48 * 1024 * 1024  # development engineering cap only, not partition capacity

def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f,"sha256").hexdigest()

def regular(path):
    return path.is_file() and not path.is_symlink()

def validate_root(root):
    if not root.is_dir() or root.is_symlink() or root.resolve() == Path("/"):
        raise ValueError("Requires a disposable development rootfs, never /")
    if not (root/"etc").is_dir() or (root/"etc").is_symlink():
        raise ValueError("Unsafe rootfs etc")
    path=root/"etc/pocketos-release"
    if not regular(path):
        raise ValueError("Missing development rootfs identity")
    required={"NAME=PocketOS","TARGET=hiby-r1","STATE=development-userspace-only","FLASHABLE=no"}
    if not required.issubset(set(path.read_text().splitlines())):
        raise ValueError("Wrong rootfs identity")
    for name in ("etc/pocketos/autostart","etc/pocketos/update-public.pem","etc/pocketos/decoder.sha256"):
        p=root/name
        if p.exists() or p.is_symlink():
            raise ValueError("Unexpected activation or trust marker: "+name)

def safe_parent(root,dest):
    current=root
    for part in dest.parts[:-1]:
        current=current/part
        if current.is_symlink():
            raise ValueError("Refuse symlink parent: "+str(current))
        current.mkdir(exist_ok=True)
        if not current.is_dir():
            raise ValueError("Invalid parent: "+str(current))

def app_abi(path):
    report=subprocess.check_output(["readelf","-h","-A",str(path)],text=True)
    if not all(key in report for key in ("ELF32","little endian","MIPS")):
        raise ValueError("Not MIPS32 little endian: "+str(path))
    return next((line.split("FP ABI:",1)[1].strip() for line in report.splitlines() if "FP ABI:" in line),None)

def stage(root,apps,expected_fp):
    validate_root(root)
    if not expected_fp:
        raise ValueError("Missing reference float ABI")
    records={}
    for name,location in CORE.items():
        source=apps/name
        if not regular(source):
            raise ValueError("Missing core input: "+name)
        if name in ELFS and app_abi(source)!=expected_fp:
            raise ValueError("Float ABI mismatch: "+name)
        records[name]={"bytes":source.stat().st_size,"sha256":digest(source),"path":location}
    size=sum(r["bytes"] for r in records.values())
    if size>CORE_CAP:
        raise ValueError("Core application size exceeds 4 MiB engineering budget")
    for name,location in CORE.items():
        dest=Path(location)
        safe_parent(root,dest)
        target=root/dest
        if target.exists() or target.is_symlink():
            if name!="S95pocketos" or not regular(target) or digest(target)!=records[name]["sha256"]:
                raise ValueError("Unexpected existing destination: "+location)
        else:
            shutil.copyfile(apps/name,target)
        target.chmod(0o644 if name.endswith(".txt") else 0o755)
    # Preserve service scripts as inert evidence. Do not auto-enable vendor or radio init.
    disabled=[]
    init=root/"etc/init.d"
    for path in sorted(init.glob("S*")):
        if any(token in path.name.lower() for token in ("bluetooth","bluez","dbus","wpa","network")):
            if not regular(path):
                raise ValueError("Unsafe service entry: "+str(path))
            target=path.with_name("disabled-"+path.name)
            if target.exists() or target.is_symlink():
                raise ValueError("Duplicate service entry")
            path.rename(target)
            disabled.append(path.name)
    # Only these tools are staged; target daemon/driver availability is not claimed.
    locations={
        "aplay":["usr/bin/aplay","bin/aplay"],
        "curl":["usr/bin/curl","bin/curl"],
        "openssl":["usr/bin/openssl","bin/openssl"],
        "wpa_supplicant":["usr/sbin/wpa_supplicant","usr/bin/wpa_supplicant","sbin/wpa_supplicant"],
        "bluetoothctl":["usr/bin/bluetoothctl","bin/bluetoothctl"],
        "ca_bundle":["etc/ssl/certs/ca-certificates.crt"],
    }
    dependencies={name:next((loc for loc in choices if regular(root/loc)),None) for name,choices in locations.items()}
    return {"core":records,"core_bytes":size,"core_cap_bytes":CORE_CAP,
            "dependencies":dependencies,"disabled_service_scripts":disabled,
            "flashable":False,"installation_allowed":False,"hardware_verified":False,
            "autostart_enabled":False,"update_activation_enabled":False}

def report(rootfs,reference,assembly,boot):
    if assembly.get("flashable") is not False or boot.get("installation_allowed") is not False:
        raise ValueError("Invalid upstream gates")
    candidate=rootfs.stat().st_size
    ref=reference.stat().st_size
    return {"phase":"4C","flashable":False,"installation_allowed":False,
            "hardware_verified":False,"boot_verified":False,"recovery_verified":False,
            "engineering_rootfs_cap_bytes":ROOTFS_CAP,"candidate_squashfs_bytes":candidate,
            "reference_squashfs_bytes":ref,"candidate_within_engineering_cap":candidate<=ROOTFS_CAP,
            "candidate_not_larger_than_reference":candidate<=ref,
            "physical_partition_capacity_bytes":None,"assembly":assembly,
            "boot_gates":boot["gates"],"sd_optional":["PocketOS-codecs/pocketos-ffmpeg","Books","Music","PocketOS-inbox","PocketOS-updates"],
            "notes":["Scratch sizing only. No UPT, kernel, vendor modules or flash image published.",
                     "Reference size and 48 MiB engineering cap are not usable partition capacity.",
                     "Audio, radio and update target dependencies require physical integration and trusted signing."]}

def main():
    p=argparse.ArgumentParser()
    subs=p.add_subparsers(dest="mode",required=True)
    a=subs.add_parser("stage")
    for name in ("root","apps","foundation","output"):a.add_argument(name,type=Path)
    b=subs.add_parser("report")
    for name in ("candidate","reference","assembly","boot","output"):b.add_argument(name,type=Path)
    args=p.parse_args()
    if args.mode=="stage":
        foundation=json.loads(args.foundation.read_text())
        if foundation.get("flashable") is not False or not foundation.get("fp_abi_matches"):
            raise SystemExit("Unverified foundation ABI")
        result=stage(args.root,args.apps,foundation["reference_player_abi"]["FP ABI"])
    else:
        result=report(args.candidate,args.reference,json.loads(args.assembly.read_text()),json.loads(args.boot.read_text()))
    args.output.write_text(json.dumps(result,indent=2)+"\n")
    if args.mode=="stage" and not all(result["dependencies"].values()):
        raise SystemExit("Target runtime dependencies missing; inspect assembly report")
    if args.mode=="report" and not result["candidate_within_engineering_cap"]:
        raise SystemExit("Scratch image exceeds engineering budget; optimize before proceeding")
if __name__=="__main__":main()
