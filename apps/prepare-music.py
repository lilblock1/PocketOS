#!/usr/bin/env python3
"""Prepare optional PocketOS metadata / artwork sidecars on a desktop.
Never modifies source audio. ffprobe and ffmpeg must be installed.
"""
import argparse
import json
import math
import pathlib
import subprocess
import sys

EXT={".wav",".flac",".mp3",".mp2",".mpa",".aac",".adts",".m4a",".mp4",".m4b",".alac",
     ".ogg",".oga",".opus",".wma",".asf",".ape",".aiff",".aif",".aifc",".wv",".tta",
     ".mpc",".mpp",".mp+",".ac3",".eac3",".dts",".dsf",".dff",".caf",".au",".snd",
     ".amr",".3gp",".3g2",".mka",".webm",".mkv",".ra",".rm",".spx",".tak",".voc",
     ".w64",".mlp",".thd",".aob",".oma",".aa3",".wvpk"}
def tool(args):
    return subprocess.run(args,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL,timeout=30,check=True).stdout
def clean(s):
    return "".join(c if 32<=ord(c)<=126 else " " for c in str(s))[:64]
def prepare(f):
    data=json.loads(tool(["ffprobe","-v","error","-protocol_whitelist","file,pipe",
        "-show_entries","format=duration:format_tags=title,artist,album:stream=index:stream_disposition=attached_pic",
        "-of","json",str(f)]))
    fmt=data.get("format",{})
    tags={str(k).lower():v for k,v in fmt.get("tags",{}).items()}
    text="".join(k+"="+clean(tags[k])+"\n" for k in ("title","artist","album") if tags.get(k))
    try:
        duration=float(fmt.get("duration",0))
        if math.isfinite(duration) and 0<duration<=604800:text+=f"duration={math.ceil(duration)}\n"
    except (ValueError,TypeError):pass
    side=f.with_name(f.name+".tags")
    if side.is_symlink():raise ValueError("refusing symlink")
    if text and not side.exists():
        with side.open("x",encoding="ascii") as out:out.write(text)
    for stream in data.get("streams",[]):
        if not stream.get("disposition",{}).get("attached_pic"):continue
        idx=int(stream["index"])
        raw=tool(["ffmpeg","-v","error","-nostdin","-protocol_whitelist","file,pipe","-i",str(f),"-map",f"0:{idx}",
                  "-frames:v","1","-vf","scale=64:64:force_original_aspect_ratio=decrease,pad=64:64:(ow-iw)/2:(oh-ih)/2",
                  "-pix_fmt","rgb24","-f","rawvideo","pipe:1"])
        pic=f.with_name(f.name+".ppm")
        if len(raw)==64*64*3 and not pic.exists() and not pic.is_symlink():
            with pic.open("xb") as out:out.write(b"P6\n64 64\n255\n"+raw)
        break
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("music",type=pathlib.Path)
    args=p.parse_args()
    root=args.music.resolve()
    if not root.is_dir():p.error("music folder not found")
    count=bad=0
    for f in sorted(root.rglob("*")):
        if f.is_symlink() or not f.is_file() or f.suffix.lower() not in EXT:continue
        try:prepare(f);count+=1
        except (OSError,ValueError,subprocess.SubprocessError) as e:
            print(f"{f.name}: {e}",file=sys.stderr);bad+=1
    print(f"Prepared {count} tracks; {bad} skipped or failed.")
    return bool(bad)
if __name__=="__main__":raise SystemExit(main())
