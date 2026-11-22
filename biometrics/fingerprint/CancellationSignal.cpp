//
// SPDX-License-Identifier: Apache-2.0
//

#include "CancellationSignal.h"
#include "Session.h"

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

CancellationSignal::CancellationSignal(Session* session) : mSession(session) {}

ndk::ScopedAStatus CancellationSignal::cancel() {
    if (mSession && !mSession->isClosed()) {
        return mSession->cancel();
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
