#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

import os
import re
import struct

from extract_utils.fixups_blob import (
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'device/tecno/kj7',
    'hardware/mediatek',
    'hardware/mediatek/libion_mtk',
    'hardware/mediatek/libmtkperf_client',
]


def _parse_elf_sections(elf: bytearray):
    e_shoff = struct.unpack('<Q', elf[40:48])[0]
    e_shentsize = struct.unpack('<H', elf[58:60])[0]
    e_shnum = struct.unpack('<H', elf[60:62])[0]
    e_shstrndx = struct.unpack('<H', elf[62:64])[0]

    shstr_hdr = e_shoff + e_shstrndx * e_shentsize
    shstr_offset = struct.unpack('<Q', elf[shstr_hdr + 24 : shstr_hdr + 32])[0]

    sections = {}
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh_name, sh_type, _flags, sh_addr, sh_offset, sh_size, sh_link, _info = struct.unpack(
            '<IIQQQQII', elf[off : off + 48]
        )
        name = elf[shstr_offset + sh_name :].split(b'\0')[0].decode('latin1')
        sections[name] = {
            'index': i,
            'name': name,
            'type': sh_type,
            'addr': sh_addr,
            'offset': sh_offset,
            'size': sh_size,
            'link': sh_link,
        }
    return sections


def _find_elf_symbol(elf: bytearray, sections: dict, symbol_name: str):
    dynsym = sections.get('.dynsym')
    dynstr = sections.get('.dynstr')
    if not dynsym or not dynstr:
        return None, None
    dynstr_off = dynstr['offset']
    for off in range(dynsym['offset'], dynsym['offset'] + dynsym['size'], 24):
        st_name, _info, _other, st_shndx, st_value, _size = struct.unpack(
            '<IBBHQQ', elf[off : off + 24]
        )
        str_start = dynstr_off + st_name
        str_end = elf.find(b'\0', str_start)
        if elf[str_start:str_end].decode('latin1') == symbol_name:
            for sec in sections.values():
                if sec['index'] == st_shndx:
                    return st_value, sec['offset'] + (st_value - sec['addr'])
    return None, None


def _find_plt_entry(elf: bytearray, sections: dict, symbol_name: str):
    rela = sections.get('.rela.plt')
    dynsym = sections.get('.dynsym')
    dynstr = sections.get('.dynstr')
    plt = sections.get('.plt')
    if not rela or not dynsym or not dynstr or not plt:
        return None
    dynstr_off = dynstr['offset']
    for i, off in enumerate(range(rela['offset'], rela['offset'] + rela['size'], 24)):
        _r_offset, r_info, _r_addend = struct.unpack('<QQq', elf[off : off + 24])
        sym_idx = r_info >> 32
        sym_off = dynsym['offset'] + sym_idx * 24
        st_name = struct.unpack('<I', elf[sym_off : sym_off + 4])[0]
        str_start = dynstr_off + st_name
        str_end = elf.find(b'\0', str_start)
        if elf[str_start:str_end].decode('latin1') == symbol_name:
            return plt['addr'] + 32 + i * 16
    return None


def patch_libsl_fp_impl(ctx, file, file_path: str, *args, **kwargs):
    """Port of Oplus-UDFPS-Bridge-Transsion/patch_libsl_fp.py for AOSP 17 UDFPS."""
    with open(file_path, 'rb') as f:
        elf = bytearray(f.read())

    sections = _parse_elf_sections(elf)

    # 1. Bypass netlink FULL_HBM_SET wait in silfp_cust_wait_ui_ready with 5ms usleep + return 0
    vma_wait_ui, off_wait_ui = _find_elf_symbol(elf, sections, 'silfp_cust_wait_ui_ready')
    plt_usleep = _find_plt_entry(elf, sections, 'usleep')
    if not vma_wait_ui or not off_wait_ui or not plt_usleep:
        raise RuntimeError(f'Missing silfp_cust_wait_ui_ready ({vma_wait_ui}) or usleep@plt ({plt_usleep}) in {file_path}')

    diff = (plt_usleep - (vma_wait_ui + 8)) // 4
    bl_insn = 0x94000000 | (diff & 0x03ffffff)
    elf[off_wait_ui : off_wait_ui + 24] = struct.pack(
        '<6I',
        0xa9bf7bfd,  # stp x29, x30, [sp, #-16]!
        0x52827100,  # mov w0, #5000
        bl_insn,     # bl usleep@plt
        0x52800000,  # mov w0, #0
        0xa8c17bfd,  # ldp x29, x30, [sp], #16
        0xd65f03c0,  # ret
    )

    # 2, 3, 6, 8, 9. Return 0 immediately in HAL HBM/token/cancel blockers
    ret0 = struct.pack('<2I', 0x52800000, 0xd65f03c0)
    for sym in (
        'silfp_cust_set_hbm_mode',
        'silfp_cust_restore_hbm',
        'silfp_impl_verify_enroll_challenge',
        'silfp_impl_wait_finger_up_with_cancel',
        'silfp_impl_wait_finger_up_with_cancel_no_action',
    ):
        _vma, off = _find_elf_symbol(elf, sections, sym)
        if not off:
            raise RuntimeError(f'Missing required symbol {sym} in {file_path}')
        elf[off : off + 8] = ret0

    # 4 & 5. Skip m_nl_inited branch in silfp_dev_sync_finger_down / up
    for sym in ('silfp_dev_sync_finger_down', 'silfp_dev_sync_finger_up'):
        _vma, off = _find_elf_symbol(elf, sections, sym)
        if not off:
            raise RuntimeError(f'Missing required symbol {sym} in {file_path}')
        if elf[off + 12 : off + 16] not in (bytes.fromhex('21010054'), bytes.fromhex('1f2003d5')):
            raise RuntimeError(f'Unexpected opcode at {sym}+12 in {file_path}: {elf[off+12:off+16].hex()}')
        elf[off + 12 : off + 16] = bytes.fromhex('1f2003d5')  # nop

    # 7. Force m_finger_status_mode = 1 in silfp_dev_wait_finger_status to use msg pipe
    _vma, off_wait = _find_elf_symbol(elf, sections, 'silfp_dev_wait_finger_status')
    if not off_wait:
        raise RuntimeError(f'Missing silfp_dev_wait_finger_status in {file_path}')
    for rel in (0x18, 0x5c, 0xf0):
        insn = struct.unpack('<I', elf[off_wait + rel : off_wait + rel + 4])[0]
        if insn not in (0xb945c708, 0x52800028):
            raise RuntimeError(f'Unexpected opcode at silfp_dev_wait_finger_status+0x{rel:x}: 0x{insn:08x}')
        elf[off_wait + rel : off_wait + rel + 4] = struct.pack('<I', 0x52800028)  # mov w8, #1

    # 8. Force enrollment finger-down wait through silfp_impl_wait_finger_down (msg pipe)
    #    instead of TEE polling loop when GSL7002 init2 capability bit 6 is set.
    _vma, off_get_down = _find_elf_symbol(elf, sections, 'silfp_impl_get_finger_down_with_cancel')
    if not off_get_down:
        raise RuntimeError(f'Missing silfp_impl_get_finger_down_with_cancel in {file_path}')
    insn_bit6 = struct.unpack('<I', elf[off_get_down + 0x30 : off_get_down + 0x34])[0]
    if insn_bit6 not in (0x37300148, 0xd503201f):
        raise RuntimeError(
            f'Unexpected opcode at silfp_impl_get_finger_down_with_cancel+0x30: 0x{insn_bit6:08x}'
        )
    elf[off_get_down + 0x30 : off_get_down + 0x34] = struct.pack('<I', 0xd503201f)  # nop

    # 9. Skip silfp_cmd_check_esd() retry loop after msg-pipe finger-down in silfp_impl_wait_finger_down
    #    so optical touches proceed directly to capture instead of looping back to poll().
    _vma, off_wait_down = _find_elf_symbol(elf, sections, 'silfp_impl_wait_finger_down')
    if not off_wait_down:
        raise RuntimeError(f'Missing silfp_impl_wait_finger_down in {file_path}')
    insn_bit2 = struct.unpack('<I', elf[off_wait_down + 0xd8 : off_wait_down + 0xdc])[0]
    if insn_bit2 not in (0x37100268, 0x14000013):
        raise RuntimeError(
            f'Unexpected opcode at silfp_impl_wait_finger_down+0xd8: 0x{insn_bit2:08x}'
        )
    elf[off_wait_down + 0xd8 : off_wait_down + 0xdc] = struct.pack('<I', 0x14000013)  # b +0x4c

    with open(file_path, 'wb') as f:
        f.write(elf)


def patch_c2_init_rc_64b(ctx, file, file_path: str, *args, **kwargs):
    """Switch MediaTek Codec2 HIDL service RC entry to the 64-bit binary."""
    with open(file_path, 'r', encoding='utf-8') as f:
        data = f.read()
    updated = re.sub(
        r'/vendor/bin/hw/android\.hardware\.media\.c2@1\.2-mediatek(?!-64b)',
        '/vendor/bin/hw/android.hardware.media.c2@1.2-mediatek-64b',
        data,
    )
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(updated)


def patch_c2_seccomp_64b(ctx, file, file_path: str, *args, **kwargs):
    """Translate 32-bit ARM seccomp syscall rules to AArch64 for c2@1.2-mediatek-64b."""
    arm_to_arm64 = (
        (r'^mmap2:', 'mmap:'),
        (r'^ftruncate64:\s*1$', 'ftruncate: 1'),
        (r'^getuid32:\s*1$', 'getuid: 1'),
        (r'^geteuid32:\s*1$', 'geteuid: 1'),
        (r'^getgid32:\s*1$', 'getgid: 1'),
        (r'^getegid32:\s*1$', 'getegid: 1'),
        (r'^getgroups32:\s*1$', 'getgroups: 1'),
        (r'^open:\s*1$', 'openat: 1'),
        (r'^fstat64:\s*1$', 'fstat: 1'),
        (r'^readlink:\s*1$', 'readlinkat: 1'),
        (r'^fstatfs64:\s*1$', 'fstatfs: 1'),
        (r'^_llseek:\s*1$', 'lseek: 1'),
        (r'^fstatat64:\s*1$', 'newfstatat: 1'),
        (r'^ugetrlimit:\s*1$', 'getrlimit: 1'),
        (r'^sigreturn:\s*1$', 'rt_sigreturn: 1'),
        (r'^fcntl64:\s*1$', 'fcntl: 1'),
    )
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.read().splitlines()

    seen_syscalls = set()
    out = []
    for line in lines:
        stripped = line.strip()
        if stripped and not stripped.startswith('#'):
            for pat, rep in arm_to_arm64:
                if re.match(pat, stripped):
                    stripped = re.sub(pat, rep, stripped)
                    break
            syscall = stripped.split(':', 1)[0].strip()
            if syscall in seen_syscalls:
                continue
            seen_syscalls.add(syscall)
            line = stripped
        out.append(line)

    for extra in ('unlinkat: 1', 'sched_setscheduler: 1'):
        syscall = extra.split(':', 1)[0].strip()
        if syscall not in seen_syscalls:
            seen_syscalls.add(syscall)
            out.append(extra)

    with open(file_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(out) + '\n')



lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
}

blob_fixups: blob_fixups_user_type = {
    (
        'vendor/bin/hw/android.hardware.gnss-service.mediatek',
        'vendor/lib64/hw/android.hardware.gnss-impl-mediatek.so',
    ): blob_fixup()
        .replace_needed('android.hardware.gnss-V1-ndk_platform.so', 'android.hardware.gnss-V1-ndk.so'),
    (
        'vendor/bin/hw/android.hardware.security.keymint-service.trustonic',
    ): blob_fixup()
        .replace_needed('android.hardware.security.keymint-V1-ndk_platform.so', 'android.hardware.security.keymint-V1-ndk.so')
        .replace_needed('android.hardware.security.secureclock-V1-ndk_platform.so', 'android.hardware.security.secureclock-V1-ndk.so')
        .replace_needed('android.hardware.security.sharedsecret-V1-ndk_platform.so', 'android.hardware.security.sharedsecret-V1-ndk.so')
        .add_needed('android.hardware.security.rkp-V1-ndk.so'),
    (
        'vendor/bin/hw/android.hardware.media.c2@1.2-mediatek-64b',
    ): blob_fixup()
        .replace_needed('libavservices_minijail_vendor.so', 'libavservices_minijail.so')
        .add_needed('libstagefright_foundation-v33.so'),
    (
        'vendor/etc/init/android.hardware.media.c2@1.2-mediatek.rc',
    ): blob_fixup()
        .call(patch_c2_init_rc_64b, need_tmp_dir=False),
    (
        'vendor/etc/seccomp_policy/android.hardware.media.c2@1.2-mediatek-seccomp-policy',
        'vendor/etc/seccomp_policy/android.hardware.media.c2@1.2-extended-seccomp-policy',
    ): blob_fixup()
        .call(patch_c2_seccomp_64b, need_tmp_dir=False),
    (
        'vendor/lib64/libcodec2_mtk_venc.so',
        'vendor/lib64/libcodec2_mtk_vdec.so',
    ): blob_fixup()
        .replace_needed('libformatter.so', 'libformatter_mtk.so'),
    (
        'vendor/lib64/libformatter_mtk.so',
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/bin/hw/android.hardware.vibrator-service.richtap',
    ): blob_fixup()
        .replace_needed('android.hardware.vibrator-V2-ndk_platform.so', 'android.hardware.vibrator-V2-ndk.so'),
    (
        'vendor/etc/init/vibrator-mtk-default.rc',
    ): blob_fixup()
        .binary_regex_replace(b'android.hardware.vibrator-service.mediatek', b'android.hardware.vibrator-service.richtap'),
    (
        'vendor/lib64/hw/face.default.so',
    ): blob_fixup()
        .replace_needed('android.hardware.power-V1-ndk_platform.so', 'android.hardware.power-V1-ndk.so')
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/bin/hw/mtkfusionrild',
    ): blob_fixup()
        .add_needed('libutils-v32.so'),
    (
        'vendor/bin/mnld',
    ): blob_fixup()
        .replace_needed('libsensorndkbridge.so', 'android.hardware.sensors@1.0-convert-shared.so')
        .replace_needed('libmnl.so', 'libmnl_mtk.so'),
    (
        'vendor/lib64/libmnl_mtk.so',
    ): blob_fixup()
        .fix_soname()
        .add_needed('libcutils.so'),
    (
        'vendor/lib64/mt6789/libmorpho_video_stabilizer.so',
    ): blob_fixup()
        .add_needed('libutils.so'),
    (
        'vendor/lib64/libpcap_mtk.so',
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/lib64/libpkm.so',
    ): blob_fixup()
        .replace_needed('libpcap.so', 'libpcap_mtk.so'),
    (
        'vendor/lib64/hw/hwcomposer.mtk_common.so',
        'vendor/lib64/hw/hwcomposer.mtk_fold.so',
    ): blob_fixup()
        .add_needed('libprocessgroup_shim.so'),
    (
        'vendor/lib64/libnir_neon_driver_ndk.mtk.vndk.so',
        'vendor/lib64/libspeech_enh_lib.so',
        'vendor/lib64/libnotifyaudiohal.so',
        'vendor/lib64/hw/sensors.elliptic.so',
        'vendor/lib64/hw/gf_fingerprint.default.so',
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/lib64/mt6789/libaaa_ltm.so',
        'vendor/lib64/mt6789/lib3a.ae.stat.so',
        'vendor/lib64/mt6789/lib3a.flash.so',
        'vendor/lib64/mt6789/lib3a.sensors.color.so',
        'vendor/lib64/mt6789/lib3a.sensors.flicker.so',
    ): blob_fixup()
        .add_needed('liblog.so'),
    (
        'vendor/lib64/mt6789/libaalservice.so',
    ): blob_fixup()
        .replace_needed('libsensorndkbridge.so', 'android.hardware.sensors@1.0-convert-shared.so')
        .replace_needed('libhidlbase.so', 'libhidlbase-v32.so')
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/lib64/mt6789/libcam.utils.sensorprovider.so',
    ): blob_fixup()
        .replace_needed('libsensorndkbridge.so', 'android.hardware.sensors@1.0-convert-shared.so')
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/bin/hw/mt6789/camerahalserver',
        'vendor/bin/hw/vendor.transsion.hardware.trancam.trancamserver@1.0-service',
        'vendor/lib64/hw/mt6789/android.hardware.camera.provider@2.6-impl-mediatek.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.camera.atms@1.0-impl.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.camera.bgservice@1.1-impl.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.camera.ccap@1.0-impl.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.camera.isphal@1.0-impl.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.camera.isphal@1.1-impl.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.camera.lomoeffect@1.0-impl.so',
        'vendor/lib64/libcam3_tranelapsedtime.so',
        'vendor/lib64/libcam3_transerverengine.so',
        'vendor/lib64/libcamalgo.warp.so',
        'vendor/lib64/libmtkcam_3rdparty.thread.so',
        'vendor/lib64/libmtkcam_pipelinepolicy-aov.so',
        'vendor/lib64/libmtkisp_metadata.so',
        'vendor/lib64/mt6789/lib3a.ae.core.so',
        'vendor/lib64/mt6789/lib3a.af.assist.so',
        'vendor/lib64/mt6789/lib3a.ccudrv.so',
        'vendor/lib64/mt6789/lib3a.ccuif.so',
        'vendor/lib64/mt6789/lib3a.log.so',
        'vendor/lib64/mt6789/lib3a.n3d3a.so',
        'vendor/lib64/mt6789/libcam.afhal.so',
        'vendor/lib64/mt6789/libcam.chdr.so',
        'vendor/lib64/mt6789/libcam.feature_utils.so',
        'vendor/lib64/mt6789/libcam.hal3a.cctsvr.so',
        'vendor/lib64/mt6789/libcam.hal3a.log.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.ae.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.ai3a.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.awb.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.dng.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.fsmgr.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.lscMgr.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.lsctbl.50.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.nvram.50.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.platform.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.resultpool.so',
        'vendor/lib64/mt6789/libcam.hal3a.v3.so',
        'vendor/lib64/mt6789/libcam.halisp.buf.so',
        'vendor/lib64/mt6789/libcam.halisp.common.so',
        'vendor/lib64/mt6789/libcam.halisp.so',
        'vendor/lib64/mt6789/libcam.halsensor.so',
        'vendor/lib64/mt6789/libcam.iopipe.so',
        'vendor/lib64/mt6789/libcam.isptuning.so',
        'vendor/lib64/mt6789/libcam.pdtblgen.so',
        'vendor/lib64/mt6789/libcam.tuning.cache.so',
        'vendor/lib64/mt6789/libcam.vhdr.so',
        'vendor/lib64/mt6789/libcam3atms_profiles.so',
        'vendor/lib64/mt6789/libcamalgo.ispfeature.so',
        'vendor/lib64/mt6789/libcamdrv_tuning_mgr.so',
        'vendor/lib64/mt6789/libcamera.custom.pd_buf_mgr.so',
        'vendor/lib64/mt6789/libcamera.customae.so',
        'vendor/lib64/mt6789/libcamera.customaf.so',
        'vendor/lib64/mt6789/libcamera.customawb.so',
        'vendor/lib64/mt6789/libcamera.customflk.so',
        'vendor/lib64/mt6789/libcameracustom.camera.isp.so',
        'vendor/lib64/mt6789/libcameracustom.plugin.so',
        'vendor/lib64/mt6789/libcameracustom.so',
        'vendor/lib64/mt6789/libdpframework.so',
        'vendor/lib64/mt6789/libfeature.face.so',
        'vendor/lib64/mt6789/libfeature.stereo.provider.so',
        'vendor/lib64/mt6789/libfeature.vsdof.hal.so',
        'vendor/lib64/mt6789/libfeature_3dnr.so',
        'vendor/lib64/mt6789/libfeature_fsc.so',
        'vendor/lib64/mt6789/libfeature_lmv.so',
        'vendor/lib64/mt6789/libfeature_rss.so',
        'vendor/lib64/mt6789/libfeatureiodrv_mem.so',
        'vendor/lib64/mt6789/libimageio_plat_pipe.so',
        'vendor/lib64/mt6789/libmtkcam.atmseventmgr.so',
        'vendor/lib64/mt6789/libmtkcam.eventcallback.so',
        'vendor/lib64/mt6789/libmtkcam.featurepipe.capture.so',
        'vendor/lib64/mt6789/libmtkcam.featurepipe.depthmap.so',
        'vendor/lib64/mt6789/libmtkcam.featurepipe.streaming.so',
        'vendor/lib64/mt6789/libmtkcam.featurepipe.vsdof_util.so',
        'vendor/lib64/mt6789/libmtkcam.logicalmodule.so',
        'vendor/lib64/mt6789/libmtkcam_3rdparty.core.so',
        'vendor/lib64/mt6789/libmtkcam_3rdparty.customer.so',
        'vendor/lib64/mt6789/libmtkcam_3rdparty.mtk.so',
        'vendor/lib64/mt6789/libmtkcam_3rdparty.so',
        'vendor/lib64/mt6789/libmtkcam_calibration_convertor.so',
        'vendor/lib64/mt6789/libmtkcam_calibration_provider.so',
        'vendor/lib64/mt6789/libmtkcam_debugutils.so',
        'vendor/lib64/mt6789/libmtkcam_device3_app.so',
        'vendor/lib64/mt6789/libmtkcam_device3_hal.so',
        'vendor/lib64/mt6789/libmtkcam_device3_hidl.so',
        'vendor/lib64/mt6789/libmtkcam_device3_hidlutils.so',
        'vendor/lib64/mt6789/libmtkcam_device3_utils.so',
        'vendor/lib64/mt6789/libmtkcam_devicesessionpolicy.so',
        'vendor/lib64/mt6789/libmtkcam_exif.so',
        'vendor/lib64/mt6789/libmtkcam_featurepolicy.so',
        'vendor/lib64/mt6789/libmtkcam_featureutils.so',
        'vendor/lib64/mt6789/libmtkcam_fwkutils.so',
        'vendor/lib64/mt6789/libmtkcam_grallocutils.so',
        'vendor/lib64/mt6789/libmtkcam_hwnode.so',
        'vendor/lib64/mt6789/libmtkcam_hwutils.so',
        'vendor/lib64/mt6789/libmtkcam_imgbuf.so',
        'vendor/lib64/mt6789/libmtkcam_mapping_mgr.so',
        'vendor/lib64/mt6789/libmtkcam_metadata.so',
        'vendor/lib64/mt6789/libmtkcam_metastore.so',
        'vendor/lib64/mt6789/libmtkcam_modulefactory_aaa.so',
        'vendor/lib64/mt6789/libmtkcam_modulefactory_custom.so',
        'vendor/lib64/mt6789/libmtkcam_modulefactory_drv.so',
        'vendor/lib64/mt6789/libmtkcam_modulefactory_utils.so',
        'vendor/lib64/mt6789/libmtkcam_pipeline.so',
        'vendor/lib64/mt6789/libmtkcam_pipeline_fbm.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinemodel.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinemodel_adapter.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinemodel_capture.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinemodel_isp.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinemodel_session.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinemodel_utils.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinemodel_zsl.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinepolicy-security.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinepolicy-smvr.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinepolicy.so',
        'vendor/lib64/mt6789/libmtkcam_pipelinepolicy_factory.so',
        'vendor/lib64/mt6789/libmtkcam_prerelease.so',
        'vendor/lib64/mt6789/libmtkcam_scenariorecorder.so',
        'vendor/lib64/mt6789/libmtkcam_stdutils.so',
        'vendor/lib64/mt6789/libmtkcam_streamutils.so',
        'vendor/lib64/mt6789/libmtkcam_synchelper.so',
        'vendor/lib64/mt6789/libmtkcam_sysutils.so',
        'vendor/lib64/mt6789/libmtkcam_tuning_utils.so',
        'vendor/lib64/mt6789/libmtkcam_ulog.so',
        'vendor/lib64/mtkcam/mt6789/libmtkcam_streaminfo_plugin-p1stt.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.atms@1.0.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.bgservice@1.0.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.bgservice@1.1.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.ccap@1.0.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.frhandler@1.0.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.isphal@1.0.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.isphal@1.1.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.lomoeffect@1.0.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.postproc@1.0.so',
        'vendor/lib64/vendor.mediatek.hardware.camera.security@1.0.so',
        'vendor/lib64/vendor.transsion.hardware.trancam.trancamserver@1.0.so',
    ): blob_fixup()
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/bin/hw/tranradionet',
        'vendor/bin/hw/vendor.mediatek.hardware.tranHwInfo@1.0-service',
        'vendor/bin/meta_tst',
        'vendor/bin/nfcstackp-vendor',
        'vendor/bin/STFlashTool',
        'vendor/lib64/libnvram.so',
        'vendor/lib64/libsysenv.so',
    ): blob_fixup()
        .add_needed('libbase_shim.so'),
    (
        'vendor/lib64/vendor.silead.hardware.fingerprintext@1.0.so',
        'vendor/lib64/vendor.silead.hardware.fingerprintext@1.0-adapter-helper.so',
    ): blob_fixup()
        .add_needed('libhidlbase_shim.so'),
    (
        'vendor/lib64/mt6789/libsegmention.so',
    ): blob_fixup()
        .add_needed('libtflite_native_gainference.so'),
    (
        'vendor/lib64/mt6789/libneuralnetworks_sl_driver_mtk_prebuilt.so',
    ): blob_fixup()
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_describe')
        .clear_symbol_version('AHardwareBuffer_lock')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock')
        .clear_symbol_version('AHardwareBuffer_createFromHandle')
        .clear_symbol_version('AHardwareBuffer_getNativeHandle'),
    (
        'vendor/lib64/libvideofilmeffect.so',
    ): blob_fixup()
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_lock')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock'),
    (
        'vendor/lib64/mt6789/libeffect_hal.so',
        'vendor/lib64/mt6789/libtranssion_bodybeauty.so',
    ): blob_fixup()
        .clear_symbol_version('AHardwareBuffer_allocate')
        .clear_symbol_version('AHardwareBuffer_describe')
        .clear_symbol_version('AHardwareBuffer_lockPlanes')
        .clear_symbol_version('AHardwareBuffer_release')
        .clear_symbol_version('AHardwareBuffer_unlock'),
    (
        'vendor/bin/hw/vendor.mediatek.hardware.pq@2.2-service',
    ): blob_fixup()
        .replace_needed('libhidlbase.so', 'libhidlbase-v32.so')
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.pq@2.15-impl.so',
    ): blob_fixup()
        .replace_needed('libhidlbase.so', 'libhidlbase-v32.so')
        .replace_needed('libutils.so', 'libutils-v32.so')
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    (
        'vendor/lib64/mt6789/libpq_prot.so',
    ): blob_fixup()
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/bin/dconfig',
        'vendor/lib64/librt_extamp_intf.so',
        'vendor/lib64/libpowerhal.so',
        'vendor/lib64/hw/audio.primary.mediatek.so',
    ): blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    (
        'vendor/lib64/hw/android.hardware.sensors@2.X-subhal-mediatek.so',
    ): blob_fixup()
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/lib64/libwifi-hal-mtk.so',
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/lib64/libsl_fp_impl.so',
    ): blob_fixup()
        .call(patch_libsl_fp_impl, need_tmp_dir=False),
    (
        'vendor/etc/init/init.volte_md_status.rc',
    ): blob_fixup()
        .binary_regex_replace(b'system/vendor', b'vendor'),
}  # fmt: skip

module = ExtractUtilsModule(
    'kj7',
    'tecno',
    device_rel_path='device/tecno/kj7',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
