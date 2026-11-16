#!/bin/sh
set -eu
bin=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
printf 'synthetic decoder\n' > "$tmp/decoder"
sha256sum "$tmp/decoder" | cut -d ' ' -f 1 > "$tmp/hash"
"$bin" --verify-fixture "$tmp/decoder" "$tmp/hash"
printf 'tamper\n' >> "$tmp/decoder"
if "$bin" --verify-fixture "$tmp/decoder" "$tmp/hash"; then exit 1; fi
ln -s "$tmp/decoder" "$tmp/link"
if "$bin" --verify-fixture "$tmp/link" "$tmp/hash"; then exit 1; fi
printf 'incorrect\n' > "$tmp/hash"
if "$bin" --verify-fixture "$tmp/decoder" "$tmp/hash"; then exit 1; fi

# Incorrect action must not attempt activation.
if "$bin" unsupported-action; then exit 1; fi
