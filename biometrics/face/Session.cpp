//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "FaceSessionKJ7"

#include "Session.h"

#include <log/log.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#include "CancellationSignal.h"

namespace aidl::android::hardware::biometrics::face::kj7 {

namespace {

void onClientDeath(void* cookie) {
    auto* session = static_cast<Session*>(cookie);
    if (session && !session->isClosed()) {
        ALOGI("FaceService client died; closing session");
        session->close();
    }
}

}  // namespace

Session::Session(int32_t /*sensorId*/, int32_t /*userId*/, std::shared_ptr<ISessionCallback> cb,
                 std::shared_ptr<FaceHal> hal)
    : mCb(std::move(cb)), mHal(std::move(hal)) {
    mDeathRecipient = AIBinder_DeathRecipient_new(onClientDeath);
}

void Session::linkToDeath(AIBinder* binder) {
    if (binder && mDeathRecipient) {
        AIBinder_linkToDeath(binder, mDeathRecipient, this);
    }
}

ndk::ScopedAStatus Session::generateChallenge() {
    uint64_t challenge = mHal->preEnroll();
    mCb->onChallengeGenerated(static_cast<int64_t>(challenge));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::revokeChallenge(int64_t challenge) {
    mHal->postEnroll();
    mCb->onChallengeRevoked(challenge);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getEnrollmentConfig(EnrollmentType /*enrollmentType*/,
                                                std::vector<EnrollmentStageConfig>* out) {
    *out = {};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enroll(
        const keymaster::HardwareAuthToken& hat, EnrollmentType /*enrollmentType*/,
        const std::vector<Feature>& /*features*/,
        const std::optional<HardwareNativeHandle>& previewSurface,
        std::shared_ptr<common::ICancellationSignal>* out) {
    if (previewSurface.has_value() && !previewSurface->ints.empty()) {
        mHal->attachPreviewToken(previewSurface->ints);
    }
    mHal->startEnrollCamera();
    int ret = mHal->enroll(hat, 60);
    if (ret != 0) {
        ALOGW("FaceHal::enroll returned %d", ret);
        mHal->stopEnrollCamera();
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(mHal);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticate(int64_t operationId,
                                         std::shared_ptr<common::ICancellationSignal>* out) {
    int ret = mHal->authenticate(static_cast<uint64_t>(operationId));
    if (ret != 0) {
        ALOGW("FaceHal::authenticate returned %d", ret);
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(mHal);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::detectInteraction(
        std::shared_ptr<common::ICancellationSignal>* out) {
    *out = ndk::SharedRefBase::make<CancellationSignal>(mHal);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enumerateEnrollments() {
    auto ids = parseEnrolledFaceIds();
    mCb->onEnrollmentsEnumerated(ids);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::removeEnrollments(const std::vector<int32_t>& enrollmentIds) {
    auto currentIds = parseEnrolledFaceIds();
    bool removeAll = enrollmentIds.empty() ||
                     std::find(enrollmentIds.begin(), enrollmentIds.end(), 0) != enrollmentIds.end();
    if (!removeAll && !currentIds.empty()) {
        removeAll = std::all_of(currentIds.begin(), currentIds.end(), [&](int32_t id) {
            return std::find(enrollmentIds.begin(), enrollmentIds.end(), id) != enrollmentIds.end();
        });
    }

    if (removeAll) {
        for (int32_t id : currentIds) {
            if (id > 0) mHal->remove(static_cast<uint32_t>(id));
        }
        mHal->remove(0);
        unlink("/data/vendor_de/0/facedata/template_file.data");
        unlink("/data/vendor_de/0/facedata/template_file.data.bak");
        unlink("/data/vendor_de/0/facedata/feature_ocular.txt");
        unlink("/data/vendor_de/0/facedata/faceIdEnroll.yuv");
    } else {
        for (int32_t id : enrollmentIds) {
            if (id > 0) mHal->remove(static_cast<uint32_t>(id));
        }
    }

    if (enrollmentIds.empty()) {
        mCb->onEnrollmentsRemoved(removeAll ? std::vector<int32_t>{0} : std::vector<int32_t>{});
    } else {
        mCb->onEnrollmentsRemoved(enrollmentIds);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getFeatures() {
    mCb->onFeaturesRetrieved({});
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::setFeature(const keymaster::HardwareAuthToken& /*hat*/, Feature feature,
                                       bool /*enabled*/) {
    mCb->onFeatureSet(feature);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getAuthenticatorId() {
    mCb->onAuthenticatorIdRetrieved(static_cast<int64_t>(mHal->getAuthenticatorId()));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::invalidateAuthenticatorId() {
    mCb->onAuthenticatorIdInvalidated(static_cast<int64_t>(mHal->getAuthenticatorId()));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::resetLockout(const keymaster::HardwareAuthToken& hat) {
    mHal->resetLockout(hat);
    mCb->onLockoutCleared();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::close() {
    if (!mClosed) {
        mClosed = true;
        mHal->cancel();
        mHal->setCallback(nullptr);
        if (mCb) {
            mCb->onSessionClosed();
        }
        if (mDeathRecipient) {
            AIBinder_DeathRecipient_delete(mDeathRecipient);
            mDeathRecipient = nullptr;
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticateWithContext(
        int64_t operationId, const common::OperationContext& /*context*/,
        std::shared_ptr<common::ICancellationSignal>* out) {
    return authenticate(operationId, out);
}

ndk::ScopedAStatus Session::enrollWithContext(
        const keymaster::HardwareAuthToken& hat, EnrollmentType enrollmentType,
        const std::vector<Feature>& features,
        const std::optional<HardwareNativeHandle>& previewSurface,
        const common::OperationContext& /*context*/,
        std::shared_ptr<common::ICancellationSignal>* out) {
    return enroll(hat, enrollmentType, features, previewSurface, out);
}

ndk::ScopedAStatus Session::detectInteractionWithContext(
        const common::OperationContext& /*context*/,
        std::shared_ptr<common::ICancellationSignal>* out) {
    return detectInteraction(out);
}

ndk::ScopedAStatus Session::onContextChanged(const common::OperationContext& /*context*/) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enrollWithOptions(const FaceEnrollOptions& options,
                                              std::shared_ptr<common::ICancellationSignal>* out) {
    if (options.surfacePreview.has_value() && options.surfacePreview->get() != nullptr) {
        mHal->attachPreviewWindow(options.surfacePreview->get());
    } else if (options.nativeHandlePreview.has_value() &&
               !options.nativeHandlePreview->ints.empty()) {
        mHal->attachPreviewToken(options.nativeHandlePreview->ints);
    }

    mHal->startEnrollCamera();
    int ret = mHal->enroll(options.hardwareAuthToken, 60);
    if (ret != 0) {
        ALOGW("FaceHal::enroll (with options) returned %d", ret);
        mHal->stopEnrollCamera();
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(mHal);
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::biometrics::face::kj7
