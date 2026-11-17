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
    identity=root/"usr/resource/sonix/components/system-info.json"
    identity.parent.mkdir(parents=True,exist_ok=True)
    identity.write_text('{"device-name":"HiBy R1","OS_version":"0.3.0.99","ota-repo":"lilblock1/Sonix-Books"}')
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
    def test_opt_in_guard_preserves_exact_original_player_and_hardware(self):
        with tempfile.TemporaryDirectory() as temp:
            base=Path(temp)
            root=base/"ref"
            apps=base/"apps"
            make_fixture(root,apps)
            original=overlay.manifest(root)
            report=overlay.stage(root,apps,base/"candidate")
            guard=base/"guard.sh"
            guard.write_text("#!/bin/sh\nexec /usr/bin/sonix_player.pocketos-fallback.sh\n")
            result=overlay.add_guard(base/"candidate",original,guard,report)
            self.assertTrue(result["guarded_boot_opt_in"])
            self.assertFalse(result["installation_allowed"])
            self.assertFalse(result["flashable"])
            self.assertTrue(result["vendor_hardware_initialization_unchanged"])
            self.assertFalse(result["pocketos_launcher_autostart"])
            self.assertEqual((base/"candidate/usr/bin/sonix_player.pocketos-fallback.sh").read_bytes(),
                             (root/"usr/bin/sonix_player.sh").read_bytes())
            self.assertEqual((base/"candidate/module_driver/soc_fb.ko").read_bytes(),
                             (root/"module_driver/soc_fb.ko").read_bytes())
            with self.assertRaisesRegex(ValueError,"collision"):
                overlay.add_guard(base/"candidate",original,guard,report)
    def test_internal_pocketos_is_default_with_original_player_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp)
            ref=base/"reference"
            apps=base/"apps"
            make_fixture(ref,apps)
            stock=overlay.manifest(ref)
            boot=base/"boot.sh"
            boot.write_text(
                "#!/bin/sh\n"
                "ORIGINAL=/usr/bin/sonix_player.pocketos-fallback.sh\n"
                "POCKETOS=/usr/bin/pocketos-shell\n"
                "fallback(){ exec \"$ORIGINAL\"; }\n"
                "\"$POCKETOS\" --boot-splash --auto-input || fallback\n"
            )
            result=overlay.stage_internal_gui(ref,base/"candidate",apps,boot,"0.3.0.101")
            built=base/"candidate"
            self.assertTrue(result["pocketos_internal"])
            self.assertTrue(result["pocketos_default_boot"])
            self.assertFalse(result["sd_boot_marker_required"])
            self.assertEqual(set(result["installed_pocketos_apps"]),set(overlay.INTERNAL_GUI_APPS))
            self.assertEqual((built/"usr/bin/pocketos-shell").read_bytes(),
                             (apps/"pocketos-shell").read_bytes())
            self.assertEqual(overlay.digest(built/"usr/bin/sonix_player.pocketos-fallback.sh"),
                             stock["usr/bin/sonix_player.sh"])
            self.assertEqual((built/"usr/bin/sonix_player").read_bytes(),
                             (ref/"usr/bin/sonix_player").read_bytes())
            updated=overlay.manifest(built)
            self.assertEqual([name for name in stock if updated[name]!=stock[name]],
                             ["usr/bin/sonix_player.sh"])
            self.assertIn("lilblock1/PocketOS",
                          (built/"usr/resource/sonix/components/system-info.json").read_text())
            with self.assertRaisesRegex(ValueError,"existing staged"):
                overlay.stage_internal_gui(ref,built,apps,boot,"0.3.0.101")

    def test_no_sd_required_in_default_boot(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp)
            ref=base/"reference"
            apps=base/"apps"
            make_fixture(ref,apps)
            boot=base/"boot.sh"
            boot.write_text(
                "#!/bin/sh\n"
                "ORIGINAL=/usr/bin/sonix_player.pocketos-fallback.sh\n"
                "POCKETOS=/usr/bin/pocketos-shell\n"
                "fallback(){ exec \"$ORIGINAL\"; }\n"
                "test -f /mnt/sd_0/PocketOS-try-new-ui || fallback\n"
                "\"$POCKETOS\" --auto-input\n"
            )
            with self.assertRaisesRegex(ValueError,"microSD"):
                overlay.stage_internal_gui(ref,base/"candidate",apps,boot,"0.3.0.101")

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
