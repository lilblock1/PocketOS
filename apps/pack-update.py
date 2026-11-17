#!/usr/bin/env python3
"""Create flat, signed R1 SD development packages. Never publish or flash."""
import argparse
import hashlib
import re
import shutil
import subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("--sequence",type=int,required=True)
p.add_argument("--channel",choices=("stable","test"),default="test")
p.add_argument("--key",type=Path,required=True)
p.add_argument("--output",type=Path,required=True)
p.add_argument("files",type=Path,nargs="+")
a=p.parse_args()
if not 1<=a.sequence<=999999999 or not 1<=len(a.files)<=64:
    p.error("Invalid sequence or file count")
rows=[f"POCKETOS-SD-1 {a.channel} r1-mipsel {a.sequence}"]
total=0
names=set()
for f in a.files:
    if not f.is_file() or f.is_symlink() or not re.fullmatch(r"(pocketos-|data-)[A-Za-z0-9][A-Za-z0-9._-]{0,69}",f.name) or f.name in names:
        p.error(f"Invalid or duplicate package: {f}")
    names.add(f.name)
    size=f.stat().st_size
    total+=size
    if not 0<size<=268435456 or total>536870912:
        p.error("Package size limit exceeded")
    with f.open("rb") as stream:
        digest=hashlib.file_digest(stream,"sha256").hexdigest()
    rows.append(f"{digest} {size} {f.name}")
a.output.mkdir(parents=True,exist_ok=False)
for f in a.files:
    shutil.copyfile(f,a.output/f.name)
(a.output/"manifest").write_text("\n".join(rows)+"\n")
subprocess.run(["openssl","dgst","-sha256","-sign",str(a.key),"-out",str(a.output/"manifest.sig"),str(a.output/"manifest")],check=True)
print(f"Signed {a.channel} staging package. Not an installer.")
