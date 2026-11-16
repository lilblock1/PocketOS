#!/usr/bin/env python3
"""Add a verified ADB package to an offline development rootfs, never a device."""
import argparse
import json
import shutil
from pathlib import Path
from r1_assemble import validate_root, safe_parent, digest

p = argparse.ArgumentParser()
p.add_argument('root', type=Path)
p.add_argument('package', type=Path)
p.add_argument('report', type=Path)
a = p.parse_args()
root = a.root.resolve()
validate_root(root)
manifest = json.loads((a.package/'adb-manifest.json').read_text())
if manifest.get('flashable') is not False or manifest.get('tcp_fallback') is not False or manifest.get('test_binary_included') is not False:
    raise SystemExit('Invalid development ADB package')
locations = {'adbd':'usr/bin/adbd', 'libcrypt.so.2.0.0':'usr/lib/libcrypt.so.2.0.0',
             'pocketos-usb-adb':'usr/sbin/pocketos-usb-adb'}
for name, dest in locations.items():
    source = a.package/name
    if source.is_symlink() or not source.is_file() or digest(source) != manifest['files'][name]['sha256']:
        raise SystemExit('ADB input digest mismatch: '+name)
    target = root/dest
    safe_parent(root, Path(dest))
    if target.exists() or target.is_symlink():
        raise SystemExit('Unexpected existing ADB destination: '+dest)
    shutil.copyfile(source, target)
    target.chmod(0o755)
link = root/'usr/lib/libcrypt.so.2'
if link.exists() or link.is_symlink():
    raise SystemExit('Unexpected libcrypt SONAME destination')
link.symlink_to('libcrypt.so.2.0.0')
for name, text in {
    'etc/init.d/S45pocketos-adbd':'#!/bin/sh\ncase "$1" in start) exec /usr/sbin/pocketos-usb-adb start;; *) exit 0;; esac\n',
    'etc/pocketos/adb-enable':'POCKETOS-USB-ADB-DEV-1\n',
}.items():
    dest = Path(name);safe_parent(root, dest);target = root/dest
    if target.exists() or target.is_symlink():
        raise SystemExit('Unexpected activation destination')
    target.write_text(text)
    target.chmod(0o755 if name.startswith('etc/init.d/') else 0o600)
result = {'phase':'5C', 'flashable':False, 'installation_allowed':False,
          'usb_hardware_verified':False, 'firmware_boot_verified':False,
          'adb_staged':True, 'usb_only':True, 'development_enable_marker':True,
          'test_binary_included':False, 'package':manifest,
          'notes':['Offline rootfs staging only. The current R1 ADB connection was not changed.',
                   'USB gadget startup requires hardware validation after independent board init.']}
a.report.write_text(json.dumps(result, indent=2)+'\n')
print('USB-only ADB staged in independent development rootfs; boot and physical USB unverified.')
