#!/usr/bin/env python3
"""Losslessly store native UI glyphs as zero skips and literal packed pixels."""
import argparse
from pathlib import Path
import re

def compact(text):
    if "#define UI_ATLAS_SPARSE 1" in text:
        return text
    m = re.search(r"static const unsigned char ui_pixels\[3\]\[95\]\[960\]=\{(.*?)\n\};", text, re.S)
    if not m:
        raise ValueError("Expected native 40x48 packed UI glyphs")
    pixels = bytes(map(int, re.findall(r"\d+", m[1])))
    if len(pixels) != 3*95*960:
        raise ValueError("Unexpected atlas length")
    offsets = [0]
    encoded = bytearray()
    for start in range(0, len(pixels), 960):
        glyph = pixels[start:start+960]
        out = bytearray()
        i = 0
        while i < len(glyph):
            skip = 0
            while i < len(glyph) and glyph[i] == 0 and skip < 255:
                i += 1; skip += 1
            begin = i
            while i < len(glyph) and glyph[i] != 0 and i-begin < 255:
                i += 1
            out.extend((skip, i-begin)); out.extend(glyph[begin:i])
        decoded = bytearray()
        at = 0
        while at < len(out):
            skip, count = out[at:at+2]; at += 2
            decoded.extend(bytes(skip)); decoded.extend(out[at:at+count]); at += count
        if decoded != glyph:
            raise AssertionError("Glyph pixels changed")
        encoded.extend(out); offsets.append(len(encoded))
    packed = ("#define UI_ATLAS_SPARSE 1\n"
              "static const unsigned int ui_offsets[286]={"+",".join(map(str, offsets))+"};\n"
              "static const unsigned char ui_data["+str(len(encoded))+"]={\n"+
              ",\n".join(",".join(map(str, encoded[i:i+96])) for i in range(0,len(encoded),96))+"\n};")
    return text[:m.start()]+packed+text[m.end():]

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path); p.add_argument("output", type=Path)
    a = p.parse_args()
    a.output.write_text(compact(a.input.read_text()))
