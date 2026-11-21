#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the KJ7 kernel and staging inputs for the Android 17 product."""
import gzip
import re
from xml.etree import ElementTree as ET

from blobtool import DEVICE, VENDOR


def kconfig(text):
    result = {}
    for line in text.splitlines():
        if line.startswith('CONFIG_'):
            key, value = line.split('=', 1)
            result[key] = value
        elif match := re.fullmatch(r'# (CONFIG_\w+) is not set', line):
            result[match.group(1)] = 'n'
    return result


def check_kernel():
    fcm = DEVICE / 'aosp17/fcm6'
    image = gzip.decompress((DEVICE.with_name('TECNO-KJ7-kernel') / 'kernel').read_bytes())
    version = re.search(rb'Linux version (\d+)\.(\d+)\.(\d+)', image)
    if not version:
        raise SystemExit('No Linux version found in the prebuilt kernel')
    actual_version = tuple(map(int, version.groups()))
    start = image.index(b'IKCFG_ST') + len(b'IKCFG_ST')
    end = image.index(b'IKCFG_ED', start)
    actual = kconfig(gzip.decompress(image[start:end]).decode())
    base = kconfig((fcm / 'android-base.config').read_text())
    # kernel_config metadata on this branch is an XML fragment with several roots.
    meta = ET.fromstring('<meta>' + (fcm / 'android-base-conditional.xml').read_text() + '</meta>')
    minimum = tuple(map(int, meta.find('kernel').get('minlts').split('.')))
    if actual_version[:2] != minimum[:2] or actual_version < minimum:
        raise SystemExit(f'Kernel {actual_version} does not satisfy minimum {minimum}')
    active = []
    for group in meta.findall('group'):
        def checks(nodes):
            return [(c.findtext('key'), c.findtext('value')) for c in nodes]
        if all(actual.get(k, 'n') == v for k, v in checks(group.findall('conditions/config'))):
            active.extend(checks(group.findall('config')))
    mismatches = [f'{k}: needs {v}, found {actual.get(k, "n")}'
                  for k, v in [*base.items(), *active] if actual.get(k, 'n') != v]
    if mismatches:
        raise SystemExit('\n'.join(mismatches))
    print(f'Kernel {".".join(map(str, actual_version))}: '
          f'{len(base)} FCM 6 base + {len(active)} active conditional checks passed')


def check_vintf_staging():
    root = DEVICE / 'vintf'
    vendor_pairs = [(root / 'manifest.xml', VENDOR / 'proprietary/vendor/etc/vintf/manifest.xml')]
    vendor_pairs.extend((f, VENDOR / 'proprietary/vendor/etc/vintf/manifest' / f.name)
                        for f in (root / 'fragments').glob('*.xml'))
    odm_pairs = [(f, VENDOR / 'proprietary/vendor/odm/etc/vintf' / f.name)
                 for f in (root / 'odm').glob('*.xml')]
    bp = (root / 'Android.bp').read_text()
    staged_bp = set(re.findall(r'src:\s*"([^"]+)"', bp))
    if staged_bp != {p.relative_to(root).as_posix() for p, _ in vendor_pairs}:
        raise SystemExit('Staged VINTF entries diverged from the stock inventory')
    a17_bc = (DEVICE / 'aosp17/BoardConfig.mk').read_text()
    staged_odm = set(re.findall(r'vintf/(odm/[^\s]+)', a17_bc))
    if staged_odm != {p.relative_to(root).as_posix() for p, _ in odm_pairs}:
        raise SystemExit('Staged ODM VINTF entries diverged from the stock inventory')
    pairs = vendor_pairs + odm_pairs
    for src, original in pairs:
        if src.read_bytes() != original.read_bytes():
            raise SystemExit(f'Staged manifest diverged from stock: {src}')
    manifest = ET.parse(pairs[0][0]).getroot()
    if manifest.get('target-level') != '6' or manifest.findtext('sepolicy/version') != '31.0':
        raise SystemExit('Stock vendor FCM or policy version changed')
    boot = ET.parse(root / 'fragments/android.hardware.boot@1.2.xml').getroot()
    if not any(h.findtext('name') == 'android.hardware.boot'
               and h.findtext('fqname') == '@1.2::IBootControl/default' for h in boot.findall('hal')):
        raise SystemExit('OTA requires Boot Control HIDL 1.2')
    print(f'Vendor FCM 6 and policy 31.0; {len(pairs)} exact vendor/ODM manifests staged; Boot Control 1.2 present')


if __name__ == '__main__':
    check_kernel()
    check_vintf_staging()
