#!/bin/sh
set -eu
bin=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
test "$("$bin" --version)" = 'PocketOS shell v1'
"$bin" --self-test
for page in HOME SETTINGS SYSTEM BATTERY MUSIC CONTROL PREFERENCES DISPLAY STORAGE RADIOS SOUND UPDATES; do
    "$bin" --preview "$page" "$tmp/$page.ppm"
    test "$(wc -c < "$tmp/$page.ppm")" -eq 1152015
done
if cmp -s "$tmp/HOME.ppm" "$tmp/SETTINGS.ppm"; then exit 1; fi
if "$bin" --preview UNKNOWN "$tmp/no.ppm"; then exit 1; fi
"$bin" --test-config "$tmp/preferences"
test "$(cat "$tmp/preferences")" = 'POCKETOS4 2 1 1 1 3 37 2 42 1 2 1'
"$bin" --test-volume-config "$tmp/volume-preferences"

mkdir "$tmp/backlight"
printf '100\n' > "$tmp/backlight/max_brightness"
printf '75\n' > "$tmp/backlight/brightness"
"$bin" --test-backlight "$tmp/backlight"
test "$(cat "$tmp/backlight/brightness")" = 50

"$bin" --preview 'NOW PLAYING' "$tmp/now.ppm"
test "$(wc -c < "$tmp/now.ppm")" -eq 1152015

mkdir "$tmp/books"
printf 'CHAPTER ONE\n' > "$tmp/books/example.txt"
i=0; while [ "$i" -lt 80 ]; do printf 'Line of text %s\n' "$i" >> "$tmp/books/example.txt"; i=$((i+1)); done
"$bin" --test-books "$tmp/books"
"$bin" --preview BOOKS "$tmp/BOOKS.ppm"
"$bin" --preview READER "$tmp/READER.ppm"
test "$(wc -c < "$tmp/READER.ppm")" -eq 1152015

mkdir -p "$tmp/sd/Documents" "$tmp/sd/Downloads" "$tmp/sd/PocketOS-updates"
printf 'file manager test\n' > "$tmp/sd/Documents/example.txt"
printf 'preserve\n' > "$tmp/sd/Downloads/existing.txt"
printf 'protected\n' > "$tmp/sd/PocketOS-updates/protected.txt"
printf 'loose media\n' > "$tmp/sd/loose.txt"
printf 'firmware\n' > "$tmp/sd/r1.upt"
ln -s "$tmp" "$tmp/sd/Documents/link"
"$bin" --test-files "$tmp/sd"
test "$(cat "$tmp/sd/Documents/example.txt")" = 'file manager test'
test "$(cat "$tmp/sd/Downloads/existing.txt")" = preserve
test "$(cat "$tmp/sd/PocketOS-updates/protected.txt")" = protected
test ! -e "$tmp/sd/Downloads/renamed.txt"
test ! -e "$tmp/sd/Downloads/example.txt"
test "$(cat "$tmp/sd/Documents/loose.txt")" = 'loose media'
test ! -e "$tmp/sd/loose.txt"
test "$(cat "$tmp/sd/r1.upt")" = firmware
test -d "$tmp/sd/Documents/New folder"
test ! -d "$tmp/sd/PocketOS-updates/bad"
"$bin" --preview FILES "$tmp/FILES.ppm"
test "$(wc -c < "$tmp/FILES.ppm")" -eq 1152015

"$bin" --test-radio
for screen in WI-FI BLUETOOTH; do
    "$bin" --preview "$screen" "$tmp/$screen.ppm"
    test "$(wc -c < "$tmp/$screen.ppm")" -eq 1152015
done

"$bin" --test-update-ui

"$bin" --test-power

"$bin" --boot-preview 0 "$tmp/boot0.ppm"
"$bin" --boot-preview 3 "$tmp/boot3.ppm"
test "$(wc -c < "$tmp/boot0.ppm")" -eq 1152015
if cmp -s "$tmp/boot0.ppm" "$tmp/boot3.ppm"; then exit 1; fi
if "$bin" --boot-preview 4 "$tmp/bad.ppm"; then exit 1; fi
