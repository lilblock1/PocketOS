#!/usr/bin/env python3
"""Stage verified, already built radio assets into an offline R1 candidate."""
import argparse
import json
import shutil
from pathlib import Path
from r1_assemble import app_abi, digest, regular, safe_parent

ASSETS = (
    'bin/brcm_patchram_plus', 'bin/bluetoothd', 'bin/bluealsa', 'bin/bluealsa-cli',
    'lib/libsbc.so.1', 'lib/alsa-lib/libasound_module_pcm_bluealsa.so',
    'modules/brcmutil.ko', 'modules/brcmfmac.ko',
    'firmware/brcm/brcmfmac43430-sdio.bin', 'firmware/brcm/brcmfmac43430-sdio.txt',
    'firmware/bt/BCM4343A1_001.002.009.0122.0538.hcd',
    'config/bluealsa.conf', 'config/20-bluealsa.conf',
)
STOCK = {
    'bin/brcm_patchram_plus': '0ffb26e4bb63a0abf8bde55e1f0f1cbcf379cc0c4a7e8ab6d0dd89c7e0e2200d',
    'modules/brcmutil.ko': '72c61b9b294e766e15b35d5fdb874101ae5c10ecdc28074dd9c451545c09992a',
    'modules/brcmfmac.ko': 'ee4d75e97ef4d17cb799c27a77ae9094668c446ca7ffc1225914dde1d0345713',
    'firmware/brcm/brcmfmac43430-sdio.bin': '37575bf1f36011c187adab4961fd3c96c9976f86f1211890826734a0cbeb6674',
    'firmware/brcm/brcmfmac43430-sdio.txt': 'daf054a00f1ba1fee4f7167f939989adb18b0da785523c89e65244a1510bebc5',
    'firmware/bt/BCM4343A1_001.002.009.0122.0538.hcd': '9873217acdeaee78bd502a31fa876dc1b120c9d07ca8c16420853bc44ba64972',
}

def stage(root, package, repo):
    if root.is_symlink() or not root.is_dir() or root.resolve() == Path('/'):
        raise ValueError('Requires an offline candidate')
    config = root/'etc/pocketos'
    if config.is_symlink() or not regular(config/'firstboot-enable'):
        raise ValueError('Prepare reviewed first boot first')
    release = root/'etc/pocketos-release'
    if not regular(release) or not {'NAME=PocketOS','TARGET=hiby-r1','FLASHABLE=no'}.issubset(release.read_text().splitlines()):
        raise ValueError('Wrong candidate identity')
    expected = app_abi(root/'usr/bin/pocketos-shell')
    records = {}
    for name in ASSETS:
        source = package/name
        if not regular(source) or any(p.is_symlink() for p in source.parents if p != package.parent):
            raise ValueError('Unsafe radio source: '+name)
        sha = digest(source)
        if name in STOCK and sha != STOCK[name]:
            raise ValueError('Reference radio digest differs: '+name)
        if name.startswith(('bin/','lib/')) and app_abi(source) != expected:
            raise ValueError('Radio ABI differs: '+name)
        records[name] = {'sha256':sha,'bytes':source.stat().st_size}
    if sum(record['bytes'] for record in records.values()) > 4*1024*1024:
        raise ValueError('Core radio assets exceed 4 MiB budget')
    destbase = Path('usr/lib/pocketos/radio')
    scripts = {
        'usr/sbin/pocketos-radio-service':'pocketos-radio-service.sh',
        'usr/sbin/pocketos-network-event':'pocketos-network-event.sh',
        'usr/sbin/pocketos-udhcpc':'pocketos-udhcpc.sh',
        'usr/sbin/pocketos-state-service':'pocketos-state-service.sh',
    }
    additions = {
        'etc/pocketos/radio-enable': b'POCKETOS-R1-RADIO-1\n',
        'etc/pocketos/state-enable': b'POCKETOS-R1-STATE-1\n',
        'etc/pocketos/radio.sha256': ''.join(f"{r['sha256']}  /{destbase}/{name}\n" for name,r in sorted(records.items())).encode(),
        'usr/share/dbus-1/system.d/bluealsa.conf': (package/'config/bluealsa.conf').read_bytes(),
    }
    for path in [*[str(destbase/name) for name in ASSETS],*scripts,*additions]:
        target=root/path
        if target.exists() or target.is_symlink():raise ValueError('Unexpected candidate destination: '+path)
    for name in ASSETS:
        dest=destbase/name;safe_parent(root,dest);shutil.copyfile(package/name,root/dest)
        (root/dest).chmod(0o755 if name.startswith(('bin/','lib/')) else 0o644)
    for dest,source in scripts.items():
        safe_parent(root,Path(dest));shutil.copyfile(repo/'hardware/hiby-r1'/source,root/dest);(root/dest).chmod(0o755)
    for dest,data in additions.items():
        safe_parent(root,Path(dest));(root/dest).write_bytes(data)
        (root/dest).chmod(0o400 if dest.startswith('etc/pocketos/') else 0o644)
    return {'kernel_reused':True,'recovery_payload':False,'on_device_updates_enabled':False,
            'radio_assets':records,'cold_boot_verified':False,'bluetooth_reconnect_verified':False}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('root','package','report'):parser.add_argument(name,type=Path)
    args=parser.parse_args()
    result=stage(args.root,args.package,Path(__file__).resolve().parents[1])
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print('Radio assets staged offline; no device access or recovery changes.')
