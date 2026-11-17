import pathlib, struct, subprocess, sys, tempfile
binary=sys.argv[1]
with tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory)
    def output(file):
        return subprocess.check_output([binary,"--test-metadata",str(file)],text=True).splitlines()
    p=root/"a.flac"
    stream=bytearray(34)
    stream[10:18]=((48000<<44)|96000).to_bytes(8,"big")
    fields=[b"TITLE=Test song",b"ARTIST=Test artist",b"ALBUM=Test album"]
    tags=struct.pack("<II",0,len(fields))+b"".join(struct.pack("<I",len(x))+x for x in fields)
    p.write_bytes(b"fLaC"+b"\0\0\0\x22"+stream+b"\x84"+len(tags).to_bytes(3,"big")+tags)
    assert output(p)==["Test song","Test artist","Test album","2"]
    frames=b"".join(tag+len(value).to_bytes(4,"big")+b"\0\0"+value for tag,value in [(b"TIT2",b"\x03Track"),(b"TPE1",b"\0Artist"),(b"TALB",b"\0Album")])
    p=root/"a.mp3"
    n=len(frames)
    p.write_bytes(b"ID3\x03\0\0"+bytes([(n>>21)&127,(n>>14)&127,(n>>7)&127,n&127])+frames)
    assert output(p)==["Track","Artist","Album","0"]
    p.write_bytes(b"ID3\x03\0\0\x7f\x7f\x7f\x7f")
    assert output(p)==["","","","0"]
    p.write_bytes(b"fLaC\x84\xff\xff\xff")
    assert output(p)==["","","","0"]
