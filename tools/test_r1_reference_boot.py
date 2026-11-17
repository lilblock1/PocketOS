"""Host fixtures for the reference-preserving PocketOS R1 boot compatibility overlay."""
import tempfile
import unittest
from pathlib import Path
import r1_reference_boot as overlay


def make_fixture(root, apps):
    for directory in ("etc/init.d","module_driver","usr/bin","usr/sbin","sbin"):
        (root / directory).mkdir(parents=True,exist_ok=True)
    for filename in ("etc/inittab","sbin/init","usr/bin/sonix_player","usr/bin/sonix_player.sh"):
        (root / filename).write_bytes(b"ORIGINAL-"+filename.encode())
    for i in range(20):
        (root / f"etc/init.d/S{i:02d}original").write_bytes(f"original init {i}".encode())
    (root / "module_driver" / "soc_fb.ko").write_bytes(b"original driver")
    apps.mkdir()
    for app in overlay.APPLICATIONS:
        (apps / app).write_bytes(b"pocketos "+app.encode())


class Stage(unittest.TestCase):
    def test_preserves_vendor_boot_and_stages_only_extra_apps(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp)
            root=base/"ref"
            apps=base/"apps"
            make_fixture(root,apps)
            before=overlay.manifest(root)
            report=overlay.stage(root,apps,base/"candidate")
            self.assertEqual(before,overlay.manifest(base/"candidate"))
            self.assertFalse(report["flashable"])
            self.assertFalse(report["installation_allowed"])
            self.assertFalse(report["pocketos_launcher_autostart"])
            self.assertTrue(report["vendor_boot_files_unchanged"])
            self.assertEqual(set(report["added_applications"]),set(overlay.APPLICATIONS))
            self.assertEqual((base/"candidate/usr/bin/sonix_player").read_bytes(), b"ORIGINAL-usr/bin/sonix_player")
            self.assertTrue((base/"candidate/usr/bin/pocketos-shell").is_file())
    def test_refuses_collision_with_reference(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp)
            root=base/"ref"
            apps=base/"apps"
            make_fixture(root,apps)
            (root/"usr/bin/pocketos-shell").write_bytes(b"existing")
            with self.assertRaisesRegex(ValueError,"overwrite"):
                overlay.stage(root,apps,base/"candidate")
            self.assertFalse((base/"candidate").exists())
    def test_requires_real_vendor_startup(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp)
            root=base/"ref"
            apps=base/"apps"
            make_fixture(root,apps)
            (root/"sbin/init").unlink()
            with self.assertRaisesRegex(ValueError,"missing"):
                overlay.stage(root,apps,base/"candidate")


if __name__=="__main__":
    unittest.main()
