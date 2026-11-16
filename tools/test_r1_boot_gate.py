import tempfile
import unittest
from pathlib import Path
from r1_boot_gate import parse_mtd, size_check, inspect_init, report, sha

class BootTests(unittest.TestCase):
    def test_partition_sizes_and_unknown_labels(self):
        parts = parse_mtd('dev: size erasesize name\nmtd3: 01000000 00020000 "rootfs"\n')
        self.assertEqual(parts["rootfs"]["bytes"], 16777216)
        self.assertTrue(size_check(1000, parts, "rootfs")["fits_raw"])
        self.assertFalse(size_check(20000000, parts, "rootfs")["fits_raw"])
        self.assertIsNone(size_check(1000, parts, "kernel")["fits_raw"])

    def test_bad_partition_records(self):
        for data in ('mtd0: 00000000 00020000 "kernel"',
                     'mtd0: 00020000 00000000 "kernel"',
                     'mtd0: 00020000 00020000 "kernel"\nmtd1: 00020000 00020000 "kernel"',
                     'unexpected'):
            with self.assertRaises(ValueError):
                parse_mtd(data)

    def test_no_install_even_when_size_fits(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            kernel = root/"kernel"; kernel.write_bytes(b"k")
            ref = root/"old"; ref.write_bytes(b"old")
            new = root/"new"; new.write_bytes(b"new")
            (root/"sbin").mkdir()
            (root/"bin").mkdir()
            (root/"bin/busybox").write_bytes(b"fixture")
            (root/"sbin/init").symlink_to("/bin/busybox")
            self.assertTrue(inspect_init(root)["init_binary_exists"])
            integrated = {"flashable":False,"kernel":{"sha256":sha(kernel)}}
            plan = {"flashable":False,"apply_enabled":False}
            parts = parse_mtd('mtd0: 00020000 00020000 "kernel"\nmtd1: 00020000 00020000 "rootfs"')
            r = report(root, ref, new, kernel, integrated, plan, parts)
            self.assertFalse(r["installation_allowed"])
            self.assertTrue(r["raw_size_checks"]["rootfs"]["fits_raw"])
            self.assertFalse(r["sd_boot_verified"])
            plan["apply_enabled"] = True
            with self.assertRaises(ValueError):
                report(root, ref, new, kernel, integrated, plan, parts)

if __name__ == "__main__":
    unittest.main()
