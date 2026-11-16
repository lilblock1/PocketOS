#!/bin/bash
# Host-only static diagnostic build. Never connects to or writes to an R1.
set -euo pipefail
if [ "$#" -ne 3 ]; then
    echo 'Usage: build-hardware-diagnostics.sh VERIFIED_SDK_DIR SDK_MANIFEST NEW_OUTPUT_DIR' >&2
    exit 2
fi
sdk=$(realpath "$1")
manifest=$(realpath "$2")
output=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/.." && pwd)
test ! -e "$output" || { echo 'Output must be a new directory' >&2; exit 2; }
compiler="$sdk/bin/mipsel-buildroot-linux-gnu-gcc"
test -x "$compiler"
python3 - "$manifest" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
if m.get('schema') != 1 or m.get('flashable') is not False or not m.get('sdk_sha256'):
    raise SystemExit('Requires a verified development SDK manifest')
PY
mkdir -p "$output"
for name in pocketos-info pocketos-r1-runtime; do
    "$compiler" -Os -Wall -Wextra -Werror -static -s "$repo/apps/$name.c" -o "$output/$name"
    readelf -h -A -l -d "$output/$name" > "$output/$name.elf.txt"
    grep -q ELF32 "$output/$name.elf.txt"
    grep -q 'little endian' "$output/$name.elf.txt"
    grep -q MIPS "$output/$name.elf.txt"
    grep -q 'Hard float (double precision)' "$output/$name.elf.txt"
    if grep -Eq 'INTERP|NEEDED' "$output/$name.elf.txt"; then
        echo 'Unexpected dynamic dependency' >&2; exit 1
    fi
done
cp "$manifest" "$output/sdk-manifest.json"
python3 - "$repo" "$output" <<'PY'
import hashlib, json, pathlib, sys
repo, out = map(pathlib.Path, sys.argv[1:])
def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()
names = ('pocketos-info', 'pocketos-r1-runtime')
m = {'phase': '5A', 'flashable': False, 'installation_allowed': False,
     'hardware_verified': False, 'linkage': 'static',
     'purpose': 'Read-only diagnostics under the existing OS; not a boot test',
     'binaries': {n: {'sha256': sha(out/n), 'bytes': (out/n).stat().st_size,
                      'source_sha256': sha(repo/'apps'/(n+'.c'))} for n in names}}
m['shared_hardware_header_sha256'] = sha(repo/'apps/pocketos-hardware.h')
(out/'diagnostics.json').write_text(json.dumps(m, indent=2)+'\n')
(out/'SHA256SUMS').write_text(''.join(f'{sha(out/n)}  {n}\n' for n in names))
PY
cat > "$output/README.txt" <<'EOF'
PocketOS Phase 5A read-only diagnostic candidates. NOT FIRMWARE.
Built with the verified SDK, statically linked to avoid the old host libc.
No UI, playback, autostart, module loader, updater or firmware image included.
No device installation or execution is performed by this build script.
Physical execution remains a separate step after recovery evidence review.
Use only --version (both) or --status (pocketos-r1-runtime) initially.
EOF
echo 'Static MIPS diagnostic candidates prepared on host only.'
