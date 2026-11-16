#!/bin/bash
# Build an isolated read-only userspace test filesystem. Never a boot/OTA image.
set -euo pipefail
[ "$#" -eq 6 ] || { echo 'Usage: build-r1-userspace-test.sh SDK SDK_MANIFEST FOUNDATION_DIR ADB_PACKAGE NEW_WORK NEW_OUTPUT' >&2; exit 2; }
sdk=$(realpath "$1"); manifest=$(realpath "$2"); foundation=$(realpath "$3")
adb=$(realpath "$4"); work=$(realpath -m "$5"); out=$(realpath -m "$6")
repo=$(cd "$(dirname "$0")/.." && pwd)
[ ! -e "$work" ] && [ ! -e "$out" ] || { echo 'Use fresh work/output directories' >&2; exit 2; }
python3 - "$manifest" "$foundation" "$repo" <<'PY'
import hashlib,json,pathlib,sys
m_path,base,repo=map(pathlib.Path,sys.argv[1:])
m=json.loads(m_path.read_text()); f=json.loads((base/'compatibility.json').read_text())
def sha(p):
    with p.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
assert m['schema']==1 and m['flashable'] is False and f['flashable'] is False and f['fp_abi_matches']
assert m['sdk_sha256']==sha(m_path.parent/'pocketos-sdk.tar.gz')
assert m['rootfs_sha256']==sha(base/'PocketOS-rootfs-NOT-FIRMWARE.tar.gz')==f['candidate_tar_gz']['sha256']
assert m['config_sha256']==sha(base/'buildroot.config')
assert m['reference_sha256']==sha(base/'reference.json')==sha(repo/'boards/hiby-r1/reference.json')
print('Verified SDK archive and matching foundation provenance')
PY
mkdir -p "$work/rootfs" "$out"
tar -xzf "$foundation/PocketOS-rootfs-NOT-FIRMWARE.tar.gz" -C "$work/rootfs" --no-same-owner --exclude='./dev/*' --exclude='dev/*'
make -C "$repo/apps" BUILD_DIR="$work/host-apps" check > "$out/host-tests.log" 2>&1
make -C "$repo/apps" CC="$sdk/bin/mipsel-buildroot-linux-gnu-gcc" CFLAGS='-Os -Wall -Wextra -Werror' LDFLAGS=-s BUILD_DIR="$work/target-apps" all > "$out/target-build.log" 2>&1
python3 "$repo/tools/r1_assemble.py" stage "$work/rootfs" "$work/target-apps" "$foundation/compatibility.json" "$out/assembly.json"
python3 "$repo/tools/stage-adbd.py" "$work/rootfs" "$adb" "$out/adb-staging.json"
for name in pocketos-info pocketos-r1-runtime pocketos-shell pocketos-music pocketos-wifi; do
    timeout 30 qemu-mipsel -L "$work/rootfs" "$work/rootfs/usr/$( [ "$name" = pocketos-r1-runtime ] && echo sbin || echo bin)/$name" --version
done > "$out/target-smoke.log"
qemu-mipsel -L "$work/rootfs" "$work/rootfs/usr/bin/adbd" --pocketos-build-info >> "$out/target-smoke.log"
for flag in --self-test --test-power --test-radio --test-update-ui; do
    timeout 30 qemu-mipsel -L "$work/rootfs" "$work/rootfs/usr/bin/pocketos-shell" "$flag" >> "$out/target-smoke.log"
done
# This test filesystem deliberately has no board modules, boot kernel or UI enable marker.
test ! -e "$work/rootfs/etc/pocketos/autostart"
test ! -e "$work/rootfs/module_driver"
test ! -e "$work/rootfs/usr/bin/ffmpeg"
mksquashfs "$work/rootfs" "$out/userspace-test-NOT-FIRMWARE.squashfs" -noappend -all-root -no-xattrs -comp lzo -processors 2 -no-progress > "$out/squashfs.log"
cp "$manifest" "$out/sdk-manifest.json"
python3 - "$repo" "$out" <<'PY'
import hashlib,json,pathlib,sys
repo,out=map(pathlib.Path,sys.argv[1:])
def sha(p):
    with p.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
image=out/'userspace-test-NOT-FIRMWARE.squashfs'
m={'phase':'5C','purpose':'isolated read-only chroot compatibility test',
   'flashable':False,'installation_allowed':False,'bootable':False,
   'firmware_boot_verified':False,'usb_hardware_verified':False,
   'kernel_included':False,'board_modules_included':False,'autostart_enabled':False,
   'compression':'lzo',
   'image':{'name':image.name,'bytes':image.stat().st_size,'sha256':sha(image)},
   'builder_sha256':sha(repo/'tools/build-r1-userspace-test.sh'),
   'adb':json.loads((out/'adb-staging.json').read_text())}
(out/'userspace-test.json').write_text(json.dumps(m,indent=2)+'\n')
(out/'SHA256SUMS').write_text(sha(image)+'  '+image.name+'\n')
PY
echo 'Read-only userspace test filesystem ready. No boot image or device changes.'
