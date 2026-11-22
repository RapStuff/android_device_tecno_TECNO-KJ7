//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <aidl/android/hardware/biometrics/fingerprint/BnFingerprint.h>
#include <hardware/fingerprint.h>

#include <memory>
#include <vector>

#include "LockoutTracker.h"
#include "Session.h"

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

class Fingerprint : public BnFingerprint {
  public:
    Fingerprint();
    ~Fingerprint() override;

    ndk::ScopedAStatus getSensorProps(std::vector<SensorProps>* out) override;
    ndk::ScopedAStatus createSession(int32_t sensorId, int32_t userId,
                                     const std::shared_ptr<ISessionCallback>& cb,
                                     std::shared_ptr<ISession>* out) override;

  private:
    fingerprint_device_t* openHal();
    static void notify(const fingerprint_msg_t* msg);
    static std::vector<SensorLocation> getSensorLocations();

    fingerprint_device_t* mDevice = nullptr;
    SileadOps mSileadOps{};
    LockoutTracker mLockoutTracker;
    std::shared_ptr<Session> mSession;
};

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
