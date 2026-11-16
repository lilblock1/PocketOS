import hashlib
import json
import shlex
import tempfile
import unittest
from pathlib import Path
from r1_reviewed_init import describe


class ReviewedInitTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.integration = {'flashable':False,'modules':{},'startup_evidence':{}}
        self.file('driver_default_init_script.sh','sh reserved.sh\nsh display.sh\n','startup_evidence')
        self.file('reserved.sh','a complex reviewed statement\n','startup_evidence')
        self.file('display.sh','insmod display.ko frame_num=2\n','startup_evidence')
        self.file('reserved.ko','reserved','modules',depends='')
        self.file('display.ko','display','modules',depends='reserved')
        self.recipes = {'apply_enabled':False,'recipes':{'reserved.sh':{
            'script_sha256':self.integration['startup_evidence']['reserved.sh']['sha256'],
            'actions':[{'kind':'module','module':'reserved.ko','parameters':['start=0']}],
        }}}

    def file(self,name,text,group,**extra):
        data=text.encode();(self.root/name).write_bytes(data)
        self.integration[group][name]={'sha256':hashlib.sha256(data).hexdigest(),**extra}

    def run_plan(self,cmdline='mem=64M@0x0 init=/linuxrc'):
        return describe(self.root,self.integration,self.recipes,cmdline)

    def test_preserves_dependency_order_without_enabling_apply(self):
        plan=self.run_plan()
        self.assertEqual([a['module'] for a in plan['steps']],['reserved.ko','display.ko'])
        self.assertFalse(plan['apply_enabled'])
        self.assertFalse(plan['cold_boot_verified'])

    def test_rejects_changed_source_even_with_updated_input_manifest(self):
        self.file('reserved.sh','changed source','startup_evidence')
        with self.assertRaisesRegex(ValueError,'recipe no longer matches'):self.run_plan()

    def test_rejects_unsupported_reserved_memory_and_shell_expansion(self):
        for cmdline in ('mem=32M@0x0','mem=64M@0x0 rmem=8M@0x3800000'):
            with self.assertRaises(ValueError):self.run_plan(cmdline)
        self.file('display.sh','insmod display.ko pin=$(touch /tmp/unwanted)\n','startup_evidence')
        with self.assertRaisesRegex(ValueError,'Unreviewed statement'):self.run_plan()
        self.assertFalse((self.root/'unwanted').exists())

    def test_rejects_dependency_reordering_and_modified_module(self):
        self.file('driver_default_init_script.sh','sh display.sh\nsh reserved.sh\n','startup_evidence')
        with self.assertRaisesRegex(ValueError,'dependency'):self.run_plan()
        (self.root/'display.ko').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError,'evidence differs'):self.run_plan()

    def test_pmic_kernel_arguments_retain_compound_values(self):
        path=Path(__file__).resolve().parents[1]/'hardware/hiby-r1/reviewed-init-recipes.json'
        params=json.loads(path.read_text())['recipes']['axp2101.sh']['actions'][0]['parameters']
        parsed=shlex.split(' '.join(params))
        self.assertEqual(len(parsed),18)
        values=dict(p.split('=',1) for p in parsed)
        self.assertEqual(values['charge_voltage_limit'],'4400')
        self.assertEqual(values['dcdc2'],'regulator_name=dcdc2 boot_on=enable work_voltage=1100000 suspend_volage=1100000')
        self.assertEqual(values['aldo2'],'regulator_name=aldo2 boot_on=disable work_voltage=3300000 suspend_volage=3300000')


if __name__=='__main__':unittest.main()
