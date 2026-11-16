"""Regress the two R1 discovery failures against shared shell/runtime code."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

runtime, shell = sys.argv[1:]
sys.argv = sys.argv[:1]


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.battery('battery', 'cw201x', 79, 4176000)
        self.battery('axp_battery', 'axp2101-power-supply', 0, 0)
        self.put('proc/mounts', '/dev/mmcblk0p1 /mnt/sd_0 vfat rw 0 0\n')
        self.put('sys/class/block/mmcblk0p1/partition', '1\n')
        self.put('sys/block/mmcblk0/size', '243472384\n')
        self.put('sys/block/mmcblk0/removable', '0\n')

    def put(self, path, text):
        p = self.root / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        return p

    def battery(self, name, driver, percent, voltage):
        prefix = f'sys/class/power_supply/{name}'
        for key, value in {'type': 'Battery', 'present': '1', 'capacity': str(percent),
                           'voltage_now': str(voltage), 'status': 'Charging'}.items():
            self.put(prefix+'/'+key, value+'\n')
        link = self.root / prefix / 'device/driver'
        link.parent.mkdir(exist_ok=True)
        link.symlink_to('../../../bus/i2c/drivers/'+driver)

    def outputs(self):
        result = []
        for args in ([runtime, '--root', str(self.root), '--status'],
                     [shell, '--test-hardware', str(self.root)]):
            text = subprocess.check_output(args, text=True)
            result.append(dict(line.split('=', 1) for line in text.splitlines()))
        return result

    def expect(self, **values):
        for result in self.outputs():
            for key, value in values.items():
                self.assertEqual(result[key], str(value), (key, result))

    def test_real_r1_driver_pair_and_mounted_nonremovable_card(self):
        self.expect(battery_percent=79, battery_ambiguous=0, battery_source='cw201x', sd_mounted=1)
        r = self.outputs()[0]
        self.assertEqual(r['sd_present'], '1')
        self.assertEqual(r['removable_sd_candidate'], '0')

    def test_unknown_pair_stays_ambiguous(self):
        (self.root/'sys/class/power_supply/axp_battery/device/driver').unlink()
        self.expect(battery_percent=-1, battery_ambiguous=1)

    def test_bad_fuel_gauge_does_not_fall_back_to_pmic(self):
        self.put('sys/class/power_supply/battery/capacity', 'invalid\n')
        self.expect(battery_percent=-1, battery_source='cw201x')
        self.put('sys/class/power_supply/battery/capacity', '0\n')
        self.expect(battery_percent=0)  # Zero is valid, not a reason to choose PMIC.
        self.put('sys/class/power_supply/battery/present', '0\n')
        self.expect(battery_percent=-1)

    def test_missing_voltage_keeps_preferred_gauge_unknown(self):
        self.put('sys/class/power_supply/battery/voltage_now', '0\n')
        self.expect(battery_percent=-1)

    def test_unmounted_directory_is_not_a_card(self):
        (self.root/'mnt/sd_0').mkdir(parents=True)
        self.put('proc/mounts', '')
        self.expect(sd_mounted=0)

    def test_wrong_source_fs_and_duplicate_mount_rejected(self):
        for mount in ('/dev/sda1 /mnt/sd_0 vfat rw 0 0\n',
                      'tmpfs /mnt/sd_0 tmpfs rw 0 0\n',
                      '/dev/mmcblk0p1 /mnt/sd_0 ext4 rw 0 0\n',
                      '/dev/mmcblk0p1 /mnt/sd_0 vfat rw 0 0\n'*2):
            self.put('proc/mounts', mount)
            self.expect(sd_mounted=0)

    def test_partition_mapping_must_match(self):
        self.put('sys/class/block/mmcblk0p1/partition', '2\n')
        self.expect(sd_mounted=0)

    def test_r1_touch_calibration_reaches_back_button(self):
        prefix = 'sys/module/cst8xx_touch/parameters/'
        for key, value in {'cst_x_coords_min':0, 'cst_y_coords_min':0,
                           'cst_x_coords_max':480, 'cst_y_coords_max':800,
                           'cst_x_coords_flip':0, 'cst_y_coords_flip':0,
                           'cst_x_y_coords_exchange':0}.items():
            self.put(prefix+key, str(value)+'\n')
        def sample():
            text = subprocess.check_output([shell, '--test-touch', str(self.root)], text=True)
            return dict(line.split('=', 1) for line in text.splitlines())
        result = sample()
        self.assertEqual(result['corrected'], '1')
        self.assertGreaterEqual(int(result['back_y']), 287)
        self.assertEqual(result['outside'], '-1')
        self.put(prefix+'cst_x_y_coords_exchange', '1\n')
        self.assertEqual(sample()['corrected'], '0')


unittest.main()
