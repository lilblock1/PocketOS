#!/usr/bin/env python3
"""Prepare a tiny grayscale reader font from a font you are licensed to use.
Requires Pillow on the desktop; no font library is required on the R1."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

p=argparse.ArgumentParser(description=__doc__)
p.add_argument("font",type=Path)
p.add_argument("output",type=Path)
a=p.parse_args()
font=ImageFont.truetype(str(a.font),14)
data=bytearray(b"PBF1")
for i in range(32,127):
    ch=chr(i)
    img=Image.new("L",(12,16))
    dr=ImageDraw.Draw(img)
    box=dr.textbbox((0,0),ch,font=font)
    width=box[2]-box[0]
    dr.text(((12-width)//2-box[0],-1),ch,font=font,fill=255)
    data.extend(img.tobytes())
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_bytes(data)
print("Reader atlas: "+str(len(data))+" bytes")
