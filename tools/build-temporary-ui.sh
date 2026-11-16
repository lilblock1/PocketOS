#!/bin/bash
# Host-only build. The device handoff is separate and RAM-only.
set -euo pipefail
[ "$#" -eq 4 ] || { echo 'Usage: build-temporary-ui.sh SDK_DIR SDK_MANIFEST GENERATED_FONT_DIR NEW_OUTPUT_DIR' >&2; exit 2; }
sdk=$(realpath "$1");manifest=$(realpath "$2");font=$(realpath "$3");out=$(realpath -m "$4")
repo=$(cd "$(dirname "$0")/.." && pwd)
test -f "$font/pocketos-reader-font.h"
bash "$repo/tools/build-hardware-diagnostics.sh" "$sdk" "$manifest" "$out"
cc="$sdk/bin/mipsel-buildroot-linux-gnu-gcc"
"$cc" -Os -Wall -Wextra -Werror -static -s -I"$font" "$repo/apps/pocketos-shell.c" -o "$out/pocketos-shell"
"$cc" -Os -Wall -Wextra -Werror -static -s "$repo/tools/r1-display.c" -o "$out/r1-display"
cp "$repo/tools/r1-ui-session.sh" "$out/session.sh"
chmod 500 "$out/session.sh"
for name in pocketos-shell r1-display; do
    readelf -h -A -l -d "$out/$name" > "$out/$name.elf.txt"
    grep -q 'Hard float (double precision)' "$out/$name.elf.txt"
    if grep -Eq 'INTERP|NEEDED' "$out/$name.elf.txt"; then exit 1; fi
done
python3 - "$repo" "$font" "$out" <<'PY'
import hashlib, json, pathlib, sys
repo, font, out = map(pathlib.Path, sys.argv[1:])
def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()
names = ('pocketos-info', 'pocketos-r1-runtime', 'pocketos-shell', 'r1-display', 'session.sh')
sources = ('apps/pocketos-shell.c', 'apps/pocketos-hardware.h', 'apps/pocketos-touch.h',
           'apps/pocketos-power.h', 'apps/pocketos-files.h', 'apps/pocketos-radio-ui.h',
           'apps/pocketos-update-ui.h', 'apps/pocketos-audio-formats.h',
           'tools/r1-display.c', 'tools/r1-ui-session.sh')
m = {'phase': '5B', 'flashable': False, 'installation_allowed': False,
     'firmware_boot_verified': False, 'linkage': 'static',
     'canvas': [480, 800], 'target_display': [480, 800],
     'binaries': {n: {'sha256': sha(out/n), 'bytes': (out/n).stat().st_size} for n in names},
     'sources': {n: sha(repo/n) for n in sources},
     'generated_font_sha256': sha(font/'pocketos-reader-font.h')}
(out/'temporary-ui.json').write_text(json.dumps(m, indent=2)+'\n')
(out/'SHA256SUMS').write_text(''.join(f'{sha(out/n)}  {n}\n' for n in names))
PY
echo 'Static temporary UI prepared. No device connection or firmware image created.'
cat > "$out/README.txt" <<'EOF'
PocketOS temporary UI and diagnostics. NOT FIRMWARE.
Static MIPS programs for RAM-only tests under the existing OS.
The session script preserves the original framebuffer and resumes Sonix on
exit, signal or timeout. It must only be used after board/USB identity review.
No bootloader, recovery, internal storage or microSD writes are performed.
Display pixels, temporary backlight state, and temporary input ownership change
during the session. No updater, radio, file-write or audio flags are enabled.
EOF
