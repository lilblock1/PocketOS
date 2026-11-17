#!/bin/bash
# Run from an extracted trusted kit under Debian WSL. Builds only; never flashes.
set -euo pipefail
umask 077
test "$#" -eq 2 || test "$#" -eq 3 || { echo "Usage: bash finalize-r1-bootstrap.sh PREFLIGHT_LOG PINNED_PREVIEW_UPT [SEQUENCE]"; exit 2; }
seq=${3:-1}
[[ "$seq" =~ ^[1-9][0-9]{0,8}$ ]] || { echo "Invalid sequence"; exit 2; }
preflight=$(realpath -e "$1")
preview=$(realpath -e "$2")
cd "$(dirname "$0")"
test "$EUID" -ne 0 || { echo "Run as normal user (sudo is used for the candidate tree)"; exit 1; }
command -v openssl >/dev/null
command -v genisoimage >/dev/null
command -v mksquashfs >/dev/null
command -v unsquashfs >/dev/null
sha256sum -c SHA256SUMS
# Require the explicit physical read-only gate before building an update-enabled candidate.
test -f "$preflight" && test -f "$preview"
tr -d '\r' < "$preflight" | grep -Fxq POCKETOS-R1-RECOVERY-PREFLIGHT-OK
echo "fa36d95736555dba9ce95e33a29227668270d524f1cb419a751506f59d10a074  $preview" | sha256sum -c -
command -v 7z >/dev/null
work=$(mktemp -d "$HOME/pocketos-bootstrap.XXXXXX")
sudo python3 tools/prepare-r1-updater-bootstrap.py "$preview" target-apps "$work/root"
keydir="$HOME/.local/share/pocketos-only-signing"
mkdir -p -m 700 "$keydir"
test ! -L "$keydir"
chmod 700 "$keydir"
key="$keydir/release-private.pem"
if [ ! -e "$key" ]; then
    [ "$seq" -eq 1 ] || { echo "Existing signing key required for later updates"; exit 1; }
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "$key"
fi
test -f "$key" && test ! -L "$key"
chmod 600 "$key"
openssl pkey -in "$key" -pubout -out "$work/public.pem"
sudo python3 tools/provision-r1-update-trust.py "$work/root" "$work/public.pem" "$seq"
# Offline candidate only, based on successful read-only physical preflight.
sudo install -m 0400 -o root -g root /dev/null "$work/root/etc/pocketos/recovery-enable"
printf 'POCKETOS-R1-RECOVERY-1\n' | sudo tee "$work/root/etc/pocketos/recovery-enable" >/dev/null
build_id=$(openssl rand -hex 8)
sudo sed -i "s/^BUILD_ID=.*/BUILD_ID=$build_id/" "$work/root/etc/pocketos-release"
sudo mksquashfs "$work/root" "$work/rootfs.squashfs" -noappend -comp lzo -processors 2 -no-progress
sudo chown "$(id -u):$(id -g)" "$work/rootfs.squashfs"
python3 - "$work/rootfs.squashfs" "$work/candidate.json" "$build_id" <<'PY'
from pathlib import Path
import sys, hashlib, json
image,report=map(Path,sys.argv[1:3])
if not 0 < image.stat().st_size <= 16*1024*1024:
    raise SystemExit("Bootstrap exceeds conservative 16 MiB rootfs budget")
report.write_text(json.dumps({"image":{"sha256":hashlib.sha256(image.read_bytes()).hexdigest()},"primary_target":"/dev/mtd2 (rootfs)","kernel_reused":True,"build_id":sys.argv[3]}))
PY
python3 tools/pack-r1-firmware-update.py --rootfs "$work/rootfs.squashfs" --candidate "$work/candidate.json" --key "$key" --channel test --sequence "$seq" --output "$work/signed"
cp "$work/signed/pocketos-r1.upt" "$work/r1.upt"
sha256sum "$work/r1.upt"
echo "Candidate: $work/r1.upt"
echo "Signed assets: $work/signed"
echo "Build ID: $build_id (cold boot not verified)"
echo "Private signing key retained locally. Do not upload it."
