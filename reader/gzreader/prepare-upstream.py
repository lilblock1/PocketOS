#!/usr/bin/env python3
"""Prepare a pinned GZreader/ZenOS tree for R1 MIPS32 Linux compilation.

Changes are made only in a temporary CI source checkout. Original KOReader
frontend, its plugin folders, and all license files remain intact.
"""
from pathlib import Path
import argparse
import shutil

UPSTREAM = "936f66b753e2a65427595920c4e8f36c10ec29e5"
DISPATCH = '    if isAndroid then'
TARGET = 'ifeq ($(TARGET), arm-generic)\n\tCHOST?=arm-linux-gnueabi'
FLAGS = 'ifeq ($(TARGET), kobo)\n\t# NOTE: If we only care about Kobos w/ a Touch screen, we\'re good.'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--overlay", required=True, type=Path)
    args = parser.parse_args()

    root = args.upstream.resolve(strict=True)
    overlay = args.overlay.resolve(strict=True)
    if root == overlay or overlay.is_relative_to(root):
        parser.error("Overlay must be separate from upstream checkout")
    for path in (
        "reader.lua", "frontend/device.lua",
        "base/Makefile.defs", "Makefile", "plugins",
    ):
        if not (root / path).exists():
            parser.error("Missing upstream file " + path)
    if not (root / "plugins").is_dir():
        parser.error("Missing full upstream plugin tree")
    initial = sorted(p.name for p in (root / "plugins").glob("*.koplugin"))
    # GZreader intentionally ships a curated five-plugin tree instead of
    # upstream KOReader's complete plugin collection. Keep it intact.
    if len(initial) < 5 or not {"gzreader.koplugin", "zenos.koplugin"}.issubset(set(initial)):
        parser.error("GZreader and ZenOS plugin tree is incomplete")
    source = (root / "frontend/device.lua").read_text()
    patched = (overlay / "frontend/device.lua").read_text()
    if source.count(DISPATCH) != 1 or "GZ_DEVICE" not in source or "local function probeDevice()" not in source:
        parser.error("GZreader device dispatcher changed; refuse unreviewed patch")
    if not (overlay / "frontend/device/hibyr1/device.lua").is_file():
        parser.error("Missing reviewed PocketOS R1 LCD and touch backend")
    # Select native R1 backend before GZreader's Kindle/Nook/Kobo detection.
    # Keep every GZreader/ZenOS UI change and all original plugins intact.
    source = source.replace(DISPATCH,
        '    if os.getenv("KO_PLATFORM") == "hibyr1" then\n'
        '        return require("device/hibyr1/device")\n'
        '    end\n\n' + DISPATCH, 1)
    (root / "frontend/device.lua").write_text(source)
    target = root / "frontend/device/hibyr1"
    if target.exists():
        parser.error("Refuse to replace upstream hibyr1 backend")
    shutil.copytree(overlay / "frontend/device/hibyr1", target)

    if "Grimmory" not in (root / "README.md").read_text():
        parser.error("Wrong upstream source: expected GZreader")
    path = root / "base/Makefile.defs"
    defs = path.read_text()
    if defs.count(TARGET) != 1 or defs.count(FLAGS) != 1:
        parser.error("Upstream build selectors changed; refusing unsafe patch")
    defs = defs.replace(TARGET,
        "ifeq ($(TARGET), hibyr1)\n"
        "\tCHOST?=mipsel-linux-gnu\n"
        "else " + TARGET, 1)
    defs = defs.replace(FLAGS,
        "ifeq ($(TARGET), hibyr1)\n"
        "\tTARGET_CFLAGS:=-march=mips32r2 -mabi=32\n"
        "else " + FLAGS, 1)
    # Meson upstream does not recognize MIPS in its platform matrix.
    meson_anchor = "else ifneq (,$(filter i686-%,$(TARGET_MACHINE)))"
    if defs.count(meson_anchor) != 1:
        parser.error("Upstream Meson CPU family selectors changed")
    defs = defs.replace(meson_anchor,
        "else ifneq (,$(filter mipsel-% mips-%,$(TARGET_MACHINE)))\n"
        "  MESON_CROSS_CPU_FAMILY = mips\n"
        + meson_anchor, 1)
    # LuaJIT's target is 32-bit, so its host-side buildvm also compiles as
    # 32-bit on GitHub's amd64 runner. Ubuntu's split asm include directory
    # is required on that runner; it is not passed to the target MIPS CC.
    host_anchor = "HOSTCFLAGS:=$(BASE_CFLAGS) $(QFLAGS)"
    if defs.count(host_anchor) != 1:
        parser.error("Upstream host flags changed")
    defs = defs.replace(host_anchor, host_anchor +
        "\nifeq ($(TARGET), hibyr1)\n"
        "  HOSTCFLAGS+=-isystem /usr/include/x86_64-linux-gnu\n"
        "endif", 1)
    # libjpeg-turbo has no SIMD/ASM implementation for the R1 MIPS32 CPU.
    # Retain its portable C JPEG decoder; turn off SIMD only for hibyr1.
    simd_anchor = "WANT_SIMD=1\n"
    if defs.count(simd_anchor) != 1:
        parser.error("Upstream SIMD selector changed; refusing unsafe patch")
    defs = defs.replace(simd_anchor,
        simd_anchor + "ifeq ($(TARGET), hibyr1)\n"
        "  WANT_SIMD=\n"
        "endif\n", 1)
    # On 32-bit MIPS with 64-bit time_t, Linux input_event uses __sec
    # and __usec instead of the timeval member. The kernel header
    # supplies portable input_event_sec/input_event_usec aliases.
    input_path = root / "base/input/input.c"
    input_source = input_path.read_text()
    ev_anchor = "    struct input_event ev = { 0 };\n"
    timestamp_anchor = "    gettimeofday(&ev.time, NULL);\n"
    if (input_source.count(ev_anchor) != 1 or
            input_source.count(timestamp_anchor) != 10 or
            input_source.count("input->time.tv_sec") != 1 or
            input_source.count("input->time.tv_usec") != 1):
        parser.error("Upstream input_event timestamp API changed")
    input_source = input_source.replace(ev_anchor,
        ev_anchor + "    struct timeval event_time;\n", 1)
    input_source = input_source.replace(timestamp_anchor,
        "    gettimeofday(&event_time, NULL);\n"
        "    ev.input_event_sec = event_time.tv_sec;\n"
        "    ev.input_event_usec = event_time.tv_usec;\n")
    input_source = input_source.replace("input->time.tv_sec",
        "input->input_event_sec", 1)
    input_source = input_source.replace("input->time.tv_usec",
        "input->input_event_usec", 1)
    input_path.write_text(input_source)
    path.write_text(defs)
    make_dir = root / "make"
    (make_dir / "hibyr1.mk").write_text(
        "# HiBy R1 user-space KOReader port. No flash target is provided.\n"
        "update-prepare: all\n"
        "\tfile --dereference $(INSTALL_DIR)/koreader/luajit | grep MIPS\n"
        ".PHONY: update-prepare\n"
    )
    after = sorted(p.name for p in (root / "plugins").glob("*.koplugin"))
    if after != initial:
        parser.error("KOReader plugin tree was modified unexpectedly")
    print(f"Prepared upstream GZreader + ZenOS device port with {len(after)} original plugin folders")
    print("Retains PocketOS-only boot/update path; this is not signed or flashable firmware")


if __name__ == "__main__":
    main()
