//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <aidl/android/hardware/biometrics/face/BnFace.h>
#include <memory>

#include "Session.h"

namespace aidl::android::hardware::biometrics::face::kj7 {

class Face : public BnFace {
  public:
    Face() = default;

    ndk::ScopedAStatus getSensorProps(std::vector<SensorProps>* out) override;
    ndk::ScopedAStatus createSession(int32_t sensorId, int32_t userId,
                                     const std::shared_ptr<ISessionCallback>& cb,
                                     std::shared_ptr<ISession>* out) override;

  private:
    std::shared_ptr<Session> mSession;
};

}  // namespace aidl::android::hardware::biometrics::face::kj7
