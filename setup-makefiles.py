#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from extract_utils.main import ExtractUtils, ExtractUtilsModule

namespace_imports = [
    'device/tecno/TECNO_KJ7',
    'hardware/mediatek',
    'hardware/mediatek/libion_mtk',
    'hardware/mediatek/libmtkperf_client',
]

module = ExtractUtilsModule(
    'TECNO-KJ7',
    'tecno',
    device_rel_path='device/tecno/TECNO_KJ7',
    namespace_imports=namespace_imports,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module, generate_makefiles=True)
    utils.run()
