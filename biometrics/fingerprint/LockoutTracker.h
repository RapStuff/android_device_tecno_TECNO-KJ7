//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <chrono>
#include <cstdint>

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

class LockoutTracker {
  public:
    enum class LockoutMode : int8_t { kNone = 0, kTimed, kPermanent };

    void reset(bool clearAttemptCounter);
    LockoutMode getMode();
    void addFailedAttempt();
    int64_t getLockoutTimeLeft();

  private:
    int32_t mFailedCount = 0;
    std::chrono::steady_clock::time_point mTimedStart{};
    LockoutMode mMode = LockoutMode::kNone;
};

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
