//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <aidl/android/hardware/biometrics/face/BnSession.h>
#include <aidl/android/hardware/biometrics/face/FaceEnrollOptions.h>
#include <aidl/android/hardware/biometrics/face/ISessionCallback.h>
#include <aidl/android/hardware/common/NativeHandle.h>

#include <memory>

#include "FaceHal.h"

namespace aidl::android::hardware::biometrics::face::kj7 {

using HardwareNativeHandle = ::aidl::android::hardware::common::NativeHandle;

class Session : public BnSession {
  public:
    Session(int32_t sensorId, int32_t userId, std::shared_ptr<ISessionCallback> cb,
            std::shared_ptr<FaceHal> hal);

    ndk::ScopedAStatus generateChallenge() override;
    ndk::ScopedAStatus revokeChallenge(int64_t challenge) override;
    ndk::ScopedAStatus getEnrollmentConfig(EnrollmentType enrollmentType,
                                           std::vector<EnrollmentStageConfig>* out) override;
    ndk::ScopedAStatus enroll(const keymaster::HardwareAuthToken& hat,
                              EnrollmentType enrollmentType, const std::vector<Feature>& features,
                              const std::optional<HardwareNativeHandle>& previewSurface,
                              std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus authenticate(int64_t operationId,
                                    std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus detectInteraction(
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus enumerateEnrollments() override;
    ndk::ScopedAStatus removeEnrollments(const std::vector<int32_t>& enrollmentIds) override;
    ndk::ScopedAStatus getFeatures() override;
    ndk::ScopedAStatus setFeature(const keymaster::HardwareAuthToken& hat, Feature feature,
                                  bool enabled) override;
    ndk::ScopedAStatus getAuthenticatorId() override;
    ndk::ScopedAStatus invalidateAuthenticatorId() override;
    ndk::ScopedAStatus resetLockout(const keymaster::HardwareAuthToken& hat) override;
    ndk::ScopedAStatus close() override;

    ndk::ScopedAStatus authenticateWithContext(
            int64_t operationId, const common::OperationContext& context,
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus enrollWithContext(
            const keymaster::HardwareAuthToken& hat, EnrollmentType enrollmentType,
            const std::vector<Feature>& features,
            const std::optional<HardwareNativeHandle>& previewSurface,
            const common::OperationContext& context,
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus detectInteractionWithContext(
            const common::OperationContext& context,
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus onContextChanged(const common::OperationContext& context) override;
    ndk::ScopedAStatus enrollWithOptions(
            const FaceEnrollOptions& options,
            std::shared_ptr<common::ICancellationSignal>* out) override;

    void linkToDeath(AIBinder* binder);
    bool isClosed() const { return mClosed; }

  private:
    std::shared_ptr<ISessionCallback> mCb;
    std::shared_ptr<FaceHal> mHal;
    AIBinder_DeathRecipient* mDeathRecipient = nullptr;
    bool mClosed = false;
};

}  // namespace aidl::android::hardware::biometrics::face::kj7
