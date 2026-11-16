#!/bin/bash
# Incremental source build of the R1 daemon using the verified foundation SDK.
set -euo pipefail
[ "$#" -eq 4 ] || { echo 'Usage: build-adbd.sh SDK_DIR SDK_MANIFEST NEW_WORK_DIR NEW_OUTPUT_DIR' >&2; exit 2; }
sdk=$(realpath "$1");manifest=$(realpath "$2");work=$(realpath -m "$3");out=$(realpath -m "$4")
repo=$(cd "$(dirname "$0")/.." && pwd)
[ ! -e "$work" ] && [ ! -e "$out" ] || { echo 'Use new work/output directories' >&2; exit 2; }
mkdir -p "$work/dl" "$work/android" "$work/crypt" "$work/staging" "$work/build-adbd" "$out"
python3 - "$repo/tools/adbd-sources.json" "$work/dl" <<'PY'
import base64, concurrent.futures, hashlib, json, pathlib, sys, urllib.request
m=json.load(open(sys.argv[1]));dest=pathlib.Path(sys.argv[2])
def fetch(item):
    urls=[item['source']]
    if item['name'].endswith(('.tar.xz','.tar.gz')):
        package='libxcrypt' if item['name'].startswith('libxcrypt') else 'android-tools'
        urls.append('https://sources.buildroot.net/'+package+'/'+item['name'])
    for url in urls:
        try:
            with urllib.request.urlopen(url,timeout=30) as r: data=r.read()
            if item.get('base64'):data=base64.b64decode(data)
            if hashlib.sha256(data).hexdigest()!=item['sha256']:raise ValueError('digest mismatch')
            (dest/item['name']).write_bytes(data);return
        except Exception as e:error=e
    raise RuntimeError(item['name']+': '+str(error))
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:list(pool.map(fetch,m['inputs']))
PY
tar -xf "$work/dl/android-tools_4.2.2+git20130218.orig.tar.xz" -C "$work/android" --strip-components=1
tar -xf "$work/dl/android-tools_4.2.2+git20130218-3ubuntu41.debian.tar.gz" -C "$work/android"
cd "$work/android"
while read -r change rest; do
    case "$change" in ''|\#*) continue;; esac
    patch -p1 < "debian/patches/$change" >> "$work/patches.log"
done < debian/patches/series
for change in "$work"/dl/*.patch; do patch -p1 < "$change" >> "$work/patches.log"; done
python3 "$repo/tools/prepare-adbd.py" "$work/android" "$work/dl/aosp-5.1.1-usb_linux_client.c"
tar -xf "$work/dl/libxcrypt-4.4.38.tar.xz" -C "$work/crypt" --strip-components=1
cc="$sdk/bin/mipsel-buildroot-linux-gnu-gcc"
cd "$work/crypt"
CC="$cc" AR="$sdk/bin/mipsel-buildroot-linux-gnu-ar" RANLIB="$sdk/bin/mipsel-buildroot-linux-gnu-ranlib" \
  ./configure --host=mipsel-buildroot-linux-gnu --prefix="$work/staging" --disable-werror --disable-obsolete-api > "$work/crypt-configure.log" 2>&1
make -j2 > "$work/crypt-build.log" 2>&1
make install > "$work/crypt-install.log" 2>&1
export PATH="$sdk/bin:$PATH"
export PKG_CONFIG_SYSROOT_DIR="$sdk/mipsel-buildroot-linux-gnu/sysroot"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig:$PKG_CONFIG_SYSROOT_DIR/usr/share/pkgconfig"
make -j2 -C "$work/build-adbd" -f "$work/android/debian/makefiles/adbd.mk" SRCDIR="$work/android" CC="$cc" \
  CFLAGS="-Os -I$work/staging/include -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types" \
  LDFLAGS="-L$work/staging/lib -Wl,-rpath-link,$work/staging/lib" > "$work/adbd-build.log" 2>&1
"$sdk/bin/mipsel-buildroot-linux-gnu-strip" "$work/build-adbd/adbd"
cp "$work/build-adbd/adbd" "$out/adbd"
cp "$work/staging/lib/libcrypt.so.2.0.0" "$out/"
cp "$repo/hardware/hiby-r1/pocketos-usb-adb.sh" "$out/pocketos-usb-adb"
cp "$manifest" "$out/sdk-manifest.json"
cp "$work/android/debian/copyright" "$out/android-tools-copyright.txt"
cp "$work/crypt/LICENSING" "$work/crypt/COPYING.LIB" "$out/"
readelf -h -A -d "$out/adbd" > "$out/adbd-elf.txt"
python3 - "$repo" "$work" "$out" <<'PY'
import hashlib,json,pathlib,sys
repo,work,out=map(pathlib.Path,sys.argv[1:])
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
names=('adbd','libcrypt.so.2.0.0','pocketos-usb-adb')
m={'phase':'5C','flashable':False,'installation_allowed':False,'tcp_fallback':False,
   'test_binary_included':False,'usb_hardware_verified':False,
   'files':{n:{'sha256':sha(out/n),'bytes':(out/n).stat().st_size} for n in names},
   'source_manifest_sha256':sha(repo/'tools/adbd-sources.json'),
   'adapter_sha256':sha(repo/'tools/prepare-adbd.py'),
   'dependencies':['libc.so.6','ld.so.1','libz.so.1','libcrypto.so.3','libcrypt.so.2']}
(out/'adb-manifest.json').write_text(json.dumps(m,indent=2)+'\n')
(out/'SHA256SUMS').write_text(''.join(f'{sha(out/n)}  {n}\n' for n in names))
PY
echo 'FunctionFS USB-only MIPS adbd package built; no Linux rebuild or device changes.'
