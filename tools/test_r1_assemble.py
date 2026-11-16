import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import r1_assemble as a

class Assembly(unittest.TestCase):
    def test_refuse_non_development_and_marker(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);(root/"etc").mkdir()
            with self.assertRaises(ValueError):a.validate_root(root)
            (root/"etc/pocketos-release").write_text("NAME=PocketOS\nTARGET=hiby-r1\nSTATE=development-userspace-only\nFLASHABLE=no\n")
            a.validate_root(root)
            (root/"etc/pocketos").mkdir()
            (root/"etc/pocketos/autostart").touch()
            with self.assertRaises(ValueError):a.validate_root(root)
    def test_stage(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory);root=base/"root";apps=base/"apps"
            (root/"etc/init.d").mkdir(parents=True);apps.mkdir()
            (root/"etc/pocketos-release").write_text("NAME=PocketOS\nTARGET=hiby-r1\nSTATE=development-userspace-only\nFLASHABLE=no\n")
            for name in a.CORE:(apps/name).write_text(name)
            (root/"etc/init.d/S40network").write_text("inert fixture")
            with patch.object(a,"app_abi",return_value="Soft float"):
                result=a.stage(root,apps,"Soft float")
            self.assertFalse(result["flashable"])
            self.assertFalse(result["autostart_enabled"])
            self.assertTrue((root/"usr/bin/pocketos-shell").is_file())
            self.assertTrue((root/"etc/init.d/disabled-S40network").is_file())
            self.assertFalse((root/"etc/pocketos/autostart").exists())
            self.assertEqual(set(result["core"]),set(a.CORE))
    def test_report_never_enables_flash(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/"root";p.write_bytes(b"small")
            r=a.report(p,p,{"flashable":False},{"installation_allowed":False,"gates":[]})
            self.assertFalse(r["installation_allowed"])
            self.assertIsNone(r["physical_partition_capacity_bytes"])
            self.assertTrue(r["candidate_within_engineering_cap"])
if __name__=="__main__":unittest.main()
