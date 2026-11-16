#!/usr/bin/env python3
"""Exercise the restricted candidate, not just the distro FFmpeg."""
import pathlib, struct, subprocess, sys, tempfile, wave
candidate = sys.argv[1:]
assert candidate
with tempfile.TemporaryDirectory() as d:
    base=pathlib.Path(d)/"source.wav"
    raw=struct.pack("<hh",-12000,12000)*9600
    with wave.open(str(base),"wb") as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(raw)
    for ext,codec,demux in [
        ("flac","flac","flac"),("mp3","libmp3lame","mp3"),("aac","aac","aac"),
        ("m4a","alac","mov"),("ogg","libvorbis","ogg"),("opus","libopus","ogg"),
        ("wma","wmav2","asf"),("aiff","pcm_s16be","aiff"),("wv","wavpack","wv"),
        ("tta","tta","tta"),("ac3","ac3","ac3"),("mka","flac","matroska"),
    ]:
        path=base.with_suffix("."+ext)
        subprocess.run(["ffmpeg","-v","error","-i",str(base),"-c:a",codec,str(path)],check=True)
        pcm=subprocess.check_output(candidate+["-nostdin","-hide_banner","-loglevel","error","-xerror",
            "-threads","1","-filter_threads","1","-max_alloc","16777216","-probesize","262144",
            "-analyzeduration","1000000","-protocol_whitelist","file,pipe","-f",demux,
            "-ss","0","-i",str(path),"-map","0:a:0","-vn","-sn","-dn","-ac","2","-ar","48000",
            "-c:a","pcm_s16le","-f","s16le","pipe:1"],timeout=90)
        assert pcm and len(pcm)%4==0,ext
        if ext in ("flac","m4a","aiff","wv","tta","mka"):assert pcm==raw,ext
        print(ext,len(pcm))
