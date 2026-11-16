#!/bin/sh
set -eu
bin=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
test "$("$bin" --version)" = 'PocketOS shell v1'
"$bin" --self-test
for page in HOME SETTINGS SYSTEM BATTERY MUSIC; do
    "$bin" --preview "$page" "$tmp/$page.ppm"
    test "$(wc -c < "$tmp/$page.ppm")" -eq 230415
done
if cmp -s "$tmp/HOME.ppm" "$tmp/SETTINGS.ppm"; then exit 1; fi
if "$bin" --preview UNKNOWN "$tmp/no.ppm"; then exit 1; fi
