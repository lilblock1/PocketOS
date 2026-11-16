import pathlib, subprocess, sys, tempfile, unittest, zipfile
SCRIPT=str(pathlib.Path(sys.argv.pop(1)).resolve())
class Books(unittest.TestCase):
    def test_prepare(self):
        with tempfile.TemporaryDirectory() as d:
            root=pathlib.Path(d)
            book=root/"novel.epub"
            with zipfile.ZipFile(book,"w") as z:
                z.writestr("META-INF/container.xml",'<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>')
                z.writestr("OPS/book.opf",'<package><manifest><item id="one" href="first.xhtml"/><item id="two" href="second.xhtml"/></manifest><spine><itemref idref="two"/><itemref idref="one"/></spine></package>')
                z.writestr("OPS/first.xhtml","<html><body><p>First</p></body></html>")
                z.writestr("OPS/second.xhtml","<html><body><p>Second</p><script>hidden</script></body></html>")
            f=root/"notes.html";f.write_text("<p>Hello &amp; goodbye</p>")
            subprocess.run([sys.executable,SCRIPT,d],check=True)
            self.assertEqual((root/"novel.epub.pocket.txt").read_text(),"Second\n\nFirst\n")
            self.assertEqual((root/"notes.html.pocket.txt").read_text(),"Hello & goodbye\n")
            locked=root/"locked.epub"
            with zipfile.ZipFile(locked,"w") as z:z.writestr("META-INF/encryption.xml","encrypted")
            (root/"loan.lcpl").write_text("{}")
            result=subprocess.run([sys.executable,SCRIPT,d],capture_output=True)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse((root/"locked.epub.pocket.txt").exists())
            self.assertFalse((root/"loan.lcpl.pocket.txt").exists())
if __name__=="__main__":unittest.main()
