import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("r1_stage", Path(__file__).with_name("r1_stage.py"))
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)

class StageTests(unittest.TestCase):
    def test_copy_and_digest(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            source = root / "module.ko"
            source.write_bytes(b"test module")
            result = stage.copy_checked(source, root / "copy", root, stage.digest(source))
            self.assertEqual(result["bytes"], 11)
            with self.assertRaises(ValueError):
                stage.copy_checked(source, root / "copy", root)
            with self.assertRaises(ValueError):
                stage.copy_checked(source, root / "wrong", root, "0" * 64)

    def test_reject_symlink_and_outside_source(self):
        with tempfile.TemporaryDirectory() as d, tempfile.TemporaryDirectory() as outside:
            root = Path(d)
            path = Path(outside) / "external"
            path.write_bytes(b"external")
            link = root / "link"
            link.symlink_to(path)
            for src in (path, link):
                with self.assertRaises(ValueError):
                    stage.copy_checked(src, root / "copy", root)
    
    def test_literal_order_does_not_execute(self):
        with tempfile.TemporaryDirectory() as d:
            script = Path(d) / "driver.sh"
            script.write_text("# sh ignored.sh\nsh first.sh\nsh second.sh # comment\nsh ../unsafe.sh\nsh $var\nsh third.sh && reboot\n")
            self.assertEqual(stage.literal_order(script), ["first.sh", "second.sh"])

if __name__ == "__main__":
    unittest.main()
