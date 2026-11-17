#!/bin/sh
# PocketOS signed SD package staging only. Never executes packages or writes flash.
set -eu
ui=0
progress(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS PROGRESS %s\n' "$1"; fi; }
phase(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS PHASE %s\n' "$1"; fi; }
bytes(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS BYTES %s %s\n' "$1" "$2"; fi; }
selection(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS SELECTION %s %s\n' "$1" "$2"; fi; }
die(){ if [ "$ui" -eq 1 ]; then printf 'POCKETOS ERROR %s\n' "$*"; else printf '%s\n' "$*" >&2; fi; exit 1; }
mode=""
if [ "${1:-}" = "--ui" ]; then ui=1; shift; fi
root=/mnt/sd_0
fixture=0
key=/etc/pocketos/update-public.pem
stat_cmd=/usr/bin/pocketos-stat
if [ "$#" -ge 1 ] && [ "$1" = "--fixture" ]; then
    [ "$#" -ge 4 ] || die "fixture needs root and public key"
    fixture=1; root=$2; key=$3; stat_cmd=stat; shift 3
else
    awk '$2=="/mnt/sd_0" {found=1} END {exit !found}' /proc/mounts || die "microSD not mounted"
fi
if [ "${1:-}" = "--ui" ]; then ui=1; shift; fi
[ "$#" -ge 1 ] || die "Usage: pocketos-update status | sd-inbox | sd DIRECTORY | check-github TAG | github TAG [SEQUENCE] | rollback"
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
if [ "$fixture" -eq 0 ]; then
    approval=/etc/pocketos/update-enable
    [ "$(id -u)" -eq 0 ] || die "Trusted update service requires root"
    [ ! -L /etc/pocketos ] && [ "$("$stat_cmd" -c '%u:%a' /etc/pocketos)" = 0:755 ] ||
        die "Unsafe update configuration"
    [ -f "$approval" ] && [ ! -L "$approval" ] &&
        [ "$(wc -c < "$approval")" -eq 22 ] &&
        grep -Fxq POCKETOS-R1-UPDATES-1 "$approval" || die "Signed update staging locked"
    case "$("$stat_cmd" -c '%u:%a' "$approval")" in 0:400|0:600) ;; *) die "Untrusted update approval";; esac
    case "$("$stat_cmd" -c '%u:%a' "$key" 2>/dev/null)" in 0:400|0:444) ;; *) die "Untrusted release key";; esac
    awk '$1=="/dev/mmcblk0p1" && $2=="/mnt/sd_0" && $3=="vfat" {
        n=split($4,a,","); for(i=1;i<=n;i++) opts[a[i]]=1;
        if(opts["rw"]&&opts["nosuid"]&&opts["nodev"]&&opts["noexec"]) ok=1
    } END {exit !ok}' /proc/mounts || die "Writable protected microSD required"
fi
[ -f "$key" ] && [ ! -L "$key" ] || die "Trusted update public key unavailable"
command -v openssl >/dev/null || die "OpenSSL unavailable"
openssl pkey -pubin -in "$key" -noout >/dev/null 2>&1 || die "Invalid release public key"
command -v sha256sum >/dev/null || die "SHA256 unavailable"
umask 077
mkdir -p "$store"
[ ! -L "$store/versions" ] || die "Unsafe versions directory"
mkdir -p "$store/versions"
mkdir "$store/.lock" 2>/dev/null || die "Update already running or previous update interrupted"
work="" transfer_pid=""
cleanup(){
    if [ -n "$transfer_pid" ]; then kill "$transfer_pid" 2>/dev/null || :; wait "$transfer_pid" 2>/dev/null || :; fi
    [ -z "$work" ] || rm -rf "$work"
    rmdir "$store/.lock" 2>/dev/null || :
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
work=$(mktemp -d "$store/.stage.XXXXXX")
valid_seq(){ case "$1" in ''|*[!0-9]*|0*) return 1;; esac; [ "${#1}" -le 9 ]; }
current=0 previous=0 high=0 installed=0
if [ "$fixture" -eq 0 ]; then
    installed=$(sed -n 's/^UPDATE_SEQUENCE=//p' /etc/pocketos-release)
    valid_seq "$installed" || die "Installed firmware sequence unavailable"
fi
if [ -e "$store/selection" ] || [ -L "$store/selection" ]; then
    [ -f "$store/selection" ] && [ ! -L "$store/selection" ] || die "Invalid selection"
    [ "$(wc -c < "$store/selection")" -le 40 ] || die "Invalid selection"
    read -r current previous high extra < "$store/selection" || die "Invalid selection"
    [ -z "$extra" ] || die "Invalid selection"
    valid_seq "$current" && valid_seq "$high" || die "Invalid sequence"
    [ "$previous" = 0 ] || valid_seq "$previous" || die "Invalid previous sequence"
    [ "$current" -le "$high" ] && [ "$previous" -le "$high" ] || die "Invalid high-water mark"
fi
floor=$high
[ "$installed" -le "$floor" ] || floor=$installed
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
    awk -v fixture="$fixture" '
      NR==1 {
        if(NF!=4||($1!="POCKETOS-R1-FW-1"&&(fixture!=1||$1!="POCKETOS-SD-1"))||($2!="test"&&$2!="stable")||$3!="r1-mipsel"||$4!~/^[1-9][0-9]*$/||length($4)>9) exit 1;
        firmware=($1=="POCKETOS-R1-FW-1");
        next
      }
      {
        if(NF!=3||length($1)!=64||$1!~/^[0-9a-f]+$/||$2!~/^[1-9][0-9]*$/||length($2)>9||$2>268435456||
           $3!~/^(pocketos-|data-)[a-zA-Z0-9][a-zA-Z0-9._-]*$/||length($3)>80||seen[$3]++) exit 1;
        if(firmware){
            if($3=="pocketos-r1.upt"){if($2>50331648||$2%2048!=0)exit 1; upt=1}
            else if($3=="data-r1-report.json"){if($2>32768)exit 1; report=1}
            else exit 1
        }
        total+=$2;
        if(total>536870912||NR>65) exit 1
      }
      END {if(NR<2||(firmware&&(NR!=3||!upt||!report)))exit 1}
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
phase MANIFEST
progress 2
if [ "$mode" = install ]; then
    [ "$#" -eq 0 ] || die "Unexpected install arguments"
    [ "$fixture" -eq 0 ] || die "Recovery writes are disabled in fixtures"
    marker=/etc/pocketos/recovery-enable
    [ -f "$marker" ] && [ ! -L "$marker" ] &&
        [ "$("$stat_cmd" -c '%u:%a' "$marker")" = 0:400 ] &&
        grep -Fxq POCKETOS-R1-RECOVERY-1 "$marker" || die "Recovery installation not provisioned"
    [ "$(uname -r)" = 4.4.94-sonix ] || die "Wrong kernel"
    grep -Eq '(^| )root=/dev/mtdblock_bbt_ro2( |$)' /proc/cmdline || die "Not primary system"
    for entry in 'mtd1: 00500000 00020000 "kernel"' 'mtd2: 02d00000 00020000 "rootfs"' 'mtd3: 00500000 00020000 "kernel2"' 'mtd4: 01800000 00020000 "rootfs2"' 'mtd5: 00080000 00020000 "ota"'; do
        grep -Fxq "$entry" /proc/mtd || die "Unexpected partition layout"
    done
    battery_ok(){
        level=$(cat /sys/class/power_supply/battery/capacity 2>/dev/null) || return 1
        case "$level" in ''|*[!0-9]*) return 1;; esac
        [ "$level" -ge 50 ] && [ "$level" -le 100 ]
    }
    battery_ok || die "Charge battery to at least 50%"
    [ "$current" -gt "$installed" ] && [ "$current" -eq "$high" ] || die "No newer selected firmware"
    version_dir="$store/versions/$current"
    [ -d "$version_dir" ] && [ ! -L "$version_dir" ] || die "Selected firmware unavailable"
    phase VERIFY
    verify_payload "$version_dir"
    read -r magic channel target seq < "$version_dir/manifest"
    [ "$magic" = POCKETOS-R1-FW-1 ] && [ "$seq" = "$current" ] || die "Selected firmware identity mismatch"
    # Match the preserved physical recovery backups, not just partition labels.
    for spec in '8604858bd975e272f5e238e1721656adbcd9e67eace049d096d049eff578a364 /dev/mtd1ro' 'ebcb4491fd67c5d479d4a7efccf0a7d5871f140634890b33ca1aeb83418aa297 /dev/mtd3ro' 'd4059f7cade350bfe0f53221ba2e886b8e45d6d625f7042bdd8cd4c39c149358 /dev/mtd4ro'; do
        set -- $spec
        [ -c "$2" ] || die "Recovery identity unavailable"
        actual=$(sha256sum "$2") || die "Recovery readback failed"
        [ "${actual%% *}" = "$1" ] || die "Recovery identity differs"
    done
    /usr/sbin/pocketos-recovery-arm --check || die "Boot-selection preflight failed"
    expected=$(awk '$3=="pocketos-r1.upt" {print $1}' "$version_dir/manifest")
    # Prior manual firmware may still be on the card. Keep it intact in a
    # unique backup directory; do not require card removal or overwrite it.
    needs_copy=1
    old_digest=""
    if [ -e ./r1.upt ] || [ -L ./r1.upt ]; then
        [ -f ./r1.upt ] && [ ! -L ./r1.upt ] || die "Unsafe existing recovery file"
        actual=$(sha256sum ./r1.upt) || die "Cannot read existing recovery file"
        old_digest=${actual%% *}
        [ "$old_digest" != "$expected" ] || needs_copy=0
    fi
    if [ "$needs_copy" -eq 1 ]; then
        needed=$(awk '$3=="pocketos-r1.upt" {printf "%d",int(($2+1023)/1024)+16384}' "$version_dir/manifest")
        free=$(df -Pk . | awk 'NR==2 {print $4}')
        case "$free" in ''|*[!0-9]*) die "Cannot determine SD free space";; esac
        [ "$free" -ge "$needed" ] || die "Not enough SD space for recovery copy"
        phase COPY
        cp "$version_dir/pocketos-r1.upt" "$work/r1.upt"
        actual=$(sha256sum "$work/r1.upt")
        [ "${actual%% *}" = "$expected" ] || die "Recovery copy checksum mismatch"
        sync
        if [ -n "$old_digest" ]; then
            actual=$(sha256sum ./r1.upt) || die "Existing recovery file changed"
            [ "${actual%% *}" = "$old_digest" ] || die "Existing recovery file changed"
            backup=$(mktemp -d "$store/recovery-backup.XXXXXX") || die "Cannot preserve old recovery file"
            mv ./r1.upt "$backup/r1.upt" || die "Cannot preserve old recovery file"
            sync
        fi
        [ ! -e ./r1.upt ] && [ ! -L ./r1.upt ] || die "Recovery file appeared"
        mv "$work/r1.upt" ./r1.upt || die "Cannot stage verified recovery file"
    fi
    actual=$(sha256sum ./r1.upt) || die "Cannot recheck recovery file"
    [ "${actual%% *}" = "$expected" ] || die "Final recovery checksum mismatch"
    sync
    battery_ok || die "Battery check failed before recovery"
    # Release the staging directory and working directory before the clean unmount.
    cleanup
    work=""
    trap - EXIT HUP INT TERM
    cd /
    umount /mnt/sd_0 || die "microSD busy; close playback/files and retry"
    phase RECOVERY
    progress 95
    # Do not allow the UI to kill the child during the erase/write/readback.
    trap '' HUP INT TERM
    if ! /usr/sbin/pocketos-recovery-arm --arm; then
        die "Recovery selection failed; do not reboot"
    fi
    sync
    /sbin/reboot || die "Recovery armed; automatic reboot failed"
    # Reboot may return before shutdown; never label this as installed.
    while :; do sleep 1; done
fi
if [ "$mode" = rollback ]; then
    [ "$#" -eq 0 ] && [ "$previous" != 0 ] || die "No previous staged version"
    [ -d "$store/versions/$previous" ] && [ ! -L "$store/versions/$previous" ] || die "Previous directory unavailable"
    phase VERIFY
    verify_payload "$store/versions/$previous"
    phase SAVE
    progress 95
    select_version "$previous" "$current" "$high"
    selection "$previous" "$current"
    phase DONE
    progress 100
    echo "Previous package selected; no execution or firmware flash."
    exit 0
fi
if [ "$mode" = sd-inbox ]; then
    [ "$#" -eq 0 ] || die "Unexpected inbox arguments"
    mode=sd; set -- ./PocketOS-inbox
fi
check_only=0
if [ "$mode" = check-github ]; then check_only=1; mode=github; fi
expected_channel="" expected_seq=""
if [ "$mode" = github ] && [ "$check_only" -eq 0 ] && [ "$#" -eq 2 ]; then
    expected_seq=$2
    valid_seq "$expected_seq" || die "Invalid confirmed sequence"
    set -- "$1"
fi
[ "$#" -eq 1 ] || die "Expected update folder or GitHub channel"
case "$mode" in
    sd) source=$1; [ -d "$source" ] && [ ! -L "$source" ] || die "Invalid SD source" ;;
    github)
        tag=$1
        command -v curl >/dev/null || die "HTTPS downloader unavailable"
        case "$tag" in
            stable-latest)
                expected_channel=stable
                source="https://github.com/lilblock1/PocketOS/releases/latest/download" ;;
            test-latest|test-[0-9]*)
                case "$tag" in *[!a-zA-Z0-9._-]*) die "Invalid tag";; esac
                expected_channel=test
                source="https://github.com/lilblock1/PocketOS/releases/download/$tag" ;;
            *) die "Invalid update channel" ;;
        esac ;;
    *) die "Unknown operation";;
esac
# Payload progress is based on signed byte counts, not number of files.
# Only report bytes observed in the private staging file; never trust HTTP length.
report_transfer(){
    partial=$("$stat_cmd" -c %s "$work/$transfer_name" 2>/dev/null) || partial=0
    case "$partial" in ''|*[!0-9]*) partial=0;; esac
    [ "$partial" -le "$transfer_limit" ] || partial=$transfer_limit
    observed=$((transferred+partial))
    if [ "$observed" -ne "$last_report" ]; then
        bytes "$observed" "$total"
        progress "$(awk -v n="$observed" -v t="$total" 'BEGIN {printf "%d",15+65*n/t}')"
        last_report=$observed
    fi
}
fetch(){
    transfer_name=$1 transfer_limit=$2
    if [ "$mode" = sd ]; then
        [ -f "$source/$transfer_name" ] && [ ! -L "$source/$transfer_name" ] || die "Missing package file"
        [ "$(wc -c < "$source/$transfer_name")" -le "$transfer_limit" ] || die "File exceeds limit"
        cp "$source/$transfer_name" "$work/$transfer_name" &
    else
        curl --fail --silent --show-error --location --max-redirs 5 \
            --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 300 \
            --max-filesize "$transfer_limit" --output "$work/$transfer_name" "$source/$transfer_name" &
    fi
    transfer_pid=$!
    if [ "${3:-}" = payload ] && [ "$ui" -eq 1 ]; then
        while kill -0 "$transfer_pid" 2>/dev/null; do
            report_transfer
            sleep 1
        done
    fi
    if wait "$transfer_pid"; then transfer_pid=""; else transfer_pid=""; die "Package transfer failed"; fi
    [ "$(wc -c < "$work/$transfer_name")" -le "$transfer_limit" ] || die "Downloaded file exceeds limit"
    if [ "${3:-}" = payload ] && [ "$ui" -eq 1 ]; then report_transfer; fi
}
fetch manifest 32768
fetch manifest.sig 8192
verify_manifest "$work/manifest" "$work/manifest.sig"
progress 15
read -r magic channel target seq < "$work/manifest"
[ -z "$expected_channel" ] || [ "$channel" = "$expected_channel" ] || die "Signed manifest channel mismatch"
[ -z "$expected_seq" ] || [ "$seq" = "$expected_seq" ] || die "Release changed; check again"
if [ "$check_only" -eq 1 ]; then
    if [ "$seq" -le "$floor" ]; then seq=0; fi
    if [ "$ui" -eq 1 ]; then printf 'POCKETOS AVAILABLE %s\n' "$seq"; else printf '%s\n' "$seq"; fi
    progress 100
    exit 0
fi
[ "$seq" -gt "$floor" ] || die "Update is not newer than installed or staged firmware"
[ ! -e "$store/versions/$seq" ] && [ ! -L "$store/versions/$seq" ] || die "Version already exists (inspect interrupted run)"
required=$(awk 'NR>1 {sum+=$2} END {printf "%.0f\n", int((sum+1023)/1024)+16384}' "$work/manifest")
free=$(df -Pk "$store" | awk 'NR==2 {print $4}')
case "$free" in ''|*[!0-9]*) die "Cannot determine SD free space";; esac
[ "$free" -ge "$required" ] || die "Insufficient microSD storage (includes 16 MiB reserve)"
if [ "$ui" -eq 1 ]; then command -v "$stat_cmd" >/dev/null || die "File size helper unavailable"; fi
total=$(awk 'NR>1 {sum+=$2} END {printf "%.0f\n",sum}' "$work/manifest")
transferred=0 last_report=0
if [ "$mode" = github ]; then phase DOWNLOAD; else phase COPY; fi
bytes 0 "$total"
# Keep the transfer PID in the parent so interrupted jobs can be cleaned up.
{
    read -r header
    while read -r hash size name; do
        fetch "$name" "$size" payload
        [ "$(wc -c < "$work/$name")" -eq "$size" ] || die "Wrong package size"
        transferred=$((transferred+size))
    done
} < "$work/manifest"
phase VERIFY
progress 80
verify_payload "$work"
progress 95
phase SAVE
mkdir "$work/payload"
mv "$work/manifest" "$work/manifest.sig" "$work/payload/"
tail -n +2 "$work/payload/manifest" | while read -r hash size name; do mv "$work/$name" "$work/payload/$name"; done
sync
mv "$work/payload" "$store/versions/$seq"
select_version "$seq" "$current" "$seq"
selection "$seq" "$current"
phase DONE
progress 100
echo "Signed SD update staged and selected: $seq. Application activation and firmware installation remain disabled."
