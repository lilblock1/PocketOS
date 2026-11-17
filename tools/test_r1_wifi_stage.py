import hashlib
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('wifi_stage',Path(__file__).with_name('stage-r1-wifi.py'))
wifi=importlib.util.module_from_spec(spec)
spec.loader.exec_module(wifi)

class WifiStageTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base=Path(self.temp.name)
        self.root=base/'root'; self.root.mkdir()
        self.ref=base/'reference'; self.ref.mkdir()
        self.repo=Path(__file__).resolve().parents[1]
        self.put(self.root,'etc/pocketos-release',b'NAME=PocketOS\nTARGET=hiby-r1\nSTATE=development-userspace-only\nFLASHABLE=no\n')
        self.put(self.root,'etc/pocketos/firstboot-enable',b'POCKETOS-R1-FIRSTBOOT-1\n')
        self.put(self.root,'module_driver/bcm_wlbt_power.ko',b'power')
        self.modules={n:self.put(self.ref,'module_driver/'+n,n.encode()) for n in wifi.MODULES}
        self.manifest={'reference_commit':'2920fe1c5ca3b4145b4d06251492ab87381e9817','firmware':{n:self.put(self.ref,'lib/firmware/brcm/'+n,n.encode()) for n in ('brcmfmac43430-sdio.bin','brcmfmac43430-sdio.txt')}}
        self.real_digest=wifi.digest
    def put(self,root,name,data):
        p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
        return hashlib.sha256(data).hexdigest()
    def run_stage(self):
        def fixture_digest(p):
            if p==self.root/'module_driver/bcm_wlbt_power.ko' and p.read_bytes()==b'power':
                return '3769fa5feee433083fd597acdc8ad53ebb6cac047d8d44bb2a9654be705b2888'
            return self.real_digest(p)
        with patch.object(wifi,'MODULES',self.modules),patch.object(wifi,'digest',fixture_digest):
            return wifi.stage(self.root,self.ref,self.manifest,self.repo)
    def test_stage(self):
        result=self.run_stage();self.assertFalse(result['flashable']);self.assertFalse(result['physical_wifi_verified'])
        self.assertFalse(result['dhcp_enabled']);self.assertTrue(result['on_demand'])
        self.assertTrue((self.root/'usr/sbin/pocketos-wifi-service').is_file())
        self.assertEqual((self.root/'etc/pocketos/wifi-enable').read_text(),'POCKETOS-R1-WIFI-DEV-1\n')
        self.assertEqual(len(result['files']),4)
    def test_reject_incomplete_pair(self):
        del self.manifest['firmware']['brcmfmac43430-sdio.txt']
        with self.assertRaisesRegex(ValueError,'pairs'):self.run_stage()
        self.assertFalse((self.root/'etc/pocketos/wifi-enable').exists())
    def test_reject_corruption(self):
        (self.ref/'module_driver/brcmfmac.ko').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError,'hash mismatch'):self.run_stage()
        self.assertFalse((self.root/'module_driver/brcmutil.ko').exists())
    def test_reject_existing_destination(self):
        self.put(self.root,'lib/firmware/brcm/brcmfmac43430-sdio.bin',b'keep')
        with self.assertRaisesRegex(ValueError,'existing destination'):self.run_stage()
        self.assertEqual((self.root/'lib/firmware/brcm/brcmfmac43430-sdio.bin').read_bytes(),b'keep')
    def test_reject_missing_marker(self):
        (self.root/'etc/pocketos/firstboot-enable').unlink()
        with self.assertRaisesRegex(ValueError,'reviewed board'):self.run_stage()

if __name__=='__main__':unittest.main()
