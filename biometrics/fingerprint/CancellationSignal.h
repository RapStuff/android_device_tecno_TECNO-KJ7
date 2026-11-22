//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <aidl/android/hardware/biometrics/common/BnCancellationSignal.h>
#include <aidl/android/hardware/biometrics/fingerprint/ISession.h>

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

class Session;

class CancellationSignal : public common::BnCancellationSignal {
  public:
    explicit CancellationSignal(Session* session);
    ndk::ScopedAStatus cancel() override;

  private:
    Session* mSession;
};

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
