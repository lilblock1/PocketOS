import os, socket, subprocess, sys, tempfile, threading, unittest
from pathlib import Path
BIN = str(Path(sys.argv.pop(1)).resolve())
class TestWifi(unittest.TestCase):
    def test_mock(self):
        with tempfile.TemporaryDirectory() as d:
            path = d + "/sock"
            messages = []
            with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as server:
                server.bind(path); server.settimeout(.2)
                stop = threading.Event()
                def run():
                    while not stop.is_set():
                        try: cmd, peer = server.recvfrom(2048)
                        except socket.timeout: continue
                        messages.append(cmd)
                        reply = b"4\n" if cmd == b"ADD_NETWORK" else b"wpa_state=COMPLETED\n" if cmd == b"STATUS" else b"OK\n"
                        server.sendto(reply, peer)
                thread = threading.Thread(target=run); thread.start()
                try:
                    base=[BIN,"--socket",path]
                    self.assertNotEqual(subprocess.run(base+["scan"],capture_output=True).returncode,0)
                    self.assertIn(b"COMPLETED",subprocess.check_output(base+["status"]))
                    self.assertEqual(subprocess.run(base+["--allow-radio","scan"]).returncode,0)
                    p=subprocess.run(base+["--allow-radio","connect"],input=b'Test Network\nsafe"pass\\word\n',capture_output=True)
                    self.assertEqual(p.returncode,0)
                    self.assertNotIn(b"safe",p.stdout+p.stderr)
                    self.assertIn(b'SET_NETWORK 4 ssid 54657374204e6574776f726b',messages)
                    self.assertIn(b'SET_NETWORK 4 psk "safe\\"pass\\\\word"',messages)
                    self.assertIn(b'SELECT_NETWORK 4',messages)
                finally:
                    stop.set();thread.join()
if __name__=="__main__": unittest.main()
