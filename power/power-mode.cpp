/*
 * Copyright (C) 2021-2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <aidl/android/hardware/power/BnPower.h>
#include <android-base/file.h>
#include <android-base/logging.h>

namespace aidl::google::hardware::power::impl::pixel {

using ::aidl::android::hardware::power::Mode;

bool isDeviceSpecificModeSupported(Mode type, bool* _aidl_return) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
            *_aidl_return = true;
            return true;
        default:
            return false;
    }
}

bool setDeviceSpecificMode(Mode type, bool enabled) {
    switch (type) {
        case Mode::DOUBLE_TAP_TO_WAKE:
            if (!::android::base::WriteStringToFile(enabled ? "cc1" : "cc2",
                                                    TAP_TO_WAKE_NODE, true)) {
                PLOG(ERROR) << "Failed to write DT2W state (" << (enabled ? "cc1" : "cc2")
                            << ") to " << TAP_TO_WAKE_NODE;
            }
            return true;
        default:
            return false;
    }
}

}  // namespace aidl::google::hardware::power::impl::pixel
