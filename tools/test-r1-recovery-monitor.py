#!/usr/bin/env python3
"""Run the known recovery monitor under host QEMU with inert boundaries only."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile

PIN = "6a7a9b48eaa530d7d23ff7212c7933acfb59e28b5e98626758dba2f116d48b4d"
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("recovery", type=Path)
p.add_argument("compiler", type=Path)
p.add_argument("sysroot", type=Path)
a = p.parse_args()
if hashlib.sha256(a.recovery.read_bytes()).hexdigest() != PIN:
    p.error("Wrong recovery ELF: fixture addresses must never be guessed")
if os.uname().release == "4.4.94-sonix":
    p.error("Host-only fixture; do not run on R1")
source = Path(__file__).with_suffix(".c")
with tempfile.TemporaryDirectory(prefix="pocketos-monitor-fixture-") as tmp:
    so = Path(tmp) / "fixture.so"
    # SDK compiler symlinks select their backend through argv[0].
    subprocess.run([str(a.compiler.absolute()), "-shared", "-fPIC", "-Os", "-Wall", "-Wextra", "-Werror",
                    str(source.resolve()), "-o", str(so)], check=True)
    for mode, code, expected in (
        ("rootfs-only", 42, "MONITOR-STUCK: rootfs complete, missing xImage.ok, no normal-boot request"),
        ("kernel-and-rootfs", 0, "MONITOR-COMPLETED: finish state and one normal-boot request"),
    ):
        run = subprocess.run(["qemu-mipsel", "-L", str(a.sysroot.resolve()),
                              "-E", "LD_PRELOAD="+str(so),
                              "-E", "POCKETOS_MONITOR_FIXTURE=host-qemu-only",
                              "-E", "POCKETOS_MONITOR_KIND="+mode,
                              str(a.recovery.resolve())], cwd=tmp, capture_output=True, timeout=10)
        out = run.stdout.decode(errors="replace")
        if run.returncode != code or expected not in out:
            raise RuntimeError((mode, run.returncode, out, run.stderr.decode(errors="replace")))
        print(expected)
print("PASS: original recovery monitor reproduced; no flash/reboot commands executed")
