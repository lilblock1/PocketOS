#!/usr/bin/env python3
"""Provision public update trust and installed sequence into an offline R1 rootfs."""
import argparse
import os
import secrets
import subprocess
from pathlib import Path

def provision(root, key, sequence):
    if root.is_symlink() or not root.is_dir() or root.resolve() == Path("/"):
        raise ValueError("Requires a disposable offline rootfs")
    root = root.resolve()
    # Never edit mounted rootfs trees.
    for line in Path("/proc/self/mountinfo").read_text().splitlines():
        mount = line.split()[4].replace("\\040", " ")
        m = Path(mount)
        if m == root or root in m.parents:
            raise ValueError("Refusing mounted candidate")
    if not 1 <= sequence <= 999999999:
        raise ValueError("Invalid sequence")
    etc = root / "etc"
    config = etc / "pocketos"
    release = etc / "pocketos-release"
    if any(p.is_symlink() for p in (etc, config, release)) or not config.is_dir() or not release.is_file():
        raise ValueError("Unsafe candidate metadata")
    lines = release.read_text().splitlines()
    if not {"NAME=PocketOS","TARGET=hiby-r1","STATE=development-userspace-only","FLASHABLE=no"}.issubset(lines):
        raise ValueError("Wrong candidate identity")
    if os.geteuid() != 0 or config.stat().st_uid != 0 or config.stat().st_mode & 0o777 != 0o755:
        raise ValueError("Use a root-owned 0755 candidate configuration directory")
    if key.is_symlink() or not key.is_file() or key.stat().st_size > 8192:
        raise ValueError("Invalid public key input")
    raw = key.read_bytes()
    if not raw.startswith(b"-----BEGIN PUBLIC KEY-----") or b"PRIVATE KEY" in raw:
        raise ValueError("Only a PEM public key is permitted")
    # Canonical public component; no private material is copied.
    public = subprocess.check_output(["openssl","pkey","-pubin","-in",str(key),"-pubout"])
    marker = config / "update-enable"
    dest = config / "update-public.pem"
    if any(p.exists() or p.is_symlink() for p in (marker,dest)) or any(x.startswith("UPDATE_SEQUENCE=") for x in lines):
        raise ValueError("Use fresh candidate metadata; no key replacement")
    def create(path,data,mode):
        with path.open("xb") as f:
            f.write(data)
        path.chmod(mode)
    tmp = etc / (".pocketos-release-" + secrets.token_hex(8))
    create(tmp, ("\n".join(lines+[f"UPDATE_SEQUENCE={sequence}"])+"\n").encode(), 0o644)
    # Approval marker last: incomplete provisioning remains locked.
    create(dest,public,0o444)
    tmp.replace(release)
    create(marker,b"POCKETOS-R1-UPDATES-1\n",0o400)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("root",type=Path)
    p.add_argument("public_key",type=Path)
    p.add_argument("sequence",type=int)
    a=p.parse_args()
    provision(a.root,a.public_key,a.sequence)
    print("Offline update trust provisioned; no recovery handoff enabled.")
if __name__=="__main__":
    main()
