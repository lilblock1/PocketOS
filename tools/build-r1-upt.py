#!/usr/bin/env python3
"""Package a reviewed R1 rootfs-only local update. Never installs or arms recovery."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path


def package(image, candidate, work, output):
    if work.exists() or output.exists():raise ValueError('Use new work and output paths')
    data=image.read_bytes()
    if data[:4]!=b'hsqs' or struct.unpack_from('<H',data,20)[0]!=3:
        raise ValueError('Requires the hardware-tested LZO squashfs format')
    if len(data)>47185920 or len(data)%2048:raise ValueError('Invalid R1 primary-rootfs size/alignment')
    if hashlib.sha256(data).hexdigest()!=candidate['image']['sha256']:
        raise ValueError('Candidate digest differs')
    if candidate.get('primary_target')!='/dev/mtd2 (rootfs)' or not candidate.get('kernel_reused'):
        raise ValueError('Requires the primary-rootfs-only R1 candidate')
    work.mkdir(parents=True)
    ota=work/'ota_v0';ota.mkdir()
    (work/'ota_config.in').write_text('current_version=0\n')
    (ota/'ota_v0.ok').write_bytes(b'')
    whole=hashlib.md5(data).hexdigest();previous=whole;hashes=[]
    for index,start in enumerate(range(0,len(data),524288)):
        chunk=data[start:start+524288]
        (ota/f'rootfs.squashfs.{index:04d}.{previous}').write_bytes(chunk)
        previous=hashlib.md5(chunk).hexdigest();hashes.append(previous)
    (ota/f'ota_md5_rootfs.squashfs.{whole}').write_text('\n'.join(hashes)+'\n')
    (ota/'ota_update.in').write_text(f'ota_version=0\n\nimg_type=rootfs\nimg_name=rootfs.squashfs\nimg_size={len(data)}\nimg_md5={whole}\n')
    # Deliberately no kernel, bootloader, recovery, userdata or RTOS payload.
    output.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['genisoimage','-quiet','-o',str(output),'-J','-r',str(work)],check=True)
    return {'model':'HiBy R1','kind':'experimental first-boot rootfs-only UPT',
            'requires_existing_kernel_sha256':'9222276c59d7bc9ffb8594106475b986bf66066c4f8f4db99c6fb34cb4fc64f6',
            'install_via':'Sonix Firmware Update menu; software entry selects ota:kernel2 recovery first',
            'rootfs_target_when_in_software_recovery':'/dev/mtd2',
            'build_id':candidate.get('build_id'),
            'kernel_payload':False,'bootloader_payload':False,'recovery_payload':False,
            'userdata_payload':False,'rootfs_bytes':len(data),'rootfs_sha256':hashlib.sha256(data).hexdigest(),
            'chunk_count':len(hashes),'upt_bytes':output.stat().st_size,
            'upt_sha256':hashlib.sha256(output.read_bytes()).hexdigest(),
            'cold_boot_verified':False,'stock_restore_verified':False,
            'notes':['A temporary boot-selection write enters recovery and returns to the primary kernel after update.',
                     'Use on this verified R1/Sonix kernel combination only. Keep recovery partitions intact.',
                     'The UPT is not a raw NAND image and must not be written directly to an MTD device.']}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('image','candidate','work','output','report'):p.add_argument(name,type=Path)
    a=p.parse_args()
    result=package(a.image,json.loads(a.candidate.read_text()),a.work,a.output)
    a.report.write_text(json.dumps(result,indent=2)+'\n')
    print('Rootfs-only R1 UPT built; no device connection or installation performed.')


if __name__=='__main__':main()
