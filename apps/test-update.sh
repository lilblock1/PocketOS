#!/bin/sh
set -eu
updater=$1
packer=$2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/sd"
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
test -f "$tmp/sd/PocketOS-updates/versions/2/data-test"

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
cp "$POCKETOS_TEST_RELEASE/${url##*/}" "$out"
SH
chmod 755 "$tmp/shims/curl"
POCKETOS_TEST_CP="$real_cp" POCKETOS_TEST_RELEASE="$tmp/v5" PATH="$tmp/shims:$PATH" run --ui github test-5 > "$tmp/github.log"
grep -Fx 'POCKETOS PHASE DOWNLOAD' "$tmp/github.log"
grep -Fx 'POCKETOS BYTES 4096 16384' "$tmp/github.log"
grep -Fx 'POCKETOS PROGRESS 100' "$tmp/github.log"
test "$(run status)" = '5 4 5'
# Same-size tampering may finish transfer but never report success.
python3 "$packer" --sequence 6 --key "$tmp/private.pem" --output "$tmp/v6" "$tmp/data-large"
printf x | dd of="$tmp/v6/data-large" bs=1 conv=notrunc 2>/dev/null
if run --ui sd "$tmp/v6" > "$tmp/bad.log"; then exit 1; fi
if grep -q '^POCKETOS PROGRESS 100$' "$tmp/bad.log"; then exit 1; fi
grep -Fx 'POCKETOS ERROR Package checksum mismatch' "$tmp/bad.log"
test "$(run status)" = '5 4 5'
