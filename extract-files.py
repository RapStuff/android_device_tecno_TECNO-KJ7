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
        'vendor/lib/hw/mt6789/vendor.mediatek.hardware.camera.atms@1.0-impl.so',
        'vendor/lib/hw/mt6789/vendor.mediatek.hardware.camera.bgservice@1.1-impl.so',
        'vendor/lib/libcam3_transerverengine.so',
        'vendor/lib/mt6789/lib3a.ae.core.so',
        'vendor/lib/mt6789/lib3a.log.so',
        'vendor/lib/mt6789/libcam.hal3a.log.so',
        'vendor/lib/mt6789/libcameracustom.so',
        'vendor/lib/mt6789/libdpframework.so',
        'vendor/lib/mt6789/libmtkcam.atmseventmgr.so',
        'vendor/lib/mt6789/libmtkcam.eventcallback.so',
        'vendor/lib/mt6789/libmtkcam_debugutils.so',
        'vendor/lib/mt6789/libmtkcam_metadata.so',
        'vendor/lib/mt6789/libmtkcam_stdutils.so',
        'vendor/lib/mt6789/libmtkcam_sysutils.so',
        'vendor/lib/mt6789/libmtkcam_tuning_utils.so',
        'vendor/lib/mt6789/libmtkcam_ulog.so',
        'vendor/lib/vendor.mediatek.hardware.camera.atms@1.0.so',
        'vendor/lib/vendor.mediatek.hardware.camera.bgservice@1.0.so',
        'vendor/lib/vendor.mediatek.hardware.camera.bgservice@1.1.so',
        'vendor/lib/vendor.transsion.hardware.trancam.trancamserver@1.0.so',
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
    ): blob_fixup()
        .replace_needed('libhidlbase.so', 'libhidlbase-v32.so')
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/lib/hw/mt6789/vendor.mediatek.hardware.pq@2.15-impl.so',
        'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.pq@2.15-impl.so',
    ): blob_fixup()
        .replace_needed('libhidlbase.so', 'libhidlbase-v32.so')
        .replace_needed('libutils.so', 'libutils-v32.so')
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    (
        'vendor/lib/mt6789/libpq_prot.so',
        'vendor/lib64/mt6789/libpq_prot.so',
    ): blob_fixup()
        .replace_needed('libutils.so', 'libutils-v32.so'),
    (
        'vendor/bin/dconfig',
        'vendor/lib/librt_extamp_intf.so',
        'vendor/lib64/librt_extamp_intf.so',
        'vendor/lib/libpowerhal.so',
        'vendor/lib64/libpowerhal.so',
        'vendor/lib/hw/audio.primary.mediatek.so',
        'vendor/lib64/hw/audio.primary.mediatek.so',
    ): blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so'),
    (
        'vendor/lib64/libwifi-hal-mtk.so',
    ): blob_fixup()
        .fix_soname(),
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
