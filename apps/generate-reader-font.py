#!/usr/bin/env python3
"""Generate small proportional RGB565-renderer alpha atlases from DejaVu."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

p=argparse.ArgumentParser()
p.add_argument("output",type=Path)
p.add_argument("--fonts",type=Path,default=Path("/usr/share/fonts/truetype/dejavu"))
a=p.parse_args()
parts=["/* Generated; DejaVu font license accompanies the build. */","#ifndef POCKETOS_READER_FONT_H","#define POCKETOS_READER_FONT_H"]
for name,filename in [("regular","DejaVuSans.ttf"),("bold","DejaVuSans-Bold.ttf")]:
    font=ImageFont.truetype(str(a.fonts/filename),17)
    advance=[];rows=[]
    for code in range(32,127):
        ch=chr(code)
        advance.append(max(1,round(font.getlength(ch))))
        img=Image.new("L",(18,20))
        ImageDraw.Draw(img).text((0,15),ch,font=font,anchor="ls",fill=255)
        pixels=[(v+8)//17 for v in img.tobytes()]
        rows.append(",".join(str((pixels[i]<<4)|pixels[i+1]) for i in range(0,360,2)))
    parts.append("static const unsigned char reader_"+name+"_advance[95]={"+",".join(map(str,advance))+"};")
    parts.append("static const unsigned char reader_"+name+"_pixels[95][180]={\n"+",\n".join("{"+row+"}" for row in rows)+"\n};")
parts.append("#endif")
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text("\n".join(parts)+"\n")
