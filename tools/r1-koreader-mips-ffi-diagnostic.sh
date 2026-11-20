#!/usr/bin/env bash
# Diagnostic ONLY: intentionally unsound GNU ARM ABI alias for MIPS,
# applied to an extracted CI copy (NEVER to firmware or release artifacts).
set -Eeuo pipefail
[ "$#" -eq 3 ] || exit 2
R=$(realpath "$1")
K=$(realpath "$2")
REPORT=$(realpath -m "$3")
mkdir -p "$REPORT"
if [ ! -f "$K/ffi/posix_h.lua" ] || [ ! -x "$K/luajit" ]; then
  echo "SKIP: extracted KOReader runtime absent" | tee "$REPORT/experimental-ffi.txt"
  exit 0
fi
echo "EXPERIMENTAL FFI DIAGNOSTIC ONLY - NO FIRMWARE CHANGES" | tee "$REPORT/experimental-ffi.txt"
echo "Temporarily map MIPS to ARM cdefs to inventory *additional* module failures." | tee -a "$REPORT/experimental-ffi.txt"
echo "This mapping DOES NOT establish compatible layouts, syscalls, input events, stat or clock data." | tee -a "$REPORT/experimental-ffi.txt"
cp "$K/ffi/posix_h.lua" "$REPORT/original-posix_h.lua"
python3 - "$K/ffi/posix_h.lua" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])
s=p.read_text()
needle='local platform = ({ android_arm='
assert s.count(needle)==1, "Different upstream generated POSIX declarations; do not fake the platform"
s=s.replace(needle,
'''if platform_str == "linux_gnu_mipsel" then
    -- CI only: ABI INCOMPATIBLE until generated MIPS declarations are reviewed.
    platform_str = "linux_gnu_arm"
end
local platform = ({ android_arm=''', 1)
p.write_text(s)
PY
(
  cd "$K"
  export KO_PLATFORM=hibyr1
  export LD_LIBRARY_PATH="$K/libs:$K"
  # Use the same settings bootstrap as the real KOReader module-load gate.
  # Without G_reader_settings, gesture/input modules fail for a test-harness
  # reason unrelated to the intentionally incompatible ARM-cdefs experiment.
  export KO_HOME="$REPORT/KOReader-diagnostic-settings"
  mkdir -p "$KO_HOME"
  timeout 90 qemu-mipsel -L "$R" "$K/luajit" -e '
    package.path = "./?.lua;./?/init.lua;./frontend/?.lua;./frontend/?/init.lua;" .. package.path
    package.cpath = "./libs/?.so;./libs/lib?.so;./?.so;" .. package.cpath
    require("setupkoenv") -- Match genuine KOReader boot: installs ffi.loadlib and correct require paths.
    G_defaults = require("luadefaults"):open()
    local DataStorage = require("datastorage")
    G_reader_settings = require("luasettings"):open(DataStorage:getDataDir() .. "/settings.reader.lua")
    for _, name in ipairs({
      "ffi/posix_h", "ffi/blitbuffer", "ffi/framebuffer_linux",
      "device/generic/device", "device/input", "device/hibyr1/device",
    }) do
      local ok, res = pcall(require, name)
      print((ok and "SIMULATION LOADS: " or "SIMULATION BLOCKED: ") .. name ..
            (ok and "" or (" : " .. tostring(res))))
    end
  '
) 2>&1 | tee -a "$REPORT/experimental-ffi.txt" || true
cmp "$REPORT/original-posix_h.lua" "$K/ffi/posix_h.lua" >/dev/null &&
  echo "WARNING: no diagnostic mutation was applied" | tee -a "$REPORT/experimental-ffi.txt"
cp "$REPORT/original-posix_h.lua" "$K/ffi/posix_h.lua"
echo "Restored original runtime copy. Existing unmodified SD artifact is unchanged." | tee -a "$REPORT/experimental-ffi.txt"
echo "UNVERIFIED: proper MIPS POSIX cdefs. Never treat this simulation as a PASS for flash." | tee -a "$REPORT/experimental-ffi.txt"
