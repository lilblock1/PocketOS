#!/usr/bin/env python3
"""Prepare a tiny grayscale reader font from a font you are licensed to use.
Requires Pillow on the desktop; no font library is required on the R1."""
import argparse
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

p=argparse.ArgumentParser(description=__doc__)
p.add_argument("font",type=Path)
p.add_argument("output",type=Path)
p.add_argument("--bold-file",type=Path)
a=p.parse_args()
faces=[]
for path in (a.font,a.bold_file or a.font):
    probe=ImageFont.truetype(str(path),34)
    box=probe.getbbox('H');cap=max(1,box[3]-box[1])
    faces.append(ImageFont.truetype(str(path),max(24,min(48,round(34*25/cap)))))
width=max(36,max(font.getbbox(chr(i))[2]+2 for font in faces for i in range(32,127)))
width=(width+1)//2*2
if width>64:raise SystemExit('Font exceeds bounded atlas width')
height=40
advance=bytearray();pixels=bytearray()
for font in faces:
    for i in range(32,127):
        ch=chr(i);advance.append(max(1,min(width+8,round(font.getlength(ch)))))
        img=Image.new('L',(width,height))
        ImageDraw.Draw(img).text((0,30),ch,font=font,anchor='ls',fill=255)
        alpha=[(v+8)//17 for v in img.tobytes()]
        pixels.extend((alpha[j]<<4)|alpha[j+1] for j in range(0,len(alpha),2))
data=b'PBF2'+struct.pack('<HH',width,height)+advance+pixels
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_bytes(data)
print("Reader atlas: "+str(len(data))+" bytes")
