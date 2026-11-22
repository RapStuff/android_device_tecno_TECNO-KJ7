//
// SPDX-License-Identifier: Apache-2.0
//

#include "LockoutTracker.h"

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

namespace {
constexpr int32_t kTimedThreshold = 5;
constexpr int64_t kTimedDurationMs = 30000;
constexpr int32_t kPermanentThreshold = 20;
}  // namespace

void LockoutTracker::reset(bool clearAttemptCounter) {
    if (clearAttemptCounter) {
        mFailedCount = 0;
    }
    mTimedStart = {};
    mMode = LockoutMode::kNone;
}

void LockoutTracker::addFailedAttempt() {
    ++mFailedCount;
    if (mFailedCount >= kPermanentThreshold) {
        mMode = LockoutMode::kPermanent;
    } else if (mFailedCount >= kTimedThreshold && mMode == LockoutMode::kNone) {
        mMode = LockoutMode::kTimed;
        mTimedStart = std::chrono::steady_clock::now();
    }
}

LockoutTracker::LockoutMode LockoutTracker::getMode() {
    if (mMode == LockoutMode::kTimed) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - mTimedStart)
                               .count();
        if (elapsed >= kTimedDurationMs) {
            mMode = LockoutMode::kNone;
            mTimedStart = {};
        }
    }
    return mMode;
}

int64_t LockoutTracker::getLockoutTimeLeft() {
    if (mMode != LockoutMode::kTimed) return 0;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - mTimedStart)
                           .count();
    return elapsed >= kTimedDurationMs ? 0 : (kTimedDurationMs - elapsed);
}

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
