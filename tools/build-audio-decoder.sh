#!/usr/bin/env bash
# Build a compact LGPL-only FFmpeg audio CLI using a verified upstream tarball.
set -euo pipefail
if [[ $# -ne 3 ]]; then echo "Usage: $0 host|mipsel WORK_DIR OUTPUT_DIR" >&2; exit 2; fi
mode=$1
work=$(realpath -m "$2")
out=$(realpath -m "$3")
mkdir -p "$work" "$out"
cd "$work"
version=7.1.5
archive=ffmpeg-$version.tar.xz
url=https://ffmpeg.org/releases/$archive
curl --fail --location --proto '=https' --proto-redir '=https' --retry 3 -o "$archive" "$url"
curl --fail --location --proto '=https' --proto-redir '=https' --retry 3 -o "$archive.asc" "$url.asc"
curl --fail --location --proto '=https' --proto-redir '=https' --retry 3 -o ffmpeg-devel.asc https://ffmpeg.org/ffmpeg-devel.asc
gpg_home=$(mktemp -d "$work/gpg.XXXXXX")
chmod 700 "$gpg_home"
trap 'rm -rf "$gpg_home"' EXIT
gpg --homedir "$gpg_home" --batch --import ffmpeg-devel.asc
gpg --homedir "$gpg_home" --batch --status-fd 1 --verify "$archive.asc" "$archive" > signature.txt
grep -Eq '^\[GNUPG:\] VALIDSIG FCF986EA15E6E293A5644F10B4322F04D67658D8 ' signature.txt
tar -xf "$archive"
cd "ffmpeg-$version"
# No video decoders, encoders, network protocols, devices, hardware acceleration,
# external codec dependencies or nonfree/GPL additions. CLI core filter
# dependencies may still compile tiny video transforms selected by FFmpeg itself.
demux=wav,flac,mp3,aac,mov,ogg,asf,ape,aiff,wv,tta,mpc,mpc8,ac3,eac3,dts,dsf,iff,caf,au,amr,matroska,rm,tak,voc,w64,mlp,truehd,mpeg,oma
decoders=flac,mp3,mp3float,mp2,mp2float,aac,aac_fixed,alac,vorbis,opus,wmav1,wmav2,wmapro,wmalossless,wmavoice,ape,wavpack,tta,mpc7,mpc8,ac3,ac3_fixed,eac3,dca,dsd_lsbf,dsd_msbf,dsd_lsbf_planar,dsd_msbf_planar,amrnb,amrwb,cook,ra_144,ra_288,sipr,tak,mlp,truehd,atrac1,atrac3,atrac3plus,atrac3al,atrac3pal,pcm*,adpcm*
parsers=aac,aac_latm,ac3,dca,flac,mpegaudio,mlp,opus,vorbis
opts=(--disable-everything --disable-autodetect --disable-network --disable-doc --disable-debug
      --disable-avdevice --disable-ffplay --disable-ffprobe --disable-shared --enable-static
      --enable-small --enable-ffmpeg --enable-protocol=file,pipe
      "--enable-demuxer=$demux" "--enable-decoder=$decoders" "--enable-parser=$parsers"
      --enable-encoder=pcm_s16le --enable-muxer=pcm_s16le
      --enable-filter=aresample,aformat,anull,atrim,asetpts --disable-asm)
case "$mode" in
  host) opts+=(--cc=gcc) ;;
  mipsel)
    : "${POCKETOS_CC:?Verified SDK compiler path required}"
    [[ "$POCKETOS_CC" = /* && -x "$POCKETOS_CC" ]]
    prefix="${POCKETOS_CC%gcc}"
    opts+=(--enable-cross-compile --target-os=linux --arch=mips --cpu=mips32r2 "--cross-prefix=$prefix" "--cc=$POCKETOS_CC")
    ;;
  *) exit 2 ;;
esac
# Keep SDK's reference-matched floating point ABI; never force hard/soft-float.
printf '%q ' ./configure "${opts[@]}" > "$out/configure-command.txt"
printf '\n' >> "$out/configure-command.txt"
./configure "${opts[@]}"
make -j"$(nproc)" ffmpeg
cp ffmpeg "$out/pocketos-ffmpeg"
test -s "$out/pocketos-ffmpeg"
test "$(wc -c < "$out/pocketos-ffmpeg")" -le 12582912
cp COPYING.LGPLv2.1 "$out/"
cp ffbuild/config.mak "$out/"
cp "$work/$archive" "$work/$archive.asc" "$work/signature.txt" "$out/"
# Retain exact linkable libraries and compiled CLI objects for LGPL relinking.
mkdir -p "$out/relink"
find . -type f \( -name '*.a' -o -path './fftools/*.o' \) -print0 | tar --null -T - -czf "$out/relink/objects.tar.gz"
(cd "$out" && sha256sum pocketos-ffmpeg > SHA256SUMS && cut -d ' ' -f 1 SHA256SUMS > decoder.sha256)
readelf -h "$out/pocketos-ffmpeg" > "$out/elf.txt"
if [[ "$mode" = mipsel ]]; then grep -q MIPS "$out/elf.txt"; grep -q 'little endian' "$out/elf.txt"; fi
