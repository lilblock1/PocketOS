#!/bin/sh
# Verified optional decoder snapshot. No flash writes, no executable from SD.
set -eu
die(){ printf '%s\n' "$*" >&2; exit 1; }
limit=12582912
snapshot(){
    input=$1 digest=$2 output=$3
    [ -f "$input" ] && [ ! -L "$input" ] || die "Decoder missing or unsafe"
    [ -f "$digest" ] && [ ! -L "$digest" ] || die "Trusted digest unavailable"
    [ "$(wc -c < "$digest")" -eq 65 ] || die "Invalid digest size"
    read -r expected < "$digest" || die "Invalid digest"
    case "$expected" in *[!0-9a-f]*) die "Invalid digest";; esac
    [ "${#expected}" -eq 64 ] || die "Invalid digest"
    size=$(wc -c < "$input")
    [ "$size" -gt 0 ] && [ "$size" -le "$limit" ] || die "Decoder exceeds RAM cache limit"
    # Copy at most limit + one block even if a mutable source unexpectedly grows.
    dd if="$input" of="$output" bs=4096 count=3073 2>/dev/null
    [ "$(wc -c < "$output")" -eq "$size" ] || die "Decoder changed during copy"
    actual=$(sha256sum "$output")
    [ "${actual%% *}" = "$expected" ] || die "Decoder checksum mismatch"
}
umask 077
if [ "${1:-}" = "--verify-fixture" ]; then
    [ "$#" -eq 3 ] || die "Fixture needs decoder and digest"
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    snapshot "$2" "$3" "$work/copy"
    echo "Fixture verified; not activated"
    exit 0
fi
[ "$#" -eq 1 ] || die "Usage: pocketos-decoder status | activate | ensure"
cache=/run/pocketos-decoder
if [ "$1" = status ]; then
    if [ -x "$cache/ffmpeg" ] && [ ! -L "$cache/ffmpeg" ]; then echo "RAM decoder cache present"
    elif [ -x /usr/bin/ffmpeg ]; then echo "System decoder present"
    else echo "Optional decoder inactive"; fi
    exit 0
fi
case "$1" in activate|ensure) ;; *) die "Unknown action";; esac
[ "$(id -u)" -eq 0 ] || die "Activation requires trusted system service"
# Never silently stage into internal flash or fall back to an unmounted SD folder.
awk '$2=="/run" && $3=="tmpfs" {ok=1} END {exit !ok}' /proc/mounts || die "/run RAM filesystem not mounted"
awk '$2=="/mnt/sd_0" {ok=1} END {exit !ok}' /proc/mounts || die "microSD not mounted"
[ ! -L /run ] && [ ! -L /mnt/sd_0 ] && [ ! -L /mnt/sd_0/PocketOS-codecs ] || die "Unsafe directory"
[ "$(/usr/bin/pocketos-stat -c '%u:%a' /run)" = "0:755" ] || die "Unsafe RAM parent"
digest=/etc/pocketos/decoder.sha256
[ -f "$digest" ] && [ ! -L "$digest" ] || die "Trusted digest missing"
case "$(/usr/bin/pocketos-stat -c '%u:%a' "$digest")" in 0:444|0:400|0:644|0:600) ;; *) die "Unsafe trusted digest";; esac
[ ! -L "$cache" ] || die "Unsafe cache"
mkdir -p "$cache"
[ "$(/usr/bin/pocketos-stat -c '%u:%a' "$cache")" = "0:700" ] || die "Unsafe cache owner"
# Reuse an exact protected snapshot without allocating another binary.
if [ "$1" = ensure ] && [ -f "$cache/ffmpeg" ] && [ ! -L "$cache/ffmpeg" ] &&
   [ "$(/usr/bin/pocketos-stat -c '%u:%a' "$cache/ffmpeg")" = "0:500" ] &&
   [ "$(wc -c < "$digest")" -eq 65 ]; then
    read -r expected < "$digest" || die "Invalid digest"
    case "$expected" in *[!0-9a-f]*) die "Invalid digest";; esac
    [ "${#expected}" -eq 64 ] || die "Invalid digest"
    size=$(wc -c < "$cache/ffmpeg")
    if [ "$size" -gt 0 ] && [ "$size" -le "$limit" ]; then
        actual=$(sha256sum "$cache/ffmpeg")
        if [ "${actual%% *}" = "$expected" ]; then
            echo "Verified decoder already cached in RAM"
            exit 0
        fi
    fi
fi
# Bounded cache and leave headroom for audio process memory.
size=$(wc -c < /mnt/sd_0/PocketOS-codecs/pocketos-ffmpeg)
[ "$size" -gt 0 ] && [ "$size" -le "$limit" ] || die "Decoder size invalid"
free=$(awk '$1=="MemAvailable:"{print $2}' /proc/meminfo)
case "$free" in ''|*[!0-9]*) die "Available RAM unknown";; esac
[ "$free" -ge "$(( (size+1023)/1024+16384 ))" ] || die "Insufficient safe RAM headroom"
work=$(mktemp -d "$cache/.stage.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
snapshot /mnt/sd_0/PocketOS-codecs/pocketos-ffmpeg "$digest" "$work/ffmpeg"
chmod 500 "$work/ffmpeg"
# The previously tested ABI is not a guarantee for every future candidate.
# The physical rootfs does not ship timeout. This exact digest-checked
# executable can be checked with a CPU and output limit instead.
if ! (ulimit -t 5; ulimit -f 64; LC_ALL=C "$work/ffmpeg" -version > "$work/version.txt" 2>&1); then
    die "Decoder runtime check failed"
fi
grep -q '^ffmpeg version ' "$work/version.txt" || die "Unexpected decoder version output"
mv -f "$work/ffmpeg" "$cache/ffmpeg"
echo "Verified decoder cached in RAM; no internal storage written"
