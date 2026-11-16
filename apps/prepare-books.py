#!/usr/bin/env python3
"""PocketOS desktop book preparer: lightweight custom text extraction.
No DRM decryption, no source-file modification; writes adjacent .pocket.txt.
"""
import argparse
import html.parser
import pathlib
import posixpath
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from urllib.parse import unquote

LIMIT=64*1024*1024
HTML={".html",".htm",".xhtml"}
ZIP={".epub",".kepub",".docx",".odt",".fbz",".htmlz",".txtz"}
OTHER={".mobi",".azw",".azw3",".azw4",".rtf",".doc",".djvu",".djv",".chm",".lit",".lrf",".pdb",".prc",".pml",".rb",".snb",".tcr"}
LICENSE={".acsm",".lcpl",".kfx"}
BLOCK={"p","div","br","h1","h2","h3","h4","h5","h6","li","blockquote","section","article","tr","hr"}
class Text(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts=[]
        self.hidden=0
    def handle_starttag(self,tag,attrs):
        if tag in ("style","script","head","svg"):self.hidden+=1
        elif not self.hidden and tag in BLOCK:self.parts.append("\n")
    def handle_endtag(self,tag):
        if tag in ("style","script","head","svg"):self.hidden=max(0,self.hidden-1)
        elif not self.hidden and tag in BLOCK:self.parts.append("\n")
    def handle_data(self,data):
        if not self.hidden:self.parts.append(re.sub(r"\s+"," ",data))
    def text(self):
        return "\n".join(s.strip() for s in "".join(self.parts).splitlines() if s.strip())
def markup(data):
    parser=Text();parser.feed(data.decode("utf-8",errors="replace"));parser.close()
    return parser.text()
def lname(tag):
    return tag.rsplit("}",1)[-1]
def xmltext(data,paragraphs):
    root=ET.fromstring(data)
    return "\n".join("".join(n.itertext()).strip() for n in root.iter() if lname(n.tag) in paragraphs)
def readpart(z,name):
    i=z.getinfo(name)
    if i.file_size>LIMIT or i.flag_bits&1:raise ValueError("large or protected archive entry")
    return z.read(i)
def zipped(path):
    with zipfile.ZipFile(path) as z:
        info=z.infolist()
        if len(info)>8192 or sum(i.file_size for i in info)>LIMIT:raise ValueError("archive exceeds preparation limit")
        names={i.filename.lower() for i in info}
        if {"meta-inf/encryption.xml","meta-inf/license.lcpl","meta-inf/rights.xml"} & names or any(i.flag_bits&1 for i in info):
            raise ValueError("protected or encrypted publication requires an authorized reader")
        ext=path.suffix.lower()
        if ext in {".epub",".kepub"}:
            container=ET.fromstring(readpart(z,"META-INF/container.xml"))
            opf=next(n.attrib["full-path"] for n in container.iter() if lname(n.tag)=="rootfile")
            package=ET.fromstring(readpart(z,opf))
            manifest={n.get("id"):n.get("href") for n in package.iter() if lname(n.tag)=="item" and n.get("id") and n.get("href")}
            sections=[]
            for n in package.iter():
                if lname(n.tag)!="itemref":continue
                href=manifest.get(n.get("idref"))
                if not href:continue
                href=unquote(href.split("#",1)[0])
                member=posixpath.normpath(posixpath.join(posixpath.dirname(opf),href))
                if member.startswith(("../","/")) or "://" in member:raise ValueError("invalid EPUB spine path")
                sections.append(markup(readpart(z,member)))
            return "\n\n".join(sections)
        if ext==".docx":
            root=ET.fromstring(readpart(z,"word/document.xml"))
            return "\n".join("".join(t.text or "" for t in p.iter() if lname(t.tag)=="t") for p in root.iter() if lname(p.tag)=="p")
        if ext==".odt":return xmltext(readpart(z,"content.xml"),{"p","h"})
        allowed={".fbz":{".fb2"},".htmlz":HTML,".txtz":{".txt"}}[ext]
        sections=[]
        for i in info:
            suffix=pathlib.PurePosixPath(i.filename).suffix.lower()
            if suffix not in allowed:continue
            raw=readpart(z,i.filename)
            sections.append(xmltext(raw,{"p","subtitle"}) if suffix==".fb2" else markup(raw) if suffix in HTML else raw.decode("utf-8",errors="replace"))
        return "\n\n".join(sections)
def prepare(path):
    ext=path.suffix.lower()
    if ext in LICENSE:raise ValueError("DRM provider authorization required")
    if path.stat().st_size>LIMIT:raise ValueError("source exceeds preparation limit")
    dest=path.with_name(path.name+".pocket.txt")
    if dest.exists() or dest.is_symlink():return "existing sidecar retained"
    if ext in ZIP:
        text=zipped(path)
    elif ext in HTML:
        text=markup(path.read_bytes())
    elif ext==".fb2":
        text=xmltext(path.read_bytes(),{"p","subtitle"})
    elif ext==".pdf" or ext in OTHER:
        with tempfile.TemporaryDirectory() as tmp:
            out=pathlib.Path(tmp)/"book.txt"
            command=["pdftotext","-enc","UTF-8",str(path),str(out)] if ext==".pdf" else ["ebook-convert",str(path),str(out)]
            if not shutil.which(command[0]):raise ValueError("optional desktop utility missing: "+command[0])
            subprocess.run(command,stdin=subprocess.DEVNULL,check=True,timeout=180)
            if out.stat().st_size>LIMIT:raise ValueError("converted text too large")
            text=out.read_text(encoding="utf-8",errors="replace")
    else:raise ValueError("format not supported for text extraction")
    raw=text.strip().encode("utf-8")
    if not raw or len(raw)>LIMIT:raise ValueError("no readable text or output too large")
    with dest.open("xb") as out:out.write(raw+b"\n")
    return "prepared"
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("books",type=pathlib.Path)
    a=p.parse_args()
    root=a.books.resolve()
    if not root.is_dir():p.error("Books directory not found")
    failures=0
    for f in sorted(root.rglob("*")):
        if not f.is_file() or f.is_symlink() or f.suffix.lower() not in HTML|ZIP|OTHER|LICENSE|{".fb2",".pdf"}:continue
        try:print(f.name+": "+prepare(f))
        except (ValueError,OSError,KeyError,StopIteration,ET.ParseError,zipfile.BadZipFile,subprocess.SubprocessError) as e:
            print(f.name+": "+str(e));failures+=1
    return bool(failures)
if __name__=="__main__":raise SystemExit(main())
