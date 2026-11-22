//
// SPDX-License-Identifier: Apache-2.0
//

#include "CancellationSignal.h"

namespace aidl::android::hardware::biometrics::face::kj7 {

CancellationSignal::CancellationSignal(std::shared_ptr<FaceHal> hal) : mHal(std::move(hal)) {}

ndk::ScopedAStatus CancellationSignal::cancel() {
    if (mHal) {
        mHal->cancel();
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::biometrics::face::kj7
