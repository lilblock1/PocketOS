#!/usr/bin/env python3
"""Refresh only updater applications in the digest-pinned Phase 5 preview."""
import argparse
import hashlib
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

PREVIEW_SHA = "fa36d95736555dba9ce95e33a29227668270d524f1cb419a751506f59d10a074"
REPLACEMENTS = {
    "pocketos-shell":"usr/bin/pocketos-shell",
    "pocketos-update":"usr/bin/pocketos-update",
    "pocketos-start":"usr/bin/pocketos-start",
    "pocketos-recovery-arm":"usr/sbin/pocketos-recovery-arm",
}

def digest(path):
    with path.open("rb") as f:
        return hashlib.file_digest(f,"sha256").hexdigest()

def prepare(preview,apps,root):
    if root.exists() or root.is_symlink() or root.resolve()==Path("/"):
        raise ValueError("Requires new offline root")
    if digest(preview)!=PREVIEW_SHA:
        raise ValueError("Preview UPT digest differs")
    with tempfile.TemporaryDirectory() as tmp:
        work=Path(tmp)
        subprocess.run(["7z","x","-y",str(preview.resolve()),"-o"+str(work/"iso")],
                       check=True,stdout=subprocess.DEVNULL)
        iso=work/"iso"; ota=iso/"ota_v0"
        text=(ota/"ota_update.in").read_text()
        m=re.fullmatch(r"ota_version=0\n\nimg_type=rootfs\nimg_name=rootfs\.squashfs\nimg_size=([0-9]+)\nimg_md5=([a-f0-9]{32})\n",text)
        if not m or not 0<int(m[1])<=16777216:
            raise ValueError("Unexpected preview image plan")
        size=int(m[1]); whole=m[2]; prev=whole
        hashes=(ota/("ota_md5_rootfs.squashfs."+whole)).read_text().splitlines()
        if len(hashes)!=(size+524287)//524288:
            raise ValueError("Wrong preview chunk count")
        image=work/"root.squashfs"
        with image.open("xb") as out:
            for i, expected in enumerate(hashes):
                if not re.fullmatch("[a-f0-9]{32}",expected):
                    raise ValueError("Wrong preview chunk digest")
                chunk=(ota/f"rootfs.squashfs.{i:04d}.{prev}").read_bytes()
                if len(chunk)!=min(524288,size-i*524288) or hashlib.md5(chunk).hexdigest()!=expected:
                    raise ValueError("Corrupt preview chunk")
                out.write(chunk);prev=expected
        if hashlib.md5(image.read_bytes()).hexdigest()!=whole:
            raise ValueError("Preview rootfs differs")
        subprocess.run(["unsquashfs","-no-progress","-d",str(root),str(image)],check=True)
    cfg=root/"etc/pocketos"; release=root/"etc/pocketos-release"
    if cfg.is_symlink() or release.is_symlink():
        raise ValueError("Unexpected metadata symlink")
    lines=set(release.read_text().splitlines())
    if not {"NAME=PocketOS","TARGET=hiby-r1","STATE=development-userspace-only","FLASHABLE=no","BUILD_ID=2849219495a83c35"}.issubset(lines):
        raise ValueError("Unexpected preview identity")
    for name in ("update-public.pem","update-enable","recovery-enable"):
        if (cfg/name).exists() or (cfg/name).is_symlink():
            raise ValueError("Unexpected embedded trust")
    if any(x.startswith("UPDATE_SEQUENCE=") for x in lines):
        raise ValueError("Unexpected installed release sequence")
    if (cfg/"sd-write-enable").read_text()!="POCKETOS-R1-SD-WRITE-1\n":
        raise ValueError("Preview does not enable protected SD writes")
    for name,loc in REPLACEMENTS.items():
        source=apps/name
        if source.is_symlink() or not source.is_file():
            raise ValueError("Missing application: "+name)
        dest=root/loc
        if dest.is_symlink() or dest.parent.is_symlink() or not dest.parent.is_dir():
            raise ValueError("Unsafe destination")
        if name in ("pocketos-shell","pocketos-recovery-arm"):
            report=subprocess.check_output(["readelf","-h",str(source)],text=True)
            if not all(x in report for x in ("ELF32","little endian","MIPS")):
                raise ValueError("Wrong target ABI")
        shutil.copyfile(source,dest)
        dest.chmod(0o755)
    print("Digest-pinned preview refreshed. No trust key or recovery approval installed.")

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for name in ("preview","apps","root"):p.add_argument(name,type=Path)
    a=p.parse_args();prepare(a.preview,a.apps,a.root)
