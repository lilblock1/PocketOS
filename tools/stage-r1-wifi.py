#!/usr/bin/env python3
"""Stage R1 Wi-Fi into a disposable pre-firstboot tree, from a verified reference.
No redistribution, module execution, device access or firmware writing.
"""
import argparse
import json
import re
import shutil
from pathlib import Path
from r1_assemble import safe_parent, digest, regular

MODULES = {
    'brcmutil.ko': '72c61b9b294e766e15b35d5fdb874101ae5c10ecdc28074dd9c451545c09992a',
    'brcmfmac.ko': 'ee4d75e97ef4d17cb799c27a77ae9094668c446ca7ffc1225914dde1d0345713',
}
# Exact names advertised by the inspected R1 driver, not a guessed chip ID.
CHIPS = ('43143','43241b0','43241b4','43241b5','4329','4330','4334','43340','4335','43362','4339','43430','43455','4354')
CAP = 4 * 1024 * 1024


def stage(root, reference, manifest, repo):
    if root.is_symlink() or not root.is_dir() or root.resolve() == Path('/'):
        raise ValueError('Requires a disposable development root')
    identity = root/'etc/pocketos-release'
    if (root/'etc').is_symlink() or not regular(identity) or not {'NAME=PocketOS','TARGET=hiby-r1','STATE=development-userspace-only','FLASHABLE=no'}.issubset(identity.read_text().splitlines()):
        raise ValueError('Wrong rootfs identity')
    if (root/'etc/pocketos').is_symlink() or not regular(root/'etc/pocketos/firstboot-enable') or (root/'etc/pocketos/firstboot-enable').read_text() != 'POCKETOS-R1-FIRSTBOOT-1\n':
        raise ValueError('Prepare the reviewed board first')
    power = root/'module_driver/bcm_wlbt_power.ko'
    if (root/'module_driver').is_symlink() or not regular(power) or digest(power) != '3769fa5feee433083fd597acdc8ad53ebb6cac047d8d44bb2a9654be705b2888':
        raise ValueError('Missing verified board radio power module')
    if reference.is_symlink() or not reference.is_dir() or reference.resolve() == root.resolve() or reference.resolve() == Path('/'):
        raise ValueError('Reference must be a separate extracted tree')
    # The operator supplies the file hashes from a previously verified exact R1
    # reference image. No automatic trust of files from a writable SD card.
    if manifest.get('reference_commit') != '2920fe1c5ca3b4145b4d06251492ab87381e9817':
        raise ValueError('Wrong reference commit')
    firmware = manifest.get('firmware')
    if not isinstance(firmware, dict) or not firmware:
        raise ValueError('Missing reviewed reference firmware hashes')
    groups = {}
    for name, sha in firmware.items():
        match = re.fullmatch(r'brcmfmac('+'|'.join(CHIPS)+r')-sdio\.(bin|txt)',name)
        if not match or not isinstance(sha,str) or not re.fullmatch('[a-f0-9]{64}',sha):
            raise ValueError('Unexpected firmware name or hash')
        groups.setdefault(match[1],set()).add(match[2])
    if any(exts != {'bin','txt'} for exts in groups.values()):
        raise ValueError('Require matching firmware and board NVRAM pairs')
    records = {f'module_driver/{n}': h for n,h in MODULES.items()}
    records.update({f'lib/firmware/brcm/{n}': h for n,h in firmware.items()})
    size = 0
    for name, sha in records.items():
        src = reference/name
        if any((reference/Path(*Path(name).parts[:i])).is_symlink() for i in range(1,len(Path(name).parts))):
            raise ValueError('Symlink in reference path: '+name)
        if not regular(src) or digest(src) != sha:
            raise ValueError('Reference hash mismatch: '+name)
        size += src.stat().st_size
    if size > CAP:
        raise ValueError('Wi-Fi core exceeds 4 MiB engineering cap')
    scripts = {'usr/sbin/pocketos-wifi-service': (repo/'hardware/hiby-r1/pocketos-wifi-service.sh').read_bytes(),
               'etc/pocketos/wifi.sha256': ''.join(f'{sha}  /{name}\n' for name,sha in sorted(records.items())).encode(),
               'etc/pocketos/wifi-enable': b'POCKETOS-R1-WIFI-DEV-1\n'}
    # Check destinations before copying anything. The later board preparation
    # creates module_driver; integration must run this stage AFTER that step.
    for name in [*records,*scripts]:
        dest = root/name
        if dest.exists() or dest.is_symlink():
            raise ValueError('Refuse existing destination: '+name)
        for parent in dest.parents:
            if parent == root: break
            if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
                raise ValueError('Unsafe destination parent: '+name)
    for name in records:
        safe_parent(root,Path(name)); shutil.copyfile(reference/name,root/name); (root/name).chmod(0o644)
    for name,data in scripts.items():
        safe_parent(root,Path(name)); (root/name).write_bytes(data); (root/name).chmod(0o755 if name.startswith('usr/') else 0o600)
    return {'flashable':False,'installation_allowed':False,'physical_wifi_verified':False,'core_bytes':size,'files':records,'on_demand':True,'dhcp_enabled':False}


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('root','reference','manifest','report'): p.add_argument(name,type=Path)
    a=p.parse_args()
    a.report.write_text(json.dumps(stage(a.root,a.reference,json.loads(a.manifest.read_text()),Path(__file__).resolve().parents[1]),indent=2)+'\n')
