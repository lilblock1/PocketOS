import shutil, pathlib, struct, subprocess, sys, tempfile, unittest, wave
bin=str(pathlib.Path(sys.argv.pop(1)).resolve())
class Music(unittest.TestCase):
    def test_pcm(self):
        with tempfile.TemporaryDirectory() as d:
            f=pathlib.Path(d)/"test.wav"
            raw=struct.pack("<hhhh",-32768,-10000,0,32767)*4097
            with wave.open(str(f),"wb") as w:
                w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes(raw)
            probe=subprocess.check_output([bin,"--probe",str(f)])
            self.assertIn(b"rate=44100",probe)
            self.assertEqual(subprocess.check_output([bin,"--decode",str(f)]),raw)
            self.assertEqual(subprocess.check_output([bin,"--decode",str(f),"0"]),bytes(len(raw)))
            quarter=struct.pack("<hhhh",-8192,-2500,0,8191)*4097
            self.assertEqual(subprocess.check_output([bin,"--decode",str(f),"50"]),quarter)
            f.write_bytes(f.read_bytes()[:-1])
            self.assertNotEqual(subprocess.run([bin,"--probe",str(f)],capture_output=True).returncode,0)
    def test_24bit(self):
        with tempfile.TemporaryDirectory() as d:
            f=pathlib.Path(d)/"test.wav"
            raw=b"\x00\x00\x80\xff\xff\x7f"
            with wave.open(str(f),"wb") as w:
                w.setnchannels(1);w.setsampwidth(3);w.setframerate(96000);w.writeframes(raw)
            self.assertEqual(subprocess.check_output([bin,"--decode",str(f)]),raw)
            self.assertEqual(subprocess.check_output([bin,"--decode",str(f),"50"]),b"\x00\x00\xe0\xff\xff\x1f")
    @unittest.skipUnless(shutil.which("ffmpeg"),"optional host decoder unavailable")
    def test_compressed(self):
        with tempfile.TemporaryDirectory() as d:
            f=pathlib.Path(d)/"source.wav"
            raw=struct.pack("<hh",-12000,12000)*9600
            with wave.open(str(f),"wb") as w:
                w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(raw)
            for ext,codec in [("flac","flac"),("mp3","libmp3lame"),("aac","aac"),("m4a","aac"),("ogg","libvorbis"),("opus","libopus")]:
                with self.subTest(ext=ext):
                    path=f.with_suffix("."+ext)
                    subprocess.run(["ffmpeg","-v","error","-i",str(f),"-c:a",codec,str(path)],check=True)
                    pcm=subprocess.check_output([bin,"--decode",str(path)],timeout=20)
                    self.assertGreater(len(pcm),0)
                    self.assertEqual(len(pcm)%4,0)
                    if ext=="flac":self.assertEqual(pcm,raw)
            bad=f.with_suffix(".mp3")
            bad.write_bytes(b"not audio")
            self.assertNotEqual(subprocess.run([bin,"--decode",str(bad)],capture_output=True,timeout=20).returncode,0)
if __name__=="__main__":unittest.main()
