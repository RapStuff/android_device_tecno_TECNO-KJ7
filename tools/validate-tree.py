#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check KJ7 source artifacts without a full Android checkout."""
import argparse
import hashlib
from pathlib import Path
import re
from xml.etree import ElementTree as ET

from blobtool import BLOBS, DEVICE, VENDOR, inventory

KERNEL = DEVICE.with_name('TECNO-KJ7-kernel')
OPAQUE = {
    'vendor/etc/power_app_cfg.xml', 'vendor/etc/power_app_fling_cfg.xml',
    'vendor/etc/powercontable.xml', 'vendor/etc/powerscntbl.xml',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def compare_blob(name, stock):
    blob = BLOBS / name
    if blob.is_symlink():
        if stock:
            peer = stock / name
            require(peer.is_symlink() and peer.readlink() == blob.readlink(), f'Symlink changed: {name}')
        return True
    require(blob.is_file(), f'Missing blob: {name}')
    if stock:
        with (stock / name).open('rb') as expected, blob.open('rb') as actual:
            require(hashlib.file_digest(expected, 'sha256').digest()
                    == hashlib.file_digest(actual, 'sha256').digest(), f'Blob changed: {name}')
    return False


def check_fstab():
    for p in [DEVICE / 'rootdir/etc/fstab.mt6789', DEVICE / 'rootdir/etc/recovery.fstab']:
        rows = []
        for line in p.read_text().splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            row = line.split()
            require(len(row) == 5, f'{p.name}: invalid field count: {line}')
            src, mount, _, _, flags = row
            tokens = flags.split(',')
            require(all(tokens), f'{p.name}: empty flag: {line}')
            require(mount.startswith('/') or ('voldmanaged=' in flags and mount == 'auto'),
                    f'{p.name}: invalid mountpoint: {line}')
            require(('logical' in tokens) == (not src.startswith('/')), f'{p.name}: invalid source: {line}')
            rows.append(row)
        data = [r for r in rows if r[1] == '/data']
        require(len(data) == 1 and data[0][0] == '/dev/block/by-name/userdata' and data[0][2] == 'f2fs'
                and 'fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized' in data[0][4],
                f'{p.name}: wrong userdata strategy')
        print(f'{p.name}: {len(rows)} valid entries')


def check_xml():
    n = 0
    opaque_seen = set()
    for f in sorted(DEVICE.rglob('*.xml')) + sorted(VENDOR.rglob('*.xml')):
        if f.is_symlink():
            continue
        try:
            if f == DEVICE / 'aosp17/fcm6/android-base-conditional.xml':
                # Soong kconfig_xml_fixup consumes upstream XML with multiple roots.
                ET.fromstring('<meta>' + f.read_text() + '</meta>')
            else:
                ET.parse(f)
        except ET.ParseError:
            require(f.is_relative_to(BLOBS), f'Malformed XML: {f}')
            name = f.relative_to(BLOBS).as_posix()
            require(name in OPAQUE and f.read_bytes().startswith(b'qeZU'), f'Malformed stock XML: {name}')
            opaque_seen.add(name)
        else:
            n += 1
    require(opaque_seen == OPAQUE, 'Expected opaque power configuration inventory changed')
    print(f'XML: {n} parsed; {len(opaque_seen)} opaque stock files retained unchanged')


def check_modules():
    early = KERNEL / 'modules/vendor_ramdisk'
    late = KERNEL / 'modules/vendor_dlkm'
    for group in [early, late]:
        loads = 0
        for p in group.glob('modules.load*'):
            for mod in p.read_text().split():
                require((group / mod).is_file(), f'{p}: missing {mod}')
                loads += 1
        for m in re.findall(r'[^\s:]+\.ko', (group / 'modules.dep').read_text()):
            name = Path(m).name
            require((group / name).is_file() or (early / name).is_file(), f'{group}: missing dependency {m}')
        print(f'{group.name}: {len(list(group.glob("*.ko")))} modules; {loads} records in stock load lists')


def check_blueprint():
    content = (VENDOR / 'Android.bp').read_text()
    n = content.count('cc_prebuilt_library_shared {')
    require(n and n == content.count('check_elf_files: false,'), 'Missing ELF check override')
    files = re.findall(r'srcs:\s*\[\s*"([^"]+)"', content)
    for src in files:
        require((VENDOR / src).is_file(), f'Blueprint input missing: {src}')
    print(f'Blueprint: {n} shared library modules; {len(files)} ELF inputs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock-dump', type=Path, help='also compare all blobs byte for byte')
    args = parser.parse_args()
    names = inventory()
    links = sum(compare_blob(name, args.stock_dump) for name in names)
    print(f'Proprietary: {len(names) - links} files, {links} symlinks')
    check_fstab()
    check_xml()
    check_modules()
    check_blueprint()


if __name__ == '__main__':
    main()
