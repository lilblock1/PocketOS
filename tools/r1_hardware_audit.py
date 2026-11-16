#!/usr/bin/env python3
"""Inventory the verified R1 rootfs; never run any target program or driver."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("root",type=Path)
p.add_argument("foundation",type=Path)
p.add_argument("output",type=Path)
a = p.parse_args()
data=json.loads(a.foundation.read_text())
assert data["flashable"] is False and data["hardware_verified"] is False
assert data["fp_abi_matches"], "Foundation FP ABI mismatch"
root=a.root.resolve()
entries=data["reference_rootfs"]["entries"]
elfs=data["reference_rootfs"]["elf"]
candidate_names={Path(n).name for n in data["candidate_rootfs"]["entries"] if n.startswith(("lib/","usr/lib/"))}
def classify(name):
    n=name.lower()
    if any(x in n for x in ("wifi","wlan","brcm","bcm","bluetooth","bluealsa","bt_init","wpa_","hci")):return "radio"
    if any(x in n for x in ("codec","sound","aic","alsa","earpods","audio","cs43131","dma")):return "audio"
    if any(x in n for x in ("touch","keyboard","cst8","input","gpio_add")):return "input"
    if any(x in n for x in ("lcd","backlight","soc_fb","jpeg_display","framebuffer")):return "display"
    if any(x in n for x in ("battery","axp","cw2015","batd","charger","pwm","leds","suspend","zram")):return "power"
    if any(x in n for x in ("ubifs","mount","msc","ntfs","mmc","userdata","storage")):return "storage"
    if any(x in n for x in ("adbd","usb","recovery","ota","update")):return "recovery_usb"
    return "platform"

def checked_file(name):
    path=root/name
    if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root):
        return None
    return path

def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()

def module_field(path,key):
    result=subprocess.run(["modinfo","-F",key,str(path)],capture_output=True,text=True)
    return result.stdout.strip() if result.returncode==0 else None

groups={}
for name in sorted(entries):
    if not (name.endswith(".ko") or name.startswith("etc/init.d/") or
            name.startswith("module_driver/") or name.startswith(("usr/bin/","usr/sbin/","sbin/"))):
        continue
    basename=Path(name).name
    group=classify(basename)
    if group=="platform" and not (name.endswith(".ko") or name.startswith(("etc/init.d/","module_driver/"))):continue
    item={"path":name}
    record=elfs.get(name)
    if record:
        item["needed"]=record["needed"]
        item["missing_from_foundation_by_name"]=[n for n in record["needed"] if n not in candidate_names]
    if name.endswith(".ko"):
        path=checked_file(name)
        if path:
            item["sha256"]=sha256(path)
            for field in ("name","vermagic","depends","alias","firmware","license"):
                item[field]=module_field(path,field)
    groups.setdefault(group,[]).append(item)
# Record concrete module and startup evidence, not an executable load plan.
# In particular, shell conditions, variables, and indirect scripts cannot be
# interpreted reliably by static inspection.
modules=[item for items in groups.values() for item in items if item["path"].endswith(".ko")]
def normal(name):
    return Path(name).name.removesuffix(".ko").replace("-","_")
indexed={}
for item in modules:
    indexed.setdefault(normal(item.get("name") or item["path"]),[]).append(item["path"])
dependency_evidence=[]
for item in modules:
    depends=item.get("depends")
    if depends is None:
        continue
    for dep in filter(None,(x.strip() for x in depends.split(","))):
        dependency_evidence.append({"module":item["path"],"dependency":dep,
            "reference_paths":indexed.get(normal(dep),[]),
            "status":"reference_module_found" if normal(dep) in indexed else "not_found_as_separate_module"})
boot_hints=[]
# Only report literal module names and line numbers; never export vendor script
# text, arguments, credentials, or automatically execute these commands.
pattern=re.compile(r"(?:^|[;|&]\s*|\bthen\s+|\bdo\s+)\s*(?:/sbin/|/bin/)?(insmod|modprobe)\s+([A-Za-z0-9_./+-]+)(?=\s|;|$)")
for name in sorted(entries):
    if not (name.startswith(("etc/init.d/","module_driver/")) or name in ("etc/rc.local","etc/inittab")):
        continue
    path=checked_file(name)
    if not path or path.stat().st_size>256*1024:
        continue
    raw=path.read_bytes()
    if b"\x00" in raw:
        continue
    for line_num,line in enumerate(raw.decode("utf-8",errors="replace").splitlines(),1):
        if line.lstrip().startswith("#"):
            continue
        for match in pattern.finditer(line):
            command,token=match.groups()
            if command=="insmod" and not token.endswith(".ko"):
                continue
            boot_hints.append({"source":name,"line":line_num,"command":command,
                "module":Path(token).name,"reference_paths":indexed.get(normal(token),[])})
kernel=root.parent/"reference-kernel.bin"
assert kernel.is_file() and not kernel.is_symlink(), "Missing extracted, verified reference kernel"
kernel_identity={"sha256":sha256(kernel),"bytes":kernel.stat().st_size}
player=elfs.get("usr/bin/sonix_player")
assert player and groups, "No expected hardware/reference player inventory"
report={"phase":2,"flashable":False,"hardware_verified":False,
        "foundation_fp_matches":True,
        "groups":groups,
        "reference_kernel":kernel_identity,
        "module_dependency_evidence":dependency_evidence,
        "literal_startup_module_hints":boot_hints,
        "startup_hints_are_not_load_order":True,
        "unresolved_dependencies_may_be_builtin":True,
        "reference_player_needed":player["needed"],
        "player_missing_from_foundation_by_name":[n for n in player["needed"] if n not in candidate_names],
        "requirements":["same kernel and module compatibility","device-specific load order",
                        "exact dependency and symbol resolution","physical hardware validation"],
        "caution":"Filename-based library checks are only hints, never ABI or hardware compatibility proof."}
a.output.write_text(json.dumps(report,indent=2)+"\n")
for group,items in sorted(groups.items()):
    print(group, len(items), "reference components")
print("Player libraries not supplied under the same name by foundation:",report["player_missing_from_foundation_by_name"])
print("Verified reference kernel SHA-256:",kernel_identity["sha256"])
print("Reference modules:",len(modules),"literal startup hints:",len(boot_hints))
print("Hardware integration remains unverified; no firmware created.")
