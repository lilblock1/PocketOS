#!/bin/sh
# PocketOS signed SD package staging only. Never executes packages or writes flash.
set -eu
ui=0
progress(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS PROGRESS %s\n' "$1"; fi; }
selection(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS SELECTION %s %s\n' "$1" "$2"; fi; }
die(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS ERROR %s\n' "$*"; else printf '%s\n' "$*" >&2; fi; exit 1; }
mode=""
if [ "${1:-}" = "--ui" ]; then ui=1; shift; fi
root=/mnt/sd_0
key=/etc/pocketos/update-public.pem
if [ "$#" -ge 1 ] && [ "$1" = "--fixture" ]; then
    [ "$#" -ge 4 ] || die "fixture needs root and public key"
    root=$2; key=$3; shift 3
else
    awk '$2=="/mnt/sd_0" {found=1} END {exit !found}' /proc/mounts || die "microSD not mounted"
fi
if [ "${1:-}" = "--ui" ]; then ui=1; shift; fi
[ "$#" -ge 1 ] || die "Usage: pocketos-update status | sd-inbox | sd DIRECTORY | github TAG | rollback"
mode=$1;shift
[ -d "$root" ] && [ ! -L "$root" ] || die "Unsafe SD root"
cd "$root" || die "Cannot enter SD root"
store="./PocketOS-updates"
[ ! -L "$store" ] || die "Unsafe update directory"
if [ "$mode" = status ]; then
    [ "$#" -eq 0 ] || die "Unexpected arguments"
    if [ -f "$store/selection" ] && [ ! -L "$store/selection" ]; then
        [ "$(wc -c < "$store/selection")" -le 40 ] || die "Invalid selection"
        read -r c p h extra < "$store/selection" || die "Invalid selection"
        case "$c:$p:$h" in *[!0-9:]*) die "Invalid selection";; esac
        [ -z "$extra" ] && [ -n "$c" ] && [ -n "$p" ] && [ -n "$h" ] || die "Invalid selection"
        if [ "$ui" -eq 1 ]; then selection "$c" "$p"; else cat "$store/selection"; fi
    else if [ "$ui" -eq 1 ]; then selection 0 0; else echo "No staged update"; fi; fi
    exit 0
fi
[ -f "$key" ] && [ ! -L "$key" ] || die "Trusted update public key unavailable"
command -v openssl >/dev/null || die "OpenSSL unavailable"
command -v sha256sum >/dev/null || die "SHA256 unavailable"
umask 077
mkdir -p "$store"
[ ! -L "$store/versions" ] || die "Unsafe versions directory"
mkdir -p "$store/versions"
mkdir "$store/.lock" 2>/dev/null || die "Update already running or previous update interrupted"
work=""
cleanup(){ [ -z "$work" ] || rm -rf "$work"; rmdir "$store/.lock" 2>/dev/null || :; }
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
work=$(mktemp -d "$store/.stage.XXXXXX")
valid_seq(){ case "$1" in ''|*[!0-9]*|0*) return 1;; esac; [ "${#1}" -le 9 ]; }
current=0 previous=0 high=0
if [ -e "$store/selection" ] || [ -L "$store/selection" ]; then
    [ -f "$store/selection" ] && [ ! -L "$store/selection" ] || die "Invalid selection"
    [ "$(wc -c < "$store/selection")" -le 40 ] || die "Invalid selection"
    read -r current previous high extra < "$store/selection" || die "Invalid selection"
    [ -z "$extra" ] || die "Invalid selection"
    valid_seq "$current" && valid_seq "$high" || die "Invalid sequence"
    [ "$previous" = 0 ] || valid_seq "$previous" || die "Invalid previous sequence"
    [ "$current" -le "$high" ] && [ "$previous" -le "$high" ] || die "Invalid high-water mark"
fi
select_version(){
    printf '%s %s %s\n' "$1" "$2" "$3" > "$work/selection"
    sync
    mv -f "$work/selection" "$store/selection"
    sync
}
verify_manifest(){
    [ -f "$1" ] && [ ! -L "$1" ] && [ -f "$2" ] && [ ! -L "$2" ] || die "Missing manifest"
    [ "$(wc -c < "$1")" -le 32768 ] && [ "$(wc -c < "$2")" -le 8192 ] || die "Manifest too large"
    openssl dgst -sha256 -verify "$key" -signature "$2" "$1" >/dev/null 2>&1 || die "Signature invalid"
    awk '
      NR==1 {
        if(NF!=4||$1!="POCKETOS-SD-1"||$2!="test"||$3!="r1-mipsel"||$4!~/^[1-9][0-9]*$/||length($4)>9) exit 1;
        next
      }
      {
        if(NF!=3||length($1)!=64||$1!~/^[0-9a-f]+$/||$2!~/^[1-9][0-9]*$/||length($2)>9||$2>268435456||
           $3!~/^(pocketos-|data-)[a-zA-Z0-9][a-zA-Z0-9._-]*$/||length($3)>80||seen[$3]++) exit 1;
        total+=$2;
        if(total>536870912||NR>65) exit 1
      }
      END {if(NR<2)exit 1}
    ' "$1" || die "Invalid signed manifest format"
}
verify_payload(){
    dir=$1
    verify_manifest "$dir/manifest" "$dir/manifest.sig"
    tail -n +2 "$dir/manifest" | while read -r hash size name; do
        [ -f "$dir/$name" ] && [ ! -L "$dir/$name" ] || die "Missing package"
        [ "$(wc -c < "$dir/$name")" -eq "$size" ] || die "Wrong package size"
        actual=$(sha256sum "$dir/$name"); [ "${actual%% *}" = "$hash" ] || die "Package checksum mismatch"
    done
}
progress 2
if [ "$mode" = rollback ]; then
    [ "$#" -eq 0 ] && [ "$previous" != 0 ] || die "No previous staged version"
    [ -d "$store/versions/$previous" ] && [ ! -L "$store/versions/$previous" ] || die "Previous directory unavailable"
    verify_payload "$store/versions/$previous"
    progress 90
    select_version "$previous" "$current" "$high"
    selection "$previous" "$current"
    progress 100
    echo "Previous package selected; no execution or firmware flash."
    exit 0
fi
if [ "$mode" = sd-inbox ]; then
    [ "$#" -eq 0 ] || die "Unexpected inbox arguments"
    mode=sd; set -- ./PocketOS-inbox
fi
[ "$#" -eq 1 ] || die "Expected update folder or GitHub test tag"
case "$mode" in
    sd) source=$1; [ -d "$source" ] && [ ! -L "$source" ] || die "Invalid SD source" ;;
    github)
        tag=$1
        case "$tag" in test-latest|test-[0-9]*) ;; *) die "Only test release tags accepted";; esac
        case "$tag" in *[!a-zA-Z0-9._-]*) die "Invalid tag";; esac
        command -v curl >/dev/null || die "HTTPS downloader unavailable"
        source="https://github.com/lilblock1/PocketOS/releases/download/$tag" ;;
    *) die "Unknown operation";;
esac
fetch(){
    name=$1 limit=$2
    if [ "$mode" = sd ]; then
        [ -f "$source/$name" ] && [ ! -L "$source/$name" ] || die "Missing package file"
        [ "$(wc -c < "$source/$name")" -le "$limit" ] || die "File exceeds limit"
        cp "$source/$name" "$work/$name"
    else
        curl --fail --silent --show-error --location --max-redirs 5 \
            --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 300 \
            --max-filesize "$limit" --output "$work/$name" "$source/$name" || die "GitHub download failed"
    fi
    [ "$(wc -c < "$work/$name")" -le "$limit" ] || die "Downloaded file exceeds limit"
}
fetch manifest 32768
fetch manifest.sig 8192
verify_manifest "$work/manifest" "$work/manifest.sig"
progress 15
read -r magic channel target seq < "$work/manifest"
[ "$seq" -gt "$high" ] || die "Update is not newer than the highest staged version"
[ ! -e "$store/versions/$seq" ] && [ ! -L "$store/versions/$seq" ] || die "Version already exists (inspect interrupted run)"
required=$(awk 'NR>1 {sum+=$2} END {printf "%.0f\n", int((sum+1023)/1024)+16384}' "$work/manifest")
free=$(df -Pk "$store" | awk 'NR==2 {print $4}')
case "$free" in ''|*[!0-9]*) die "Cannot determine SD free space";; esac
[ "$free" -ge "$required" ] || die "Insufficient microSD storage (includes 16 MiB reserve)"
count=$(awk 'END {print NR-1}' "$work/manifest")
tail -n +2 "$work/manifest" | (done_count=0; while read -r hash size name; do
    fetch "$name" "$size"
    [ "$(wc -c < "$work/$name")" -eq "$size" ] || die "Wrong package size"
    actual=$(sha256sum "$work/$name"); [ "${actual%% *}" = "$hash" ] || die "Package checksum mismatch"
    done_count=$((done_count+1)); progress "$((15+75*done_count/count))"
done)
verify_payload "$work"
mkdir "$work/payload"
mv "$work/manifest" "$work/manifest.sig" "$work/payload/"
tail -n +2 "$work/payload/manifest" | while read -r hash size name; do mv "$work/$name" "$work/payload/$name"; done
sync
mv "$work/payload" "$store/versions/$seq"
select_version "$seq" "$current" "$seq"
selection "$seq" "$current"
progress 100
echo "Signed SD update staged and selected: $seq. Application activation and firmware installation remain disabled."
