import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path
from r1_init_plan import build_plan, render

class InitPlanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.evidence = self.root / "reference-startup-evidence"
        self.evidence.mkdir()
        self.moddir = self.root / "rootfs/module_driver"
        self.moddir.mkdir(parents=True)
        self.state = {"modules": {}, "startup_evidence": {}, "kernel": {"sha256": "0"*64},
                      "subsystems": {}, "blocked": False}
        self.script("driver_default_init_script.sh", "sh first.sh\nsh second.sh\n")
        self.script("first.sh", "insmod first.ko bus=1 \\\n gpio=PA17\n")
        self.script("second.sh", "insmod second.ko level=0\n")
        self.module("first.ko", "")
        self.module("second.ko", "first")

    def script(self, name, content):
        path = self.evidence / name
        path.write_text(content)
        self.state["startup_evidence"][name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest()}

    def module(self, name, deps):
        path = self.moddir / name
        path.write_bytes(name.encode())
        self.state["modules"][name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                                      "vermagic": "4.4-test MIPS", "depends": deps}

    def test_order_parameters_and_disabled_execution(self):
        plan = build_plan(self.root, self.state)
        self.assertEqual(plan["blockers"], [])
        self.assertEqual(plan["planned_loads"][0]["parameters"], ["bus=1", "gpio=PA17"])
        path = self.root / "draft"
        path.write_text(render(plan))
        subprocess.run(["sh", "-n", str(path)], check=True)
        dry = subprocess.run(["sh", str(path), "--plan"], capture_output=True, text=True, check=True)
        self.assertEqual(dry.stdout.splitlines(), ["first.ko", "second.ko"])
        apply = subprocess.run(["sh", str(path), "--apply"], capture_output=True, text=True)
        self.assertEqual(apply.returncode, 2)

    def test_shell_statement_is_not_executed(self):
        self.script("first.sh", "insmod first.ko pin=$(touch /tmp/never)\n")
        plan = build_plan(self.root, self.state)
        self.assertTrue(plan["blockers"])
        self.assertEqual([c["module"] for c in plan["planned_loads"]], ["second.ko"])
        self.assertNotIn("touch", render(plan))

    def test_detect_modified_module(self):
        (self.moddir / "first.ko").write_bytes(b"bad")
        self.assertTrue(build_plan(self.root, self.state)["blockers"])

    def test_dependency_order(self):
        self.script("driver_default_init_script.sh", "sh second.sh\nsh first.sh\n")
        plan = build_plan(self.root, self.state)
        self.assertTrue(any(b.get("dependency") == "first" for b in plan["blockers"]))

if __name__ == "__main__":
    unittest.main()
