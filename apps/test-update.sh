#!/bin/sh
set -eu
updater=$1
packer=$2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp/sd"
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
