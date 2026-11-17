import pathlib, re, select, subprocess, sys, unittest
binary=str(pathlib.Path(sys.argv.pop(1)).resolve())

class AudioMonitor(unittest.TestCase):
    def test_version(self):
        self.assertEqual(subprocess.check_output([binary,"--version"],timeout=5),b"PocketOS audio monitor v1\n")
    def test_reject_unknown_options(self):
        for option in ["--device","--write","--set","--watch=1"]:
            with self.subTest(option=option):
                self.assertEqual(subprocess.run([binary,option],capture_output=True,timeout=5).returncode,2)
    def test_status_is_safe(self):
        output=subprocess.check_output([binary,"--status"],timeout=5)
        self.assertRegex(output,rb"^wired=(unknown|absent|ready)\n$")
    def test_watch_emits_initial_state(self):
        p=subprocess.Popen([binary,"--watch"],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            r,_,_=select.select([p.stdout],[],[],3)
            self.assertTrue(r,"watcher did not emit initial status")
            self.assertRegex(p.stdout.readline(),rb"^wired=(unknown|absent|ready)\n$")
        finally:
            p.terminate()
            try:p.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill();p.communicate(timeout=3)

if __name__=="__main__":
    unittest.main()
