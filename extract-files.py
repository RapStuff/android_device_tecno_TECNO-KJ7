#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

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
    'device/tecno/TECNO_KJ7',
    'hardware/mediatek',
    'hardware/mediatek/libion_mtk',
    'hardware/mediatek/libmtkperf_client',
]


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
        'vendor/bin/hw/android.hardware.media.c2@1.2-mediatek',
        'vendor/bin/hw/android.hardware.media.c2@1.2-mediatek-64b',
    ): blob_fixup()
        .replace_needed('libavservices_minijail_vendor.so', 'libavservices_minijail.so')
        .add_needed('libstagefright_foundation-v33.so'),
    (
        'vendor/lib/libcodec2_mtk_venc.so',
        'vendor/lib/libcodec2_mtk_vdec.so',
        'vendor/lib64/libcodec2_mtk_venc.so',
        'vendor/lib64/libcodec2_mtk_vdec.so',
    ): blob_fixup()
        .replace_needed('libformatter.so', 'libformatter_mtk.so'),
    (
        'vendor/lib/libformatter_mtk.so',
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
        'vendor/lib/hw/face.default.so',
        'vendor/lib64/hw/face.default.so',
    ): blob_fixup()
        .replace_needed('android.hardware.power-V1-ndk_platform.so', 'android.hardware.power-V1-ndk.so'),
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
        'vendor/lib/libpcap_mtk.so',
        'vendor/lib64/libpcap_mtk.so',
    ): blob_fixup()
        .fix_soname(),
    (
        'vendor/lib/libpkm.so',
        'vendor/lib64/libpkm.so',
    ): blob_fixup()
        .replace_needed('libpcap.so', 'libpcap_mtk.so'),
    (
        'vendor/lib/hw/hwcomposer.mtk_common.so',
        'vendor/lib64/hw/hwcomposer.mtk_common.so',
        'vendor/lib64/hw/hwcomposer.mtk_fold.so',
    ): blob_fixup()
        .add_needed('libprocessgroup_shim.so'),
    (
        'vendor/lib/libnir_neon_driver_ndk.mtk.vndk.so',
        'vendor/lib64/libnir_neon_driver_ndk.mtk.vndk.so',
        'vendor/lib/libspeech_enh_lib.so',
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
        'vendor/lib/mt6789/libaalservice.so',
        'vendor/lib64/mt6789/libaalservice.so',
        'vendor/lib64/mt6789/libcam.utils.sensorprovider.so',
    ): blob_fixup()
        .replace_needed('libsensorndkbridge.so', 'android.hardware.sensors@1.0-convert-shared.so'),
    (
        'vendor/bin/hw/tranradionet',
        'vendor/bin/hw/vendor.mediatek.hardware.tranHwInfo@1.0-service',
        'vendor/bin/meta_tst',
        'vendor/bin/nfcstackp-vendor',
        'vendor/bin/STFlashTool',
        'vendor/lib/libnvram.so',
        'vendor/lib64/libnvram.so',
        'vendor/lib/libsysenv.so',
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
        'vendor/lib/mt6789/libneuralnetworks_sl_driver_mtk_prebuilt.so',
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
        'vendor/lib/hw/mt6789/vendor.mediatek.hardware.pq@2.15-impl.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.pq@2.15-impl.so',
    ): blob_fixup()
        .replace_needed('libhidlbase.so', 'libhidlbase-v32.so'),
    (
        'vendor/etc/init/init.volte_md_status.rc',
    ): blob_fixup()
        .binary_regex_replace(b'system/vendor', b'vendor'),
}  # fmt: skip

module = ExtractUtilsModule(
    'TECNO-KJ7',
    'tecno',
    device_rel_path='device/tecno/TECNO_KJ7',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
