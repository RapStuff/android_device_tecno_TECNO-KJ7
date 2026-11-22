//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "FaceServiceKJ7"

#include "Face.h"

#include <log/log.h>

#include "FaceHal.h"

namespace aidl::android::hardware::biometrics::face::kj7 {

ndk::ScopedAStatus Face::getSensorProps(std::vector<SensorProps>* out) {
    SensorProps props{};
    props.commonProps.sensorId = 1;
    props.commonProps.sensorStrength = common::SensorStrength::CONVENIENCE;
    props.commonProps.maxEnrollmentsPerUser = 5;
    props.sensorType = FaceSensorType::RGB;
    props.halControlsPreview = true;
    props.previewDisplayId = 0;
    props.enrollPreviewWidth = 1080;
    props.enrollPreviewHeight = 1920;
    props.enrollTranslationX = 0.0f;
    props.enrollTranslationY = 0.0f;
    props.enrollPreviewScale = 1.0f;
    props.supportsDetectInteraction = false;

    *out = {std::move(props)};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Face::createSession(int32_t sensorId, int32_t userId,
                                       const std::shared_ptr<ISessionCallback>& cb,
                                       std::shared_ptr<ISession>* out) {
    auto hal = FaceHal::getInstance();
    if (!hal) {
        ALOGE("Failed to open legacy face HAL");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }
    hal->setCallback(cb);
    hal->setActiveGroup(userId, "/data/vendor_de/0/facedata");

    mSession = ndk::SharedRefBase::make<Session>(sensorId, userId, cb, std::move(hal));
    mSession->linkToDeath(cb->asBinder().get());
    *out = mSession;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::biometrics::face::kj7
