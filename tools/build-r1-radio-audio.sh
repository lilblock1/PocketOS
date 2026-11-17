#!/bin/sh
# Incremental SDK build, independent of the Linux/kernel build.
set -eu
[ "$#" -eq 3 ] || { echo 'Usage: build-r1-radio-audio.sh SDK WORK NEW_OUTPUT' >&2; exit 2; }
sdk=$(realpath "$1"); work=$(realpath -m "$2"); out=$(realpath -m "$3")
[ ! -e "$out" ] || { echo 'Use a new output directory' >&2; exit 1; }
mkdir -p "$work" "$out/bin" "$out/lib/alsa-lib" "$out/config" "$out/source"
export PATH="$sdk/bin:$PATH"
export CC="$sdk/bin/mipsel-buildroot-linux-gnu-gcc"
export PKG_CONFIG="$sdk/bin/pkg-config"
export PKG_CONFIG_SYSROOT_DIR="$sdk/mipsel-buildroot-linux-gnu/sysroot"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig"
cd "$work"
fetch(){
    name=$1 hash=$2 url=$3
    [ -f "$name" ] || curl --fail --location --max-time 120 --output "$name" "$url"
    printf '%s  %s\n' "$hash" "$name" | sha256sum -c -
    cp "$name" "$out/source/"
}
fetch sbc-2.0.tar.xz 8f12368e1dbbf55e14536520473cfb338c84b392939cc9b64298360fd4a07992 https://www.kernel.org/pub/linux/bluetooth/sbc-2.0.tar.xz
fetch bluez-alsa-v4.3.1.tar.gz 933fe898dfac21fdfeb5f4ffa685c2aa2db9c064d639170ac2652f156e956a2a https://codeload.github.com/arkq/bluez-alsa/tar.gz/refs/tags/v4.3.1
fetch bluez-5.79.tar.xz 4164a5303a9f71c70f48c03ff60be34231b568d93a9ad5e79928d34e6aa0ea8a https://www.kernel.org/pub/linux/bluetooth/bluez-5.79.tar.xz
[ -d sbc-2.0 ] || tar xf sbc-2.0.tar.xz
[ -d bluez-alsa-4.3.1 ] || tar xf bluez-alsa-v4.3.1.tar.gz
[ -d bluez-5.79 ] || tar xf bluez-5.79.tar.xz
cd sbc-2.0
./configure --host=mipsel-buildroot-linux-gnu --prefix=/usr --disable-tools --disable-tester --disable-static
make -j4
make DESTDIR="$work/stage" install
cd ../bluez-alsa-4.3.1
autoreconf -fi
export PKG_CONFIG_LIBDIR="$work/stage/usr/lib/pkgconfig:$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig"
CPPFLAGS="-I$work/stage/usr/include" LDFLAGS="-L$work/stage/usr/lib -Wl,-rpath-link,$work/stage/usr/lib" \
    ./configure --host=mipsel-buildroot-linux-gnu --prefix=/usr --sysconfdir=/etc \
    --localstatedir=/run/pocketos-bluetooth --disable-aplay --enable-cli \
    --disable-manpages --disable-aac --disable-msbc --with-bluealsauser=root
make -j4
make DESTDIR="$work/stage" install
cp "$work/stage/usr/bin/bluealsa" "$work/stage/usr/bin/bluealsa-cli" "$out/bin/"
cp "$work/stage/usr/lib/libsbc.so.1.3.1" "$out/lib/libsbc.so.1"
plugin=$(find "$work/stage" -name libasound_module_pcm_bluealsa.so -type f -print -quit)
[ -n "$plugin" ]; cp "$plugin" "$out/lib/alsa-lib/"
cp "$work/stage/usr/share/dbus-1/system.d/bluealsa.conf" "$out/config/"
cp "$work/stage/etc/alsa/conf.d/20-bluealsa.conf" "$out/config/"
cd ../bluez-5.79
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_SYSROOT_DIR/usr/lib/pkgconfig"
# Keep BAP/BASS linked together: BlueZ 5.79 has unconditional cross references.
# Experimental LE profiles are not enabled at runtime on the R1's old kernel.
./configure --host=mipsel-buildroot-linux-gnu --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
    --disable-client --disable-tools --disable-monitor --disable-udev --disable-cups \
    --disable-obex --disable-systemd --disable-manpages --disable-mcp --disable-ccp --disable-vcp --disable-csip
make src/builtin.h
make -j4 src/bluetoothd
cp src/bluetoothd "$out/bin/"
for file in "$out/bin/"* "$out/lib/alsa-lib/"*.so; do "$sdk/bin/patchelf" --remove-rpath "$file"; done
for file in "$out/bin/"* "$out/lib/"*.so* "$out/lib/alsa-lib/"*.so; do "$sdk/bin/mipsel-buildroot-linux-gnu-strip" "$file"; done
cp "$work/sbc-2.0/COPYING" "$out/source/sbc-COPYING"
cp "$work/bluez-5.79/COPYING" "$out/source/bluez-COPYING"
cp "$work/bluez-alsa-4.3.1/LICENSE" "$out/source/bluealsa-LICENSE"
cd "$out"
find bin lib config -type f -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
echo 'Radio audio components built with SDK; no device or firmware writes.'
