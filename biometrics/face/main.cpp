//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "android.hardware.biometrics.face-service.tecno_kj7"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "Face.h"

using aidl::android::hardware::biometrics::face::kj7::Face;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto service = ndk::SharedRefBase::make<Face>();
    const std::string instance = std::string() + Face::descriptor + "/default";
    binder_status_t status =
            AServiceManager_addService(service->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("Failed to register %s: %d", instance.c_str(), status);
        return EXIT_FAILURE;
    }

    ALOGI("Registered native C++ Face HAL: %s", instance.c_str());
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
