import os, subprocess, tempfile, unittest, sys
from pathlib import Path
SCRIPT = str(Path(sys.argv.pop(1)).resolve())

class TestBluetooth(unittest.TestCase):
    def test_preflight(self):
        with tempfile.TemporaryDirectory() as d:
            fake = Path(d)/'bluetoothctl'
            fake.write_text('''#!/bin/sh
printf '%s\\n' "$*" >> "$TEST_LOG"
case "$3" in
 show) case "$TEST_MODE" in unavailable) exit 1;; missing) echo 'No default controller available';; off) printf 'Controller AA:BB:CC:DD:EE:FF\\nPowered: no\\n';; *) printf 'Controller AA:BB:CC:DD:EE:FF\\nPowered: yes\\n';; esac;;
 scan) [ "$TEST_MODE" != scan-fail ] || [ "$4" = off ];;
 devices) echo 'Device AA:BB:CC:DD:EE:FF Test Headphones';;
 *) exit 1;;
esac
''')
            fake.chmod(0o755)
            log=Path(d)/'calls'
            def run(mode,*args):
                env=dict(os.environ,PATH=d+':'+os.environ['PATH'],TEST_MODE=mode,TEST_LOG=str(log))
                return subprocess.run(['sh',SCRIPT,*args],env=env,capture_output=True,timeout=5)
            for mode,error in [('unavailable',b'bt-service'),('missing',b'bt-controller'),('off',b'bt-power')]:
                r=run(mode,'--allow-radio','discover');self.assertNotEqual(r.returncode,0);self.assertIn(error,r.stdout)
            self.assertNotIn('scan on',log.read_text())
            self.assertNotEqual(run('on','discover').returncode,0)
            self.assertIn(b'Powered: no',run('off','status').stdout)
            r=run('scan-fail','--allow-radio','discover');self.assertNotEqual(r.returncode,0)
            r=run('on','--allow-radio','discover');self.assertEqual(r.returncode,0);self.assertIn(b'Test Headphones',r.stdout)

if __name__=='__main__': unittest.main()
