#!/usr/bin/env python3
"""Inventory the verified R1 rootfs; never run any target program or driver."""
import argparse
import json
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
candidate=data["candidate_rootfs"]["elf"]
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
        path=root/name
        if path.is_file() and not path.is_symlink():
            item["vermagic"]=module_field(path,"vermagic")
            item["depends"]=module_field(path,"depends")
    groups.setdefault(group,[]).append(item)
player=elfs.get("usr/bin/sonix_player")
assert player and groups, "No expected hardware/reference player inventory"
report={"phase":2,"flashable":False,"hardware_verified":False,
        "foundation_fp_matches":True,
        "groups":groups,
        "reference_player_needed":player["needed"],
        "player_missing_from_foundation_by_name":[n for n in player["needed"] if n not in candidate_names],
        "requirements":["same kernel and module compatibility","device-specific load order",
                        "exact dependency and symbol resolution","physical hardware validation"],
        "caution":"Filename-based library checks are only hints, never ABI or hardware compatibility proof."}
a.output.write_text(json.dumps(report,indent=2)+"\n")
for group,items in sorted(groups.items()):
    print(group, len(items), "reference components")
print("Player libraries not supplied under the same name by foundation:",report["player_missing_from_foundation_by_name"])
print("Hardware integration remains unverified; no firmware created.")
