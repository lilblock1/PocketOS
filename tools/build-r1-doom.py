#!/usr/bin/env python3
"""Build the pinned Doom engine with PocketOS framebuffer controls, using SDK only."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tarfile
from pathlib import Path

PIN = "e257a09527062bfb986b6e5e0780839b6cbd5b71"
REPO = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True).strip()
    if commit != PIN:
        parser.error("Doom source commit differs from reviewed pin")
    subprocess.run(["git", "-C", str(args.source), "diff", "--exit-code", "HEAD"], check=True)
    if args.output.exists() or args.output.is_symlink():
        parser.error("Use a new output directory")
    args.output.mkdir(parents=True)
    source = args.source / "doomgeneric"
    line = re.search(r"^SRC_DOOM = (.+)$", (source / "Makefile").read_text(), re.M)[1]
    files = [source / (name[:-2] + ".c") for name in line.split() if name != "doomgeneric_xlib.o"]
    flags = ["-Os", "-ffunction-sections", "-fdata-sections", "-fcommon", "-D_DEFAULT_SOURCE",
             "-DNORMALUNIX", "-DLINUX", "-DSNDSERV", "-DDOOMGENERIC_RESX=320", "-DDOOMGENERIC_RESY=200",
             "-I" + str(source), "-I" + str(REPO / "apps")]
    objects = []
    for path in files + [REPO / "games/doom/doomgeneric_r1.c"]:
        obj = args.output / (path.stem + ".o")
        extra = ["-Wall", "-Wextra", "-Werror", "-Wno-unused-function"] if path.name == "doomgeneric_r1.c" else []
        subprocess.run([args.cc, *flags, *extra, "-c", str(path), "-o", str(obj)], check=True)
        objects.append(obj)
    binary = args.output / "pocketos-doom"
    subprocess.run([args.cc, *map(str, objects), "-Wl,--gc-sections", "-lm", "-o", str(binary)], check=True)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    (args.output / "pocketos-doom.sha256").write_text(digest + "\n")
    shutil.copyfile(args.source / "LICENSE", args.output / "COPYING")
    (args.output / "SOURCE.json").write_text(json.dumps({"upstream": "https://github.com/ozkl/doomgeneric",
        "commit": PIN, "backend": "games/doom/doomgeneric_r1.c", "binary_sha256": digest,
        "backend_sha256": hashlib.sha256((REPO / "games/doom/doomgeneric_r1.c").read_bytes()).hexdigest(),
        "display": "320x200 rendered to a 480x360 viewport", "sound": False,
        "storage": "microSD; executable verified and copied to RAM only while playing"}, indent=2) + "\n")
    bundle = args.output / "source"
    shutil.copytree(source, bundle / "engine")
    (bundle / "pocketos").mkdir()
    for path in (REPO / "games/doom/doomgeneric_r1.c", REPO / "apps/pocketos-hardware.h", REPO / "apps/pocketos-touch.h"):
        shutil.copyfile(path, bundle / "pocketos" / path.name)
    shutil.copyfile(args.source / "LICENSE", bundle / "COPYING")
    shutil.copyfile(REPO / "games/doom/README.md", bundle / "README.md")
    (bundle / "UPSTREAM_COMMIT").write_text(PIN + "\n")
    names = [path.stem for path in files] + ["doomgeneric_r1"]
    makefile = "CC ?= cc\nCFLAGS ?= -Os\nCPPFLAGS += " + " ".join(flag for flag in flags if not flag.startswith("-I") and flag != "-Os") + " -Iengine -Ipocketos\n"
    makefile += "OBJS = " + " ".join("obj/" + name + ".o" for name in names) + "\n"
    makefile += "all: pocketos-doom\npocketos-doom: $(OBJS)\n\t$(CC) $(OBJS) -Wl,--gc-sections -lm -o $@\n"
    makefile += "obj:\n\tmkdir -p obj\nobj/%.o: engine/%.c | obj\n\t$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@\n"
    makefile += "obj/doomgeneric_r1.o: pocketos/doomgeneric_r1.c pocketos/pocketos-hardware.h pocketos/pocketos-touch.h | obj\n\t$(CC) $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror -Wno-unused-function -c $< -o $@\n"
    (bundle / "Makefile").write_text(makefile)
    with tarfile.open(args.output / "pocketos-doom-source.tar.gz", "w:gz") as archive:
        archive.add(bundle, arcname="pocketos-doom-source")
    print("Built PocketOS Doom; game data is separate and no device was accessed.")


if __name__ == "__main__":
    main()
