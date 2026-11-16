import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('media',Path(__file__).with_name('prepare-r1-media.py'))
media=importlib.util.module_from_spec(spec);spec.loader.exec_module(media)
class MediaTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        base=Path(self.temp.name);self.root=base/'root';self.config=self.root/'etc/pocketos';self.config.mkdir(parents=True)
        (self.root/'etc/pocketos-release').write_text('NAME=PocketOS\nTARGET=hiby-r1\nSTATE=development-userspace-only\nFLASHABLE=no\n')
        for name in ('autostart','firstboot-enable'):(self.config/name).write_text('fixture\n')
        apps=self.root/'usr/bin';apps.mkdir(parents=True)
        for name in ('pocketos-start','pocketos-music','pocketos-shell','pocketos-decoder'):(apps/name).write_text('fixture')
        self.bin=base/'ffmpeg';self.bin.write_bytes(b'decoder fixture');self.pin=hashlib.sha256(self.bin.read_bytes()).hexdigest()
    def test_markers_only(self):
        markers=media.prepare(self.root,self.bin,self.pin)
        self.assertEqual(set(markers),{'audio-enable','smart-sleep-enable','decoder.sha256'})
        for name,value in markers.items():
            self.assertEqual((self.config/name).read_text(),value)
            self.assertEqual((self.config/name).stat().st_mode&0o777,0o400)
        self.assertFalse((self.root/'usr/bin/ffmpeg').exists())
    def test_bad_digest(self):
        with self.assertRaises(ValueError):media.prepare(self.root,self.bin,'0'*64)
        self.assertFalse((self.config/'audio-enable').exists())
    def test_broad_permissions_rejected(self):
        (self.config/'features').touch()
        with self.assertRaises(ValueError):media.prepare(self.root,self.bin,self.pin)
        self.assertFalse((self.config/'smart-sleep-enable').exists())
    def test_existing_marker_rejected_without_partial_setup(self):
        (self.config/'decoder.sha256').touch()
        with self.assertRaises(ValueError):media.prepare(self.root,self.bin,self.pin)
        self.assertFalse((self.config/'audio-enable').exists())
if __name__=='__main__':unittest.main()
