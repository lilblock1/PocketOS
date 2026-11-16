#!/usr/bin/env python3
"""Opt-in media test configuration for an offline, already prepared R1 rootfs."""
import argparse
import re
from pathlib import Path
from r1_assemble import digest, regular, safe_parent

PIN = 'c536f32c0c30c00fc6333479d6cb3d0e4c37464b592034f3b2dade6f82d680a7'

def prepare(root, binary, pin=PIN):
    if not root.is_dir() or root.is_symlink() or root.resolve() == Path('/'):
        raise ValueError('Requires an offline development candidate')
    if not re.fullmatch('[0-9a-f]{64}', pin) or not regular(binary) or digest(binary) != pin:
        raise ValueError('Decoder does not match trusted pin')
    config = root / 'etc/pocketos'
    safe_parent(root, Path('etc/pocketos/decoder.sha256'))
    if config.is_symlink() or not config.is_dir():
        raise ValueError('Invalid candidate configuration')
    release = root / 'etc/pocketos-release'
    if not regular(release) or not {'NAME=PocketOS','TARGET=hiby-r1','STATE=development-userspace-only','FLASHABLE=no'}.issubset(release.read_text().splitlines()):
        raise ValueError('Wrong rootfs identity')
    for name in ('autostart','firstboot-enable'):
        if not regular(config/name):
            raise ValueError('Prepare firstboot before media configuration')
    for name in ('pocketos-start','pocketos-music','pocketos-shell','pocketos-decoder'):
        safe_parent(root, Path('usr/bin')/name)
        if not regular(root/'usr/bin'/name):
            raise ValueError('Updated media application missing: '+name)
    for name in ('features','suspend-enable','update-public.pem'):
        if (config/name).exists() or (config/name).is_symlink():
            raise ValueError('Conflicting or broad activation marker: '+name)
    markers = {'audio-enable':'POCKETOS-R1-AUDIO-1\n','smart-sleep-enable':'POCKETOS-R1-SMART-SLEEP-1\n','decoder.sha256':pin+'\n'}
    for name in markers:
        if (config/name).exists() or (config/name).is_symlink():
            raise ValueError('Unexpected existing marker: '+name)
    made = []
    try:
        for name, value in markers.items():
            path = config/name
            with path.open('x') as f:
                made.append(path)
                f.write(value)
            path.chmod(0o400)
    except Exception:
        for path in made:
            path.unlink(missing_ok=True)
        raise
    return markers

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path)
    p.add_argument('decoder',type=Path)
    p.add_argument('--enable-media-test',action='store_true',required=True)
    args=p.parse_args()
    if (Path(__file__).resolve().parents[1]/'boards/hiby-r1/verified-decoder.sha256').read_text()!=PIN+'\n':
        raise SystemExit('Repository pin mismatch')
    prepare(args.root,args.decoder)
    print('Offline low-gain audio and Smart Sleep test markers enabled; no decoder copied to flash. Installation not approved.')

if __name__=='__main__':main()
