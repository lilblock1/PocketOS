import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

binary=str(pathlib.Path(sys.argv.pop(1)).resolve())


class Metadata(unittest.TestCase):
    def test_exact_owner_and_permissions_and_symlink_behavior(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp);file=root/'file';file.touch();file.chmod(0o640)
            link=root/'link';link.symlink_to(file)
            for path in (file,link):
                expected=os.lstat(path)
                for format,value in (('%u',str(expected.st_uid)),('%a',format_mode(expected.st_mode)),
                                     ('%u:%a',f'{expected.st_uid}:{format_mode(expected.st_mode)}')):
                    self.assertEqual(subprocess.check_output([binary,'-c',format,str(path)],text=True).strip(),value)

    def test_regular_file_size_only(self):
        with tempfile.TemporaryDirectory() as temp:
            root=pathlib.Path(temp); file=root/'file'; file.write_bytes(b'x'*4096)
            self.assertEqual(subprocess.check_output([binary,'-c','%s',str(file)],text=True).strip(),'4096')
            link=root/'link'; link.symlink_to(file)
            for path in (root,link):
                self.assertNotEqual(subprocess.run([binary,'-c','%s',str(path)],capture_output=True).returncode,0)

    def test_missing_path_and_unsupported_formats_fail_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertNotEqual(subprocess.run([binary,'-c','%u:%a',str(pathlib.Path(temp)/'missing')],capture_output=True).returncode,0)
            result=subprocess.run([binary,'-c','%F',temp],capture_output=True)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(result.stdout,b'')


def format_mode(mode):return format(mode&0o7777,'o')


if __name__=='__main__':unittest.main()
