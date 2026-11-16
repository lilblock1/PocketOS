import os, socket, subprocess, sys, tempfile, threading, unittest
from pathlib import Path
BIN = str(Path(sys.argv.pop(1)).resolve())
class TestWifi(unittest.TestCase):
    def test_mock(self):
        with tempfile.TemporaryDirectory() as d:
            path = d + "/sock"
            messages = []
            listener = None
            failed_scan = False
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as server:
                server.bind(path); server.settimeout(.2)
                stop = threading.Event()
                def run():
                    nonlocal listener
                    while not stop.is_set():
                        try: cmd, peer = server.recvfrom(2048)
                        except socket.timeout: continue
                        messages.append(cmd)
                        reply = b"4\n" if cmd == b"ADD_NETWORK" else b"wpa_state=COMPLETED\n" if cmd == b"STATUS" else b"00:11:22:33:44:55\t2412\t-50\t[ESS]\tTest Network\n" if cmd == b"SCAN_RESULTS" else b"OK\n"
                        if cmd == b"DETACH":
                            listener = None
                            continue
                        if cmd == b"ATTACH": listener = peer
                        server.sendto(reply, peer)
                        if cmd == b"SCAN" and listener:
                            server.sendto(b"<3>CTRL-EVENT-SCAN-FAILED ret=-1" if failed_scan else b"<3>CTRL-EVENT-SCAN-RESULTS", listener)
                thread = threading.Thread(target=run); thread.start()
                try:
                    base=[BIN,"--socket",path]
                    self.assertNotEqual(subprocess.run(base+["scan"],capture_output=True).returncode,0)
                    self.assertIn(b"COMPLETED",subprocess.check_output(base+["status"]))
                    self.assertEqual(subprocess.run(base+["--allow-radio","scan"]).returncode,0)
                    self.assertIn(b"Test Network",subprocess.check_output(base+["--allow-radio","scan-list"],timeout=12))
                    failed_scan = True
                    bad = subprocess.run(base+["--allow-radio","scan-list"],capture_output=True,timeout=12)
                    self.assertNotEqual(bad.returncode,0)
                    self.assertIn(b"scan-incomplete",bad.stdout)
                    self.assertNotIn(b"Test Network",bad.stdout)
                    p=subprocess.run(base+["--allow-radio","connect"],input=b'Test Network\nsafe"pass\\word\n',capture_output=True)
                    self.assertEqual(p.returncode,0)
                    self.assertNotIn(b"safe",p.stdout+p.stderr)
                    self.assertIn(b'SET_NETWORK 4 ssid 54657374204e6574776f726b',messages)
                    self.assertIn(b'SET_NETWORK 4 psk "safe\\"pass\\\\word"',messages)
                    self.assertIn(b'SELECT_NETWORK 4',messages)
                finally:
                    stop.set();thread.join()
if __name__=="__main__": unittest.main()
