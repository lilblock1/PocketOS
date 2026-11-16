import pathlib
import subprocess
import sys
import tempfile
import unittest

BINARY=str(pathlib.Path(sys.argv.pop(1)).resolve())
PREPARE=str(pathlib.Path(sys.argv.pop(1)).resolve())


class ReaderFonts(unittest.TestCase):
    def test_native_atlases_and_damaged_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp);folder=root/'.fonts';folder.mkdir()
            for name in ('serif','dyslexic'):
                subprocess.run([sys.executable,PREPARE,'/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf',str(folder/(name+'.pbf')),
                                '--bold-file','/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf'],check=True,stdout=subprocess.DEVNULL)
            def run():return subprocess.run([BINARY,'--test-reader-fonts',str(root)],capture_output=True).returncode
            self.assertEqual(run(),0)
            font=folder/'serif.pbf';original=font.read_bytes()
            font.write_bytes(original[:4]+b'\xff\xff'+original[6:])
            self.assertNotEqual(run(),0)
            font.write_bytes(original[:-1]);self.assertNotEqual(run(),0)
            font.unlink();font.symlink_to(folder/'dyslexic.pbf');self.assertNotEqual(run(),0)


if __name__=='__main__':unittest.main()
