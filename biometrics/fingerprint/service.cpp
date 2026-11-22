//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "android.hardware.biometrics.fingerprint-service.tecno_kj7"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "Fingerprint.h"

using aidl::android::hardware::biometrics::fingerprint::kj7::Fingerprint;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    auto fingerprint = ndk::SharedRefBase::make<Fingerprint>();
    const std::string instance = std::string() + Fingerprint::descriptor + "/default";
    binder_status_t status =
            AServiceManager_addService(fingerprint->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("Failed to register %s: %d", instance.c_str(), status);
        return EXIT_FAILURE;
    }

    ALOGI("Registered native C++ UDFPS Fingerprint HAL: %s", instance.c_str());
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
