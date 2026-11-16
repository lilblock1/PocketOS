#!/usr/bin/env python3
"""Generate small proportional RGB565-renderer alpha atlases from DejaVu."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

p=argparse.ArgumentParser()
p.add_argument("output",type=Path)
p.add_argument("--fonts",type=Path,default=Path("/usr/share/fonts/truetype/dejavu"))
p.add_argument("--regular-file",type=Path)
p.add_argument("--bold-file",type=Path)
a=p.parse_args()
parts=["/* Generated alpha atlases; retain the source font license. */","#ifndef POCKETOS_READER_FONT_H","#define POCKETOS_READER_FONT_H", "#define READER_ATLAS_WIDTH 36", "#define READER_ATLAS_HEIGHT 40"]
regular=a.regular_file or a.fonts/"DejaVuSans.ttf"
bold=a.bold_file or a.fonts/"DejaVuSans-Bold.ttf"
for name,filename in [("regular","DejaVuSans.ttf"),("bold","DejaVuSans-Bold.ttf")]:
    font=ImageFont.truetype(str(regular if name=="regular" else bold),34)
    advance=[];rows=[]
    for code in range(32,127):
        ch=chr(code)
        advance.append(max(1,round(font.getlength(ch))))
        img=Image.new("L",(36,40))
        ImageDraw.Draw(img).text((0,30),ch,font=font,anchor="ls",fill=255)
        pixels=[(v+8)//17 for v in img.tobytes()]
        rows.append(",".join(str((pixels[i]<<4)|pixels[i+1]) for i in range(0,1440,2)))
    parts.append("static const unsigned char reader_"+name+"_advance[95]={"+",".join(map(str,advance))+"};")
    parts.append("static const unsigned char reader_"+name+"_pixels[95][720]={\n"+",\n".join("{"+row+"}" for row in rows)+"\n};")
# Match Sonix's native 24px body, 28px labels and 32px headings. These
# atlases are drawn 1:1 on the R1, without scaling a small glyph bitmap.
parts.extend(["#define UI_ATLAS_WIDTH 40", "#define UI_ATLAS_HEIGHT 48"])
ui_rows=[];ui_advance=[]
for size,face in [(24,regular),(28,bold),(32,bold)]:
    font=ImageFont.truetype(str(face),size)
    baseline=font.getmetrics()[0]
    rows=[];advance=[]
    for code in range(32,127):
        ch=chr(code);advance.append(max(1,round(font.getlength(ch))))
        img=Image.new("L",(40,48));ImageDraw.Draw(img).text((0,baseline),ch,font=font,anchor="ls",fill=255)
        pixels=[(v+8)//17 for v in img.tobytes()]
        rows.append("{"+",".join(str((pixels[i]<<4)|pixels[i+1]) for i in range(0,1920,2))+"}")
    ui_rows.append("{"+",\n".join(rows)+"}");ui_advance.append("{"+",".join(map(str,advance))+"}")
parts.append("static const unsigned char ui_advance[3][95]={"+",".join(ui_advance)+"};")
parts.append("static const unsigned char ui_pixels[3][95][960]={\n"+",\n".join(ui_rows)+"\n};")
parts.append("#endif")
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text("\n".join(parts)+"\n")
