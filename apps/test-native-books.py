import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile

BINARY=str(pathlib.Path(sys.argv.pop(1)).resolve())


class NativeBooks(unittest.TestCase):
    def make(self,root,compression=zipfile.ZIP_DEFLATED,href='first.xhtml'):
        path=root/'book.epub'
        with zipfile.ZipFile(path,'w',compression=compression) as z:
            z.writestr('META-INF/container.xml','<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>')
            z.writestr('OPS/book.opf',f'<package><manifest><item id="one" href="{href}"/><item id="two" href="second.xhtml"/></manifest><spine><itemref idref="two"/><itemref idref="one"/></spine></package>')
            z.writestr('OPS/first.xhtml','<html><head><title>hidden title</title></head><body><h1>First</h1><p>Words &amp; spaces.</p></body></html>')
            z.writestr('OPS/second.xhtml','<html><body><p>Second</p><script>hidden</script></body></html>')
        return path

    def run_book(self,path):return subprocess.run([BINARY,'--epub',str(path)],capture_output=True,timeout=10)

    def test_spine_order_entities_and_original_unchanged(self):
        for compression in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED):
            with tempfile.TemporaryDirectory() as temp:
                path=self.make(pathlib.Path(temp),compression);original=path.read_bytes()
                self.assertEqual(self.run_book(path).returncode,0)
                self.assertEqual(path.with_name(path.name+'.pocket.txt').read_text(),'Second\n\n# First\nWords & spaces.\n')
                self.assertTrue(path.with_name(path.name+'.pocket.meta').read_text().startswith('POCKETOS-BOOK-1\n'))
                self.assertEqual(path.read_bytes(),original)

    def test_protected_publication_is_not_converted(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.make(pathlib.Path(temp))
            with zipfile.ZipFile(path,'a') as z:z.writestr('META-INF/encryption.xml','<encryption/>')
            self.assertNotEqual(self.run_book(path).returncode,0)
            self.assertFalse(path.with_name(path.name+'.pocket.txt').exists())

    def test_unsafe_archive_and_spine_paths_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.make(pathlib.Path(temp),href='../../../outside.xhtml')
            self.assertNotEqual(self.run_book(path).returncode,0)
            path.unlink();path=self.make(pathlib.Path(temp))
            with zipfile.ZipFile(path,'a') as z:z.writestr('../outside','unsafe')
            self.assertNotEqual(self.run_book(path).returncode,0)

    def test_crc_failure_discards_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            path=self.make(pathlib.Path(temp),zipfile.ZIP_STORED)
            data=path.read_bytes().replace(b'Words &amp; spaces.',b'WOrds &amp; spaces.',1);path.write_bytes(data)
            self.assertNotEqual(self.run_book(path).returncode,0)
            self.assertFalse(path.with_name(path.name+'.pocket.txt').exists())
            self.assertFalse(list(path.parent.glob('*.tmp.*')))

    def test_symlink_source_and_cache_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp);path=self.make(root);alias=root/'alias.epub';alias.symlink_to(path)
            self.assertNotEqual(self.run_book(alias).returncode,0)
            target=root/'user.txt';target.write_text('untouched')
            path.with_name(path.name+'.pocket.txt').symlink_to(target)
            self.assertNotEqual(self.run_book(path).returncode,0)
            self.assertEqual(target.read_text(),'untouched')


if __name__=='__main__':unittest.main()
