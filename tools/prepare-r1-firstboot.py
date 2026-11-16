#!/usr/bin/env python3
"""Prepare a disposable offline first-boot candidate; no device access or writer."""
import argparse
import json
import shlex
import secrets
import shutil
from pathlib import Path
from r1_assemble import validate_root, safe_parent, digest


def board_script(plan):
    lines = ['#!/bin/sh','set -eu','PATH=/usr/bin:/bin:/usr/sbin:/sbin','export PATH',
             'case "${1:-}" in start|--check) ;; *) exit 2;; esac',
             "grep -Fxq NAME=PocketOS /etc/pocketos-release",
             "grep -Fxq POCKETOS-R1-FIRSTBOOT-1 /etc/pocketos/firstboot-enable",
             '[ "$(id -u)" -eq 0 ]',
             'sha256sum -c /etc/pocketos/board.sha256',
             '[ "$1" != --check ] || { echo "Board file identities verified; no modules loaded"; exit 0; }',
             '[ "$(uname -r)" = 4.4.94-sonix ]',
             "grep -Eq '(^| )mem=64M@0x0( |$)' /proc/cmdline",
             "if grep -Eq '(^| )rmem=' /proc/cmdline; then exit 1; fi",
             '[ ! -e /sys/module/utils ] || { echo "Refusing to initialize an already-running board" >&2; exit 1; }']
    allowed_sysfs = {'/sys/module/i2c_gpio_add/parameters/i2c_bus',
                     '/sys/module/keyboard_gpio_add/parameters/keyboard'}
    for step in plan['steps']:
        if step['kind'] == 'module':
            lines.append(' '.join(shlex.quote(s) for s in ['/sbin/insmod','/module_driver/'+step['module'],*step['parameters']]))
        elif step['kind'] == 'sysfs' and step['path'] in allowed_sysfs:
            lines.append("printf '%s\\n' "+shlex.quote(step['value'])+' > '+shlex.quote(step['path']))
        elif step['kind'] == 'delay' and step['seconds'] == '0.01':
            lines.append('sleep 0.01')
        else:
            raise ValueError('Unrecognized reviewed action')
    lines.extend(["echo 'R1 board initialization completed'",''])
    return '\n'.join(lines)


def prepare(root, drivers, plan, repo):
    validate_root(root)
    if plan.get('source_review_completed') is not True or plan.get('module_load_count') != 26 or plan.get('apply_enabled') is not False:
        raise ValueError('Requires the complete disabled reviewed plan')
    if (root/'module_driver').exists():
        raise ValueError('Use a fresh candidate root')
    if not (root/'usr/bin/adbd').is_file() or not (root/'etc/pocketos/adb-enable').is_file():
        raise ValueError('Stage the USB-only ADB package first')
    if not (root/'usr/bin/pocketos-stat').is_file():
        raise ValueError('Stage the required PocketOS metadata helper first')
    records = {}
    for step in plan['steps']:
        if step['kind'] != 'module':continue
        name = step['module']
        if Path(name).name != name or not name.endswith('.ko'):raise ValueError('Invalid module name')
        source = drivers/name
        if source.is_symlink() or not source.is_file() or digest(source) != step['sha256']:
            raise ValueError('Module digest differs: '+name)
        if name in records:
            raise ValueError('Duplicate module in reviewed plan')
        records[name] = step['sha256']
    if len(records) != 26:
        raise ValueError('Incomplete reviewed module sequence')
    # Validate the full action set before making the offline tree concrete.
    script = board_script(plan)
    for name in ('proc','sys','dev','tmp','run','mnt/sd_0'):
        path=Path(name)
        safe_parent(root,path/'placeholder')
        if (root/path).is_symlink() or not (root/path).is_dir():
            raise ValueError('Unsafe first-boot mount point')
    (root/'module_driver').mkdir()
    for name in records:
        shutil.copyfile(drivers/name,root/'module_driver'/name)
        (root/'module_driver'/name).chmod(0o644)
    files = {
        'usr/sbin/pocketos-r1-board-init':script,
        'usr/sbin/pocketos-r1-boot':(repo/'hardware/hiby-r1/pocketos-r1-boot.sh').read_text(),
        'etc/pocketos/board.sha256':''.join(f'{sha}  /module_driver/{name}\n' for name,sha in sorted(records.items())),
        'etc/pocketos/firstboot-enable':'POCKETOS-R1-FIRSTBOOT-1\n',
        'etc/pocketos/autostart':'POCKETOS-DEV-AUTOSTART-1 auto\n',
        'etc/init.d/rcS':'#!/bin/sh\nexec /usr/sbin/pocketos-r1-boot start\n',
        'etc/inittab':'# First-boot development userspace; keep existing kernel and recovery.\n'
                       '::sysinit:/etc/init.d/rcS\n'
                       'console::askfirst:/bin/sh\n'
                       '::shutdown:/bin/umount -a -r\n',
        'etc/fstab':'# Root stays read-only. Remaining volatile mounts are handled by pocketos-r1-boot.\n'
                    '/dev/root / squashfs ro,noauto 0 0\n',
    }
    for name, text in files.items():
        dest=Path(name);safe_parent(root,dest)
        target=root/dest
        if target.is_symlink():raise ValueError('Unsafe candidate destination')
        target.write_text(text)
        target.chmod(0o755 if name.startswith(('usr/sbin/','etc/init.d/')) else
                     0o600 if name.endswith(('autostart','firstboot-enable')) else 0o644)
    (root/'etc/pocketos').chmod(0o755)
    # Unique identifier for this freshly prepared physical candidate, independent
    # of the public version. A booted ID can be compared to the candidate report.
    release = root/'etc/pocketos-release'
    build_id = secrets.token_hex(8)
    release_text = [line for line in release.read_text().splitlines() if not line.startswith('BUILD_ID=')]
    release.write_text('\n'.join(release_text)+f'\nBUILD_ID={build_id}\n')
    # All module loads use the explicit verified sequence. Disable modalias autoload.
    mdev=root/'etc/mdev.conf'
    if mdev.is_symlink():raise ValueError('Unsafe mdev configuration')
    mdev.write_text('\n'.join(s for s in mdev.read_text().splitlines() if not s.lstrip().startswith('$MODALIAS='))+'\n')
    return {'phase':'5D','boot_candidate':True,'installation_allowed':False,
            'cold_boot_verified':False,'recovery_restore_verified':False,
            'primary_target':'/dev/mtd2 (rootfs)','kernel_reused':True,
            'preserve_mtd':[0,1,3,4,5,6], 'board_modules':records,
            'autostart_input':'auto (unique hyn_ts)','sd_firstboot_read_only':True,
            'radio_daemons_enabled':False,'userdata_mount_or_format':False,
            'build_id':build_id,
            'firstboot_files':{name:digest(root/name) for name in [*files,'etc/pocketos-release']},
            'notes':['Offline preparation only; does not create UPT or access a device.',
                     'No bootloader, kernel, recovery, boot-selection or userdata write.',
                     'New USB ADB starts before board initialization.',
                     'A failed board sequence leaves USB ADB and RAM logs for diagnostics.']}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('root','drivers','plan','report'):p.add_argument(name,type=Path)
    a=p.parse_args();root=a.root.resolve()
    if root == Path('/'):raise SystemExit('Never prepare the host root')
    result=prepare(root,a.drivers,json.loads(a.plan.read_text()),Path(__file__).resolve().parents[1])
    a.report.write_text(json.dumps(result,indent=2)+'\n')
    print('Offline first-boot root prepared; installation requires final candidate review.')


if __name__=='__main__':main()
