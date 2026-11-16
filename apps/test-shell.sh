#!/bin/sh
set -eu
bin=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
test "$("$bin" --version)" = 'PocketOS shell v1'
"$bin" --self-test
for page in HOME SETTINGS SYSTEM BATTERY MUSIC CONTROL PREFERENCES DISPLAY STORAGE RADIOS SOUND UPDATES; do
    "$bin" --preview "$page" "$tmp/$page.ppm"
    test "$(wc -c < "$tmp/$page.ppm")" -eq 230415
done
if cmp -s "$tmp/HOME.ppm" "$tmp/SETTINGS.ppm"; then exit 1; fi
if "$bin" --preview UNKNOWN "$tmp/no.ppm"; then exit 1; fi
"$bin" --test-config "$tmp/preferences"
test "$(cat "$tmp/preferences")" = 'POCKETOS3 2 1 1 1 3 37 2'

mkdir "$tmp/backlight"
printf '100\n' > "$tmp/backlight/max_brightness"
printf '75\n' > "$tmp/backlight/brightness"
"$bin" --test-backlight "$tmp/backlight"
test "$(cat "$tmp/backlight/brightness")" = 50

"$bin" --preview 'NOW PLAYING' "$tmp/now.ppm"
test "$(wc -c < "$tmp/now.ppm")" -eq 230415

mkdir "$tmp/books"
printf 'CHAPTER ONE\n' > "$tmp/books/example.txt"
i=0; while [ "$i" -lt 80 ]; do printf 'Line of text %s\n' "$i" >> "$tmp/books/example.txt"; i=$((i+1)); done
"$bin" --test-books "$tmp/books"
"$bin" --preview BOOKS "$tmp/BOOKS.ppm"
"$bin" --preview READER "$tmp/READER.ppm"
test "$(wc -c < "$tmp/READER.ppm")" -eq 230415

mkdir -p "$tmp/sd/Documents" "$tmp/sd/Downloads" "$tmp/sd/PocketOS-updates"
printf 'file manager test\n' > "$tmp/sd/Documents/example.txt"
printf 'preserve\n' > "$tmp/sd/Downloads/existing.txt"
printf 'protected\n' > "$tmp/sd/PocketOS-updates/protected.txt"
ln -s "$tmp" "$tmp/sd/Documents/link"
"$bin" --test-files "$tmp/sd"
test "$(cat "$tmp/sd/Documents/example.txt")" = 'file manager test'
test "$(cat "$tmp/sd/Downloads/existing.txt")" = preserve
test "$(cat "$tmp/sd/PocketOS-updates/protected.txt")" = protected
test ! -e "$tmp/sd/Downloads/renamed.txt"
test ! -e "$tmp/sd/Downloads/example.txt"
"$bin" --preview FILES "$tmp/FILES.ppm"
test "$(wc -c < "$tmp/FILES.ppm")" -eq 230415

"$bin" --test-radio
for screen in WI-FI BLUETOOTH; do
    "$bin" --preview "$screen" "$tmp/$screen.ppm"
    test "$(wc -c < "$tmp/$screen.ppm")" -eq 230415
done
