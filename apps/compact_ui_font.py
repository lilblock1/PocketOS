#!/usr/bin/env python3
"""Losslessly store native UI and reader glyphs as zero skips and literal packed pixels."""
import argparse
from pathlib import Path
import re

def encode_glyphs(pixels, stride):
    offsets = [0]
    encoded = bytearray()
    for start in range(0, len(pixels), stride):
        glyph = pixels[start:start+stride]
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
    return offsets, encoded


def declaration(name, offsets, encoded):
    kind = "unsigned short" if offsets[-1] <= 65535 else "unsigned int"
    return ("#define " + name.upper() + "_ATLAS_SPARSE 1\n"
            "static const " + kind + " " + name + "_offsets[" + str(len(offsets)) + "]={" + ",".join(map(str, offsets)) + "};\n"
            "static const unsigned char " + name + "_data[" + str(len(encoded)) + "]={\n" +
            ",\n".join(",".join(map(str, encoded[i:i+96])) for i in range(0,len(encoded),96)) + "\n};")


def compact(text, compress_reader=True):
    if compress_reader and "#define READER_ATLAS_SPARSE 1" not in text:
        matches = [re.search(r"static const unsigned char reader_" + face + r"_pixels\[95\]\[(\d+)\]=\{(.*?)\n\};", text, re.S) for face in ("regular", "bold")]
        if not all(matches) or matches[0][1] != matches[1][1]:
            raise ValueError("Expected proportional reader atlases")
        stride = int(matches[0][1])
        pixels = b"".join(bytes(map(int, re.findall(r"\d+", match[2]))) for match in matches)
        if len(pixels) != 190*stride:
            raise ValueError("Unexpected reader atlas length")
        offsets, encoded = encode_glyphs(pixels, stride)
        # Remove from the end so the earlier source positions stay valid.
        text = text[:matches[1].start()] + text[matches[1].end():]
        text = text[:matches[0].start()] + declaration("reader", offsets, encoded) + text[matches[0].end():]
    if "#define UI_ATLAS_SPARSE 1" not in text:
        match = re.search(r"static const unsigned char ui_pixels\[3\]\[95\]\[960\]=\{(.*?)\n\};", text, re.S)
        if not match:
            raise ValueError("Expected native 40x48 packed UI glyphs")
        pixels = bytes(map(int, re.findall(r"\d+", match[1])))
        if len(pixels) != 3*95*960:
            raise ValueError("Unexpected UI atlas length")
        offsets, encoded = encode_glyphs(pixels, 960)
        text = text[:match.start()] + declaration("ui", offsets, encoded) + text[match.end():]
    return text

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path); p.add_argument("output", type=Path)
    p.add_argument("--dense-reader",action="store_true",help="Validation: leave reader pixels dense")
    a = p.parse_args()
    a.output.write_text(compact(a.input.read_text(),not a.dense_reader))
