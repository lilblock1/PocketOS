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
[ "$plugins" -ge 37 ] || fail "Incomplete upstream plugin tree: $plugins (need 37+)"
grep -Fq 'linux_gnu_mipsel=' "$K/ffi/posix_h.lua" || fail "Compiler-generated GNU MIPS POSIX declarations missing"
if grep -Fq 'linux_mipsel=' "$K/ffi/posix_h.lua"; then fail "Nonstandard MIPS POSIX platform key present"; fi
[ ! -e "$K/plugins/zenos.koplugin" ] || fail "ZenOS plugin must not be packaged"
[ ! -e "$K/plugins/gzreader.koplugin" ] || fail "GZreader plugin must not be packaged"
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
  # Match reader.lua's settings initialization prior to loading gestures/input.
  export KO_HOME="$REPORT/KOReader-test-settings"
  mkdir -p "$KO_HOME"
  timeout 90 qemu-mipsel -L "$R" "$K/luajit" -e '
    package.path = "./?.lua;./?/init.lua;./frontend/?.lua;./frontend/?/init.lua;" .. package.path
    package.cpath = "./libs/?.so;./libs/lib?.so;./?.so;" .. package.cpath
    require("setupkoenv") -- Match genuine KOReader boot: installs ffi.loadlib and correct require paths.
    assert(jit.arch:match("^mips"), "Not native MIPS")
    assert(type(require("ffi")) == "table", "LuaJIT FFI missing")
    G_defaults = require("luadefaults"):open()
    local DataStorage = require("datastorage")
    G_reader_settings = require("luasettings"):open(DataStorage:getDataDir() .. "/settings.reader.lua")
    local modules = {"ffi/posix_h", "ffi/blitbuffer", "ffi/framebuffer_linux", "device/generic/device", "device/input", "device/hibyr1/device"}
    local errors = 0
    for _, name in ipairs(modules) do
      local ok, result = pcall(require, name)
      if ok then print("PASS: module " .. name)
      else io.stderr:write("BLOCKER: module " .. name .. ": " .. tostring(result) .. "\n"); errors = errors + 1 end
    end
    if errors > 0 then os.exit(10) end
    local ffi = require("ffi")
    -- Check generated LuaJIT declarations against the concrete GNU MIPS32
    -- ABI, not just symbol availability or typedef spelling.
    assert(ffi.sizeof("time_t") == 8, "GNU MIPS time_t must be 64-bit")
    assert(ffi.sizeof("off_t") == 8, "GNU MIPS off_t must be 64-bit")
    assert(ffi.sizeof("struct timeval") == 16, "GNU MIPS timeval size mismatch")
    assert(ffi.offsetof("struct timeval", "tv_sec") == 0, "timeval.tv_sec offset mismatch")
    assert(ffi.offsetof("struct timeval", "tv_usec") == 8, "timeval.tv_usec offset mismatch")
    print("PASS: generated GNU MIPS time_t/off_t and timeval layout match target ABI")
    for _, sym in ipairs({"clock_gettime", "gettimeofday", "open", "statvfs"}) do
      local ok, err = pcall(function() return ffi.C[sym] end)
      assert(ok, "Missing MIPS libc symbol " .. sym .. ": " .. tostring(err))
      print("PASS: native libc symbol " .. sym)
    end
    local BB = require("ffi/blitbuffer")
    local bb = BB.new(24, 24, BB.TYPE_BBRGB16)
    assert(bb and bb:getWidth() == 24 and bb:getHeight() == 24, "RGB565 framebuffer allocation failed")
    assert(bb:getBytesPerPixel() == 2 and bb.stride >= 48, "RGB565 row stride mismatch")

    -- Exercise real KOReader color conversion and pixel storage, not just
    -- allocation. On little-endian MIPS32, RGB565 primaries have these words
    -- and byte sequences in the off-screen buffer.
    local raw = ffi.cast("uint8_t*", bb.data)
    local pixels = {
      { "#FF0000", 0xF800, 0x00, 0xF8, "red" },
      { "#00FF00", 0x07E0, 0xE0, 0x07, "green" },
      { "#0000FF", 0x001F, 0x1F, 0x00, "blue" },
      { "#FFFFFF", 0xFFFF, 0xFF, 0xFF, "white" },
    }
    for x, p in ipairs(pixels) do
      local px = x - 1
      bb:setPixel(px, 0, BB.colorFromString(p[1]))
      assert(bb:getPixel(px, 0).v == p[2], "RGB565 " .. p[5] .. " packed word mismatch")
      assert(raw[px * 2] == p[3] and raw[px * 2 + 1] == p[4],
             "RGB565 " .. p[5] .. " little-endian byte order mismatch")
    end
    print("PASS: RGB565 primary/white pixel packing, byte order and readback")

    -- Linux input_event is a kernel evdev ABI, not glibc's public timeval:
    -- MIPS32 long remains 32-bit, so this upstream cdef must stay 16 bytes
    -- even though GNU MIPS public time_t/timeval are 64-bit.
    require("ffi/linux_input_h")
    assert(ffi.sizeof("long") == 4, "MIPS32 long width mismatch")
    assert(ffi.sizeof("struct input_event") == 16, "MIPS32 evdev input_event size mismatch")
    assert(ffi.offsetof("struct input_event", "type") == 8, "evdev type offset mismatch")
    assert(ffi.offsetof("struct input_event", "code") == 10, "evdev code offset mismatch")
    assert(ffi.offsetof("struct input_event", "value") == 12, "evdev value offset mismatch")
    local ev = ffi.new("struct input_event")
    ev.type, ev.code, ev.value = 1, 330, 1 -- EV_KEY / BTN_TOUCH press
    assert(ev.type == 1 and ev.code == 330 and ev.value == 1, "synthetic touch event layout mismatch")
    ev.type, ev.code, ev.value = 3, 53, 1234 -- EV_ABS / ABS_MT_POSITION_X
    assert(ev.type == 3 and ev.code == 53 and ev.value == 1234, "synthetic touch coordinate layout mismatch")
    print("PASS: MIPS32 evdev input_event size/offsets and synthetic key/touch records")
    assert(type(loadfile("reader.lua")) == "function", "KOReader entry is not parseable")
    print("PASS: upstream KOReader entry parser")
  '
) || fail "QEMU native KOReader graphical/input modules failed"
echo "PASS: native MIPS KOReader modules, software RGB565 and entry under QEMU"
echo "UNVERIFIED: full-board boot, real LCD, real touchscreen, actual R1 loop support, 64MiB RAM and recovery"
