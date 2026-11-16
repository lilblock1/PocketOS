import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BIN = str(Path(sys.argv.pop(1)).resolve())

class TimeoutTests(unittest.TestCase):
    def test_exit_status(self):
        self.assertEqual(subprocess.run([BIN, '2', sys.executable, '-c', 'raise SystemExit(7)']).returncode, 7)

    def test_kills_ignoring_descendant(self):
        with tempfile.TemporaryDirectory() as temp:
            pidfile = Path(temp)/'pid'
            descendant = 'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(20)'
            parent = 'import subprocess,sys,time; p=subprocess.Popen([sys.executable,"-c",sys.argv[2]]); open(sys.argv[1],"w").write(str(p.pid)); time.sleep(20)'
            began = time.monotonic()
            result = subprocess.run([BIN, '1', sys.executable, '-c', parent, str(pidfile), descendant], timeout=4)
            self.assertEqual(result.returncode, 124)
            self.assertLess(time.monotonic()-began, 3)
            pid = int(pidfile.read_text())
            for _ in range(30):
                stat = Path(f'/proc/{pid}/stat')
                if not stat.exists() or stat.read_text().split()[2] == 'Z': break
                time.sleep(.02)
            else:
                os.kill(pid, signal.SIGKILL)
                self.fail('descendant survived timeout')

if __name__ == '__main__': unittest.main()
