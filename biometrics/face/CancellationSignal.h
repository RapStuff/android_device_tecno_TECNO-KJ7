//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <aidl/android/hardware/biometrics/common/BnCancellationSignal.h>
#include <memory>

#include "FaceHal.h"

namespace aidl::android::hardware::biometrics::face::kj7 {

class CancellationSignal : public common::BnCancellationSignal {
  public:
    explicit CancellationSignal(std::shared_ptr<FaceHal> hal);
    ndk::ScopedAStatus cancel() override;

  private:
    std::shared_ptr<FaceHal> mHal;
};

}  // namespace aidl::android::hardware::biometrics::face::kj7
