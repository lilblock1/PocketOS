#!/bin/sh
set -eu
updater=$1
packer=$2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/sd"
# Legacy hybrid sequence history must not block the independent channel.
mkdir "$tmp/sd/PocketOS-updates"
printf '500 400 500\n' > "$tmp/sd/PocketOS-updates/selection"
if [ "$#" -ge 3 ]; then
    mkdir "$tmp/metadata"
    ln -s "$3" "$tmp/metadata/stat"
    PATH="$tmp/metadata:$PATH"; export PATH
fi
openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$tmp/private.pem" 2>/dev/null
openssl pkey -in "$tmp/private.pem" -pubout -out "$tmp/public.pem" 2>/dev/null
printf 'test payload\n' > "$tmp/data-test"
python3 "$packer" --sequence 1 --key "$tmp/private.pem" --output "$tmp/v1" "$tmp/data-test"
python3 "$packer" --sequence 2 --key "$tmp/private.pem" --output "$tmp/v2" "$tmp/data-test"
run(){ sh "$updater" --fixture "$tmp/sd" "$tmp/public.pem" "$@"; }
run sd "$tmp/v1"
test "$(run status)" = '1 0 1'
test "$(cat "$tmp/sd/PocketOS-updates/selection")" = '500 400 500'
if run sd "$tmp/v1"; then exit 1; fi
printf 'tampered\n' > "$tmp/v2/data-test"
if run sd "$tmp/v2"; then exit 1; fi
test "$(run status)" = '1 0 1'
cp "$tmp/data-test" "$tmp/v2/data-test"
run sd "$tmp/v2"
test "$(run status)" = '2 1 2'
run rollback
test "$(run status)" = '1 2 2'
if run sd "$tmp/v2"; then exit 1; fi
test -f "$tmp/sd/PocketOS-system-updates/versions/2/data-test"

# Fixed SD inbox and structured UI protocol; stage without executing payloads.
python3 "$packer" --sequence 3 --key "$tmp/private.pem" --output "$tmp/sd/PocketOS-inbox" "$tmp/data-test"
run --ui sd-inbox > "$tmp/ui.log"
grep -Fx 'POCKETOS PROGRESS 100' "$tmp/ui.log"
grep -Fx 'POCKETOS SELECTION 3 1' "$tmp/ui.log"
test "$(run --ui status)" = 'POCKETOS SELECTION 3 1'
if run --ui sd-inbox > "$tmp/ui-replay"; then exit 1; fi
grep -q '^POCKETOS ERROR ' "$tmp/ui-replay"
test "$(run status)" = '3 1 3'

# A slow transfer must report intermediate bytes before completion.
dd if=/dev/zero of="$tmp/data-large" bs=1024 count=16 2>/dev/null
python3 "$packer" --sequence 4 --key "$tmp/private.pem" --output "$tmp/v4" "$tmp/data-large"
mkdir "$tmp/shims"
real_cp=$(command -v cp)
cat > "$tmp/shims/cp" <<'SH'
#!/bin/sh
case "$1" in */data-large)
    dd if="$1" of="$2" bs=1024 count=4 2>/dev/null
    sleep 3
    dd if="$1" of="$2" bs=1024 skip=4 seek=4 conv=notrunc 2>/dev/null
    ;; *) exec "$POCKETOS_TEST_CP" "$@";; esac
SH
chmod 755 "$tmp/shims/cp"
POCKETOS_TEST_CP="$real_cp" PATH="$tmp/shims:$PATH" run --ui sd "$tmp/v4" > "$tmp/slow.log"
grep -Fx 'POCKETOS PHASE COPY' "$tmp/slow.log"
grep -Fx 'POCKETOS BYTES 4096 16384' "$tmp/slow.log"
grep -Fx 'POCKETOS BYTES 16384 16384' "$tmp/slow.log"
grep -Fx 'POCKETOS PHASE VERIFY' "$tmp/slow.log"
grep -Fx 'POCKETOS PHASE SAVE' "$tmp/slow.log"
grep -Fx 'POCKETOS PROGRESS 100' "$tmp/slow.log"
awk '/^POCKETOS PROGRESS / {if($3<p)exit 1;p=$3} END {if(p!=100)exit 1}' "$tmp/slow.log"
test "$(run status)" = '4 3 4'
# Same HTTPS backend with local stub (no network or credentials).
python3 "$packer" --sequence 5 --key "$tmp/private.pem" --output "$tmp/v5" "$tmp/data-large"
cat > "$tmp/shims/curl" <<'SH'
#!/bin/sh
out= url=
while [ "$#" -gt 0 ]; do
    case "$1" in --output) shift; out=$1;; https://*) url=$1;; esac
    shift
done
[ -n "$out" ] && [ -n "$url" ] || exit 1
# Never follow the repository's hybrid/reference test-latest or generic latest.
case "$url" in
    https://github.com/lilblock1/PocketOS/releases/download/pocketos-test-*/*|https://github.com/lilblock1/PocketOS/releases/download/pocketos-stable-latest/*) ;;
    *) exit 73 ;;
esac
cp "$POCKETOS_TEST_RELEASE/${url##*/}" "$out"
SH
chmod 755 "$tmp/shims/curl"
POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/v5" PATH="$tmp/shims:$PATH" run --ui github test-5 > "$tmp/github.log"
grep -Fx 'POCKETOS PHASE DOWNLOAD' "$tmp/github.log"
grep -Fx 'POCKETOS BYTES 4096 16384' "$tmp/github.log"
grep -Fx 'POCKETOS PROGRESS 100' "$tmp/github.log"
test "$(run status)" = '5 4 5'
# DNS failure must be surfaced before staging anything.
mkdir "$tmp/dns-shim"
printf '#!/bin/sh\nexit 6\n' > "$tmp/dns-shim/curl"
chmod 755 "$tmp/dns-shim/curl"
if PATH="$tmp/dns-shim:$PATH" run --ui check-github test-latest > "$tmp/dns.log"; then exit 1; fi
grep -Fx 'POCKETOS ERROR Wi-Fi not connected or DNS failed' "$tmp/dns.log"
test "$(run status)" = '5 4 5'
# Same-size tampering may finish transfer but never report success.
python3 "$packer" --sequence 6 --key "$tmp/private.pem" --output "$tmp/v6" "$tmp/data-large"
printf x | dd of="$tmp/v6/data-large" bs=1 conv=notrunc 2>/dev/null
if run --ui sd "$tmp/v6" > "$tmp/bad.log"; then exit 1; fi
if grep -q '^POCKETOS PROGRESS 100$' "$tmp/bad.log"; then exit 1; fi
grep -Fx 'POCKETOS ERROR Package checksum mismatch' "$tmp/bad.log"
test "$(run status)" = '5 4 5'

# Stable channel is signed independently and manifest-only checks do not stage data.
python3 "$packer" --channel stable --sequence 7 --key "$tmp/private.pem" --output "$tmp/stable7" "$tmp/data-large"
mkdir "$tmp/checkonly"
cp "$tmp/stable7/manifest" "$tmp/stable7/manifest.sig" "$tmp/checkonly/"
POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/checkonly" PATH="$tmp/shims:$PATH" run --ui check-github stable-latest > "$tmp/check.log"
grep -Fx 'POCKETOS AVAILABLE 7' "$tmp/check.log"
test "$(run status)" = '5 4 5'
test ! -e "$tmp/sd/PocketOS-system-updates/versions/7"
if POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/stable7" PATH="$tmp/shims:$PATH" run --ui check-github test-latest > "$tmp/wrong-channel.log"; then exit 1; fi
grep -Fx 'POCKETOS ERROR Signed manifest channel mismatch' "$tmp/wrong-channel.log"
if POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/stable7" PATH="$tmp/shims:$PATH" run --ui github stable-latest 8 > "$tmp/changed.log"; then exit 1; fi
grep -Fx 'POCKETOS ERROR Release changed; check again' "$tmp/changed.log"
POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/stable7" PATH="$tmp/shims:$PATH" run --ui github stable-latest 7 > "$tmp/stable.log"
grep -Fx 'POCKETOS SELECTION 7 5' "$tmp/stable.log"
test "$(run status)" = '7 5 7'
POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/checkonly" PATH="$tmp/shims:$PATH" run --ui check-github stable-latest > "$tmp/old.log"
grep -Fx 'POCKETOS AVAILABLE 0' "$tmp/old.log"

# Dedicated firmware schema accepts only the two expected assets.
dd if=/dev/zero of="$tmp/pocketos-r1.upt" bs=2048 count=1 2>/dev/null
printf '{}\n' > "$tmp/data-r1-report.json"
python3 "$packer" --firmware --channel test --sequence 8 --key "$tmp/private.pem" --output "$tmp/fw8" "$tmp/pocketos-r1.upt" "$tmp/data-r1-report.json"
# Even an authentic signature on the legacy hybrid firmware schema is rejected.
mkdir "$tmp/legacy"
sed '1s/POCKETOS-R1-ROOTFS-1/POCKETOS-R1-FW-1/' "$tmp/fw8/manifest" > "$tmp/legacy/manifest"
openssl dgst -sha256 -sign "$tmp/private.pem" -out "$tmp/legacy/manifest.sig" "$tmp/legacy/manifest"
if run --ui sd "$tmp/legacy" > "$tmp/legacy.log"; then exit 1; fi
grep -Fx 'POCKETOS ERROR Invalid signed manifest format' "$tmp/legacy.log"
test "$(run status)" = '7 5 7'
run sd "$tmp/fw8"
test "$(run status)" = '8 7 8'
if python3 "$packer" --firmware --sequence 9 --key "$tmp/private.pem" --output "$tmp/invalid" "$tmp/data-test"; then exit 1; fi

# A fixture must never access the real recovery partition.
if run --ui install > "$tmp/install.log"; then exit 1; fi
grep -Fx 'POCKETOS ERROR Recovery writes are disabled in fixtures' "$tmp/install.log"

# A firmware source offers a separate install confirmation and verified retry.
mkdir "$tmp/fw-manifest-only"
cp "$tmp/fw8/manifest" "$tmp/fw8/manifest.sig" "$tmp/fw-manifest-only/"
POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/fw-manifest-only" PATH="$tmp/shims:$PATH" run --ui check-github test-latest > "$tmp/fw-retry.log"
grep -Fx 'POCKETOS READY 8 test' "$tmp/fw-retry.log"
grep -Fx 'POCKETOS SELECTION 8 7' "$tmp/fw-retry.log"
test "$(run status)" = '8 7 8'
python3 "$packer" --firmware --channel stable --sequence 9 --key "$tmp/private.pem" --output "$tmp/fw9" "$tmp/pocketos-r1.upt" "$tmp/data-r1-report.json"
run --ui sd "$tmp/fw9" > "$tmp/fw-ready.log"
grep -Fx 'POCKETOS READY 9 stable' "$tmp/fw-ready.log"
run --ui sd "$tmp/fw9" > "$tmp/fw-retry-sd.log"
grep -Fx 'POCKETOS READY 9 stable' "$tmp/fw-retry-sd.log"
printf x | dd of="$tmp/sd/PocketOS-system-updates/versions/9/pocketos-r1.upt" bs=1 conv=notrunc 2>/dev/null
if run --ui sd "$tmp/fw9" > "$tmp/fw-bad-retry.log"; then exit 1; fi
if grep -q '^POCKETOS READY ' "$tmp/fw-bad-retry.log"; then exit 1; fi
