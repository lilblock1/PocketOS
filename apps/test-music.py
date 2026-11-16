import pathlib, struct, subprocess, sys, tempfile, unittest, wave
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
if __name__=="__main__":unittest.main()
