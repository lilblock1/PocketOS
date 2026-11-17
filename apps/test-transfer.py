"""Host-only integration tests for the bounded, SD-only PocketOS HTTP server."""
import http.client
import json
import pathlib
import re
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = str(pathlib.Path(sys.argv.pop(1)).resolve())
PORT = 8765

class Transfer(unittest.TestCase):
    def test_browser_transfer_and_protections(self):
        self.assertEqual(subprocess.check_output([BINARY, "--version"]).strip(),
                         b"PocketOS Transfer v1")
        with tempfile.TemporaryDirectory() as work:
            root = pathlib.Path(work)/"sd"
            root.mkdir()
            for folder in ("Music", "Books", "Documents", "Downloads"):
                (root/folder).mkdir()
            (root/"PocketOS-updates").mkdir()
            (root/"PocketOS-updates"/"protected.txt").write_text("preserve")
            outside = pathlib.Path(work)/"private.txt"
            outside.write_text("PRIVATE")
            (root/"Music"/"bad-link").symlink_to(outside)
            p=subprocess.Popen([BINARY, "--fixture-root", str(root)],
                               stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                               text=True,bufsize=1)
            conn=None
            try:
                url=p.stdout.readline().strip()
                code=p.stdout.readline().strip()
                self.assertEqual(url, "http://127.0.0.1:8765")
                self.assertRegex(code, r"^\d{8}$")
                conn=http.client.HTTPConnection("127.0.0.1",PORT,timeout=5)
                def request(method,path,body=None,key=code):
                    headers={"X-PocketOS-Key":key}
                    if body is not None: headers["Content-Length"]=str(len(body))
                    conn.request(method,path,body,headers=headers)
                    r=conn.getresponse()
                    return r.status,r.read()
                status,body=request("GET","/",key="")
                self.assertEqual(status,200)
                self.assertIn(b"PocketOS Wireless File Transfer",body)
                status,body=request("GET","/list/Music")
                self.assertEqual(status,200)
                self.assertEqual(json.loads(body),[])
                payload=b"PocketOS sample audio data\x00"*100
                status,body=request("PUT","/file/Music/My%20song.flac",payload)
                self.assertEqual(status,201,body)
                self.assertEqual((root/"Music"/"My song.flac").read_bytes(),payload)
                status,body=request("GET","/file/Music/My%20song.flac")
                self.assertEqual((status,body),(200,payload))
                status,body=request("GET","/list/Music")
                self.assertEqual(status,200)
                self.assertEqual(json.loads(body),["My song.flac"])
                status,body=request("PUT","/file/Music/My%20song.flac",b"replace")
                self.assertEqual(status,409)
                self.assertEqual((root/"Music"/"My song.flac").read_bytes(),payload)
                for path in ("/file/Music/..", "/file/Music/%2e%2e",
                             "/file/Music/%2fetc", "/file/PocketOS-updates/bad",
                             "/file/Music/.hidden", "/file/Music/name%5cfile"):
                    with self.subTest(path=path):
                        status,body=request("PUT",path,b"bad")
                        self.assertNotEqual(status,201)
                status,body=request("GET","/file/Music/bad-link")
                self.assertEqual(status,404)
                status,body=request("GET","/list/Downloads")
                self.assertEqual((status,json.loads(body)),(200,[]))
                self.assertEqual(outside.read_text(),"PRIVATE")
                self.assertEqual((root/"PocketOS-updates"/"protected.txt").read_text(),"preserve")
                status,body=request("GET","/list/Music",key="11111111" if code!="11111111" else "22222222")
                self.assertEqual(status,403)
            finally:
                if conn:conn.close()
                p.terminate()
                try:p.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    p.kill();p.communicate(timeout=3)
    def test_no_arbitrary_root_in_production(self):
        p=subprocess.run([BINARY,"--fixture-root","relative"],
                         capture_output=True,timeout=3)
        self.assertEqual(p.returncode,2)

if __name__=="__main__": unittest.main()
