#!/usr/bin/env bash
# Offline KOReader MIPS integration only; never installs or flashes firmware.
set -Eeuo pipefail
[ "$#" -eq 3 ] || { echo "usage: $0 ROOTFS RUNTIME REPORT" >&2; exit 2; }
R=$(realpath "$1")
K=$(realpath "$2")
REPORT=$(realpath -m "$3")
mkdir -p "$REPORT"
exec > >(tee "$REPORT/summary.log") 2>&1
fail() { echo "::error::$*" >&2; echo "$*" >>"$REPORT/BLOCKERS.txt"; exit 1; }
echo "PocketOS KOReader native runtime / boot-chain preflight: NOT HARDWARE BOOT"
for f in etc/inittab etc/init.d/rcS etc/init.d/S95pocketos usr/bin/pocketos-start usr/sbin/pocketos-r1-boot etc/pocketos-release etc/pocketos/koreader-sd.sha256 etc/pocketos/autostart; do
  [ -f "$R/$f" ] || fail "Missing rootfs startup component: $f"
done
[ -x "$R/usr/bin/pocketos-start" ] || fail "Reader launcher not executable"
grep -Fq '/usr/bin/pocketos-start' "$R/etc/init.d/S95pocketos" || fail "S95pocketos cannot launch reader"
grep -Fq '/usr/bin/pocketos-start' "$R/usr/sbin/pocketos-r1-boot" || fail "Boot supervisor does not invoke reader"
grep -Fxq NAME=PocketOS "$R/etc/pocketos-release" || fail "Wrong OS identity"
grep -Fxq TARGET=hiby-r1 "$R/etc/pocketos-release" || fail "Wrong hardware target"
sh -n "$R/etc/init.d/S95pocketos"
sh -n "$R/usr/sbin/pocketos-r1-boot"
sh -n "$R/usr/bin/pocketos-start"
for old in usr/bin/pocketos-shell usr/bin/pocketos-music usr/bin/pocketos-game; do
  [ ! -e "$R/$old" ] || fail "Legacy UI remains: $old"
done
grep -Fq 'sha256sum -c "$manifest"' "$R/usr/bin/pocketos-start" || fail "Runtime hash guard absent"
grep -Fq 'mount -t squashfs -o ro,nosuid,nodev,loop,exec' "$R/usr/bin/pocketos-start" || fail "Protected RO loop mount absent"
grep -Fq 'exec ./luajit ./reader.lua' "$R/usr/bin/pocketos-start" || fail "KOReader is not the target UI"
[ -x "$K/luajit" ] && [ -f "$K/reader.lua" ] || fail "Native runtime missing"
[ -f "$K/frontend/device/hibyr1/device.lua" ] || fail "R1 backend absent"
[ -d "$K/plugins" ] || fail "Plugin directory missing"
plugins=$(find "$K/plugins" -mindepth 1 -maxdepth 1 -type d -name '*.koplugin' | wc -l)
[ "$plugins" -ge 20 ] || fail "Incomplete plugin tree: $plugins"
echo "PASS: startup wiring, OS identity, no legacy UI, protected SD mount, $plugins plugins"
readelf -h "$K/luajit" > "$REPORT/luajit-elf.txt"
readelf -l "$K/luajit" > "$REPORT/luajit-program.txt"
readelf -A "$K/luajit" > "$REPORT/luajit-cpu-attributes.txt"
grep -Eq 'Machine:[[:space:]]+MIPS' "$REPORT/luajit-elf.txt" || fail "Not MIPS"
grep -Fq 'little endian' "$REPORT/luajit-elf.txt" || fail "Wrong endian"
loader=$(sed -n 's/.*Requesting program interpreter: \([^]]*\).*/\1/p' "$REPORT/luajit-program.txt")
[ -n "$loader" ] && [ -e "$R$loader" ] || fail "Rootfs missing native loader $loader"
echo "PASS: MIPS32 LuaJIT and rootfs dynamic loader"
: >"$REPORT/elf-needed.txt"
: >"$REPORT/elf-unresolved.txt"
count=0
while IFS= read -r -d '' lib; do
  if ! readelf -h "$lib" >"$REPORT/tmp-header" 2>/dev/null; then continue; fi
  count=$((count + 1))
  grep -Eq 'Machine:[[:space:]]+MIPS' "$REPORT/tmp-header" || fail "Wrong ELF architecture in $lib"
  grep -Fq 'little endian' "$REPORT/tmp-header" || fail "Wrong ELF endian in $lib"
  while IFS= read -r needed; do
    [ -n "$needed" ] || continue
    echo "$lib needs $needed" >>"$REPORT/elf-needed.txt"
    if ! find "$R/lib" "$R/usr/lib" "$K" -name "$needed" -print -quit 2>/dev/null | grep -q .; then
      echo "$lib: $needed" >>"$REPORT/elf-unresolved.txt"
    fi
  done < <(readelf -d "$lib" 2>/dev/null | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
done < <(find "$K" -type f \( -name luajit -o -name '*.so' -o -name '*.so.*' \) -print0)
rm -f "$REPORT/tmp-header"
if [ -s "$REPORT/elf-unresolved.txt" ]; then cat "$REPORT/elf-unresolved.txt"; fail "Unresolved native shared libraries"; fi
echo "PASS: $count packaged ELF objects have MIPS architecture and declared libraries are present"
(
  cd "$K"
  export KO_PLATFORM=hibyr1
  export LD_LIBRARY_PATH="$K/libs:$K"
  timeout 90 qemu-mipsel -L "$R" "$K/luajit" -e '
    package.path = "./?.lua;./?/init.lua;./frontend/?.lua;./frontend/?/init.lua;" .. package.path
    package.cpath = "./libs/?.so;./libs/lib?.so;./?.so;" .. package.cpath
    require("setupkoenv") -- Match genuine KOReader boot: installs ffi.loadlib and correct require paths.
    assert(jit.arch:match("^mips"), "Not native MIPS")
    assert(type(require("ffi")) == "table", "LuaJIT FFI missing")
    local modules = {"ffi/blitbuffer", "ffi/framebuffer_linux", "device/generic/device", "device/input", "device/hibyr1/device"}
    local errors = 0
    for _, name in ipairs(modules) do
      local ok, result = pcall(require, name)
      if ok then print("PASS: module " .. name)
      else io.stderr:write("BLOCKER: module " .. name .. ": " .. tostring(result) .. "\n"); errors = errors + 1 end
    end
    if errors > 0 then os.exit(10) end
    local BB = require("ffi/blitbuffer")
    local bb = BB.new(24, 24, BB.TYPE_BBRGB16)
    assert(bb and bb:getWidth() == 24 and bb:getHeight() == 24, "RGB565 framebuffer test failed")
    print("PASS: 24x24 native RGB565 offscreen framebuffer")
    assert(type(loadfile("reader.lua")) == "function", "KOReader entry is not parseable")
    print("PASS: upstream KOReader entry parser")
  '
) || fail "QEMU native KOReader graphical/input modules failed"
echo "PASS: native MIPS KOReader modules, software RGB565 and entry under QEMU"
echo "UNVERIFIED: full-board boot, real LCD, real touchscreen, actual R1 loop support, 64MiB RAM and recovery"
