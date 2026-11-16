import copy
import hashlib
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('firstboot',Path(__file__).with_name('prepare-r1-firstboot.py'))
firstboot=importlib.util.module_from_spec(spec)
spec.loader.exec_module(firstboot)


class FirstbootTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.root=self.base/'root';self.drivers=self.base/'drivers'
        self.drivers.mkdir()
        for path in ('etc/pocketos','usr/bin','usr/sbin'):(self.root/path).mkdir(parents=True,exist_ok=True)
        (self.root/'etc/pocketos-release').write_text('NAME=PocketOS\nTARGET=hiby-r1\nSTATE=development-userspace-only\nFLASHABLE=no\n')
        (self.root/'etc/pocketos/adb-enable').write_text('POCKETOS-USB-ADB-DEV-1\n')
        (self.root/'etc/mdev.conf').write_text('null root:root 666\n$MODALIAS=.* root:root 660 @modprobe "$MODALIAS"\n')
        (self.root/'usr/bin/adbd').write_bytes(b'daemon fixture')
        (self.root/'usr/bin/pocketos-stat').write_bytes(b'metadata fixture')
        steps=[]
        for i in range(26):
            name=f'module{i}.ko';data=name.encode();(self.drivers/name).write_bytes(data)
            steps.append({'kind':'module','module':name,'parameters':[],'sha256':hashlib.sha256(data).hexdigest()})
        self.plan={'source_review_completed':True,'apply_enabled':False,'module_load_count':26,'steps':steps}
        self.repo=Path(__file__).resolve().parents[1]

    def prepare(self,plan=None):return firstboot.prepare(self.root,self.drivers,plan or self.plan,self.repo)

    def test_stages_only_verified_module_bytes_and_no_vendor_scripts(self):
        report=self.prepare()
        self.assertFalse(report['installation_allowed'])
        self.assertRegex(report['build_id'],r'^[0-9a-f]{16}$')
        self.assertIn('BUILD_ID='+report['build_id'],(self.root/'etc/pocketos-release').read_text().splitlines())
        self.assertEqual(report['firstboot_files']['etc/pocketos-release'],hashlib.sha256((self.root/'etc/pocketos-release').read_bytes()).hexdigest())
        self.assertEqual(len(list((self.root/'module_driver').iterdir())),26)
        self.assertFalse(list((self.root/'module_driver').glob('*.sh')))
        self.assertEqual((self.root/'etc/pocketos/autostart').read_text(),'POCKETOS-DEV-AUTOSTART-1 auto\n')
        self.assertNotIn('$MODALIAS=',(self.root/'etc/mdev.conf').read_text())
        subprocess.run(['sh','-n',str(self.root/'usr/sbin/pocketos-r1-board-init')],check=True)

    def test_corrupt_module_rejected_before_tree_mutation(self):
        (self.drivers/'module25.ko').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'digest differs'):self.prepare()
        self.assertFalse((self.root/'module_driver').exists())
        self.assertFalse((self.root/'etc/pocketos/autostart').exists())

    def test_incomplete_duplicate_or_unreviewed_actions_rejected(self):
        incomplete=copy.deepcopy(self.plan);incomplete['steps'].pop()
        duplicate=copy.deepcopy(self.plan);duplicate['steps'][-1]=duplicate['steps'][0]
        unreviewed=copy.deepcopy(self.plan);unreviewed['steps'].append({'kind':'sysfs','path':'/dev/mtd0','value':'bad'})
        for plan in (incomplete,duplicate,unreviewed):
            with self.assertRaises(ValueError):self.prepare(plan)
            self.assertFalse((self.root/'module_driver').exists())

    def test_symlink_mount_parent_cannot_escape_candidate(self):
        outside=self.base/'outside';outside.mkdir();(self.root/'mnt').symlink_to(outside)
        with self.assertRaisesRegex(ValueError,'symlink'):self.prepare()
        self.assertFalse((outside/'sd_0').exists())


if __name__=='__main__':unittest.main()
