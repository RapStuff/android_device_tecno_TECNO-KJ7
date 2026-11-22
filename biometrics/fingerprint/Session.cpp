//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "FingerprintSessionKJ7"

#include "Session.h"

#include <android-base/file.h>
#include <log/log.h>
#include <sys/stat.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "CancellationSignal.h"
#include "Legacy2Aidl.h"

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

namespace {

constexpr const char* kLcmHbmPath = "/sys/devices/platform/soc/14013000.dsi/lcm_hbm_state";
constexpr uint32_t kCmdSileadUiReady = 10;
constexpr uint32_t kCmdSileadFingerDown = 11;

void onClientDeath(void* cookie) {
    auto* session = static_cast<Session*>(cookie);
    if (session && !session->isClosed()) {
        ALOGI("FingerprintService died; closing session");
        session->close();
    }
}

std::string ensureFpDataPath(int32_t userId) {
    mkdir("/data/vendor_de", 0700);
    std::string userDir = "/data/vendor_de/" + std::to_string(userId);
    mkdir(userDir.c_str(), 0700);
    std::string fpDir = userDir + "/fpdata";
    mkdir(fpDir.c_str(), 0700);
    return fpDir;
}

}  // namespace

Session::Session(fingerprint_device_t* device, FingerprintCmdFn cmdFn, int32_t userId,
                 std::shared_ptr<ISessionCallback> cb, LockoutTracker lockoutTracker)
    : mDevice(device),
      mCmdFn(cmdFn),
      mLockoutTracker(lockoutTracker),
      mUserId(userId),
      mCb(std::move(cb)) {
    mDeathRecipient = AIBinder_DeathRecipient_new(onClientDeath);
    std::string path = ensureFpDataPath(userId);
    if (mDevice && mDevice->set_active_group) {
        int err = mDevice->set_active_group(mDevice, mUserId, path.c_str());
        ALOGI("set_active_group(user=%d, path=%s) -> %d", mUserId, path.c_str(), err);
    }
}

void Session::linkToDeath(AIBinder* binder) {
    if (binder && mDeathRecipient) {
        AIBinder_linkToDeath(binder, mDeathRecipient, this);
    }
}

void Session::setHbm(bool enable) {
    if (!::android::base::WriteStringToFile(enable ? "1" : "0", kLcmHbmPath)) {
        ALOGW("Failed to write %s to %s", enable ? "1" : "0", kLcmHbmPath);
    }
}

void Session::sendVendorCmd(uint32_t cmd, uint32_t arg) {
    if (mCmdFn && mDevice) {
        mCmdFn(mDevice, cmd, arg);
    }
}

ndk::ScopedAStatus Session::generateChallenge() {
    uint64_t challenge = (mDevice && mDevice->pre_enroll) ? mDevice->pre_enroll(mDevice) : 0;
    mCb->onChallengeGenerated(static_cast<int64_t>(challenge));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::revokeChallenge(int64_t challenge) {
    if (mDevice && mDevice->post_enroll) {
        mDevice->post_enroll(mDevice);
    }
    mCb->onChallengeRevoked(challenge);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enroll(const keymaster::HardwareAuthToken& hat,
                                   std::shared_ptr<common::ICancellationSignal>* out) {
    hw_auth_token_t authToken{};
    translate(hat, authToken);
    int err = (mDevice && mDevice->enroll) ? mDevice->enroll(mDevice, &authToken, mUserId, 60) : -1;
    if (err != 0) {
        ALOGE("enroll failed: %d", err);
        mCb->onError(Error::UNABLE_TO_PROCESS, err);
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(this);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticate(int64_t operationId,
                                         std::shared_ptr<common::ICancellationSignal>* out) {
    if (!checkSensorLockout()) {
        int err = (mDevice && mDevice->authenticate)
                          ? mDevice->authenticate(mDevice, static_cast<uint64_t>(operationId),
                                                  mUserId)
                          : -1;
        if (err != 0) {
            ALOGE("authenticate failed: %d", err);
            mCb->onError(Error::UNABLE_TO_PROCESS, err);
        }
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(this);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::detectInteraction(std::shared_ptr<common::ICancellationSignal>* out) {
    mCb->onError(Error::UNABLE_TO_PROCESS, 0);
    *out = ndk::SharedRefBase::make<CancellationSignal>(this);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enumerateEnrollments() {
    int err = (mDevice && mDevice->enumerate) ? mDevice->enumerate(mDevice) : -1;
    if (err != 0) {
        ALOGE("enumerate failed: %d", err);
        mCb->onEnrollmentsEnumerated({});
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::removeEnrollments(const std::vector<int32_t>& enrollmentIds) {
    if (!mDevice || !mDevice->remove) {
        mCb->onEnrollmentsRemoved({});
        return ndk::ScopedAStatus::ok();
    }
    for (int32_t fid : enrollmentIds) {
        int err = mDevice->remove(mDevice, mUserId, fid);
        if (err != 0) {
            ALOGE("remove(fid=%d) failed: %d", fid, err);
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getAuthenticatorId() {
    uint64_t authId = (mDevice && mDevice->get_authenticator_id)
                              ? mDevice->get_authenticator_id(mDevice)
                              : 0;
    mCb->onAuthenticatorIdRetrieved(static_cast<int64_t>(authId));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::invalidateAuthenticatorId() {
    uint64_t authId = (mDevice && mDevice->get_authenticator_id)
                              ? mDevice->get_authenticator_id(mDevice)
                              : 0;
    mCb->onAuthenticatorIdInvalidated(static_cast<int64_t>(authId));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::resetLockout(const keymaster::HardwareAuthToken& /*hat*/) {
    clearLockout(true);
    mIsLockoutTimerAborted.store(true, std::memory_order_release);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerDown(int32_t /*pointerId*/, int32_t /*x*/, int32_t /*y*/,
                                          float /*minor*/, float /*major*/) {
    setHbm(true);
    sendVendorCmd(kCmdSileadUiReady, 1);
    sendVendorCmd(kCmdSileadFingerDown, 1);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerUp(int32_t /*pointerId*/) {
    setHbm(false);
    sendVendorCmd(kCmdSileadUiReady, 0);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onUiReady() {
    sendVendorCmd(kCmdSileadUiReady, 1);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticateWithContext(
        int64_t operationId, const common::OperationContext& /*context*/,
        std::shared_ptr<common::ICancellationSignal>* out) {
    return authenticate(operationId, out);
}

ndk::ScopedAStatus Session::enrollWithContext(const keymaster::HardwareAuthToken& hat,
                                              const common::OperationContext& /*context*/,
                                              std::shared_ptr<common::ICancellationSignal>* out) {
    return enroll(hat, out);
}

ndk::ScopedAStatus Session::detectInteractionWithContext(
        const common::OperationContext& /*context*/,
        std::shared_ptr<common::ICancellationSignal>* out) {
    return detectInteraction(out);
}

ndk::ScopedAStatus Session::onPointerDownWithContext(const PointerContext& context) {
    return onPointerDown(context.pointerId, context.x, context.y, context.minor, context.major);
}

ndk::ScopedAStatus Session::onPointerUpWithContext(const PointerContext& context) {
    return onPointerUp(context.pointerId);
}

ndk::ScopedAStatus Session::onContextChanged(const common::OperationContext& /*context*/) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerCancelWithContext(const PointerContext& context) {
    return onPointerUp(context.pointerId);
}

ndk::ScopedAStatus Session::setIgnoreDisplayTouches(bool /*shouldIgnore*/) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::cancel() {
    setHbm(false);
    sendVendorCmd(kCmdSileadUiReady, 0);
    if (!mDevice || !mDevice->cancel) {
        mCb->onError(Error::CANCELED, 0);
        return ndk::ScopedAStatus::ok();
    }
    int ret = mDevice->cancel(mDevice);
    if (ret == 0) {
        mCb->onError(Error::CANCELED, 0);
        return ndk::ScopedAStatus::ok();
    }
    return ndk::ScopedAStatus::fromServiceSpecificError(ret);
}

ndk::ScopedAStatus Session::close() {
    if (!mClosed.exchange(true, std::memory_order_acq_rel)) {
        setHbm(false);
        sendVendorCmd(kCmdSileadUiReady, 0);
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

Error Session::VendorErrorFilter(int32_t error, int32_t* vendorCode) {
    *vendorCode = 0;
    switch (error) {
        case FINGERPRINT_ERROR_HW_UNAVAILABLE:
            return Error::HW_UNAVAILABLE;
        case FINGERPRINT_ERROR_UNABLE_TO_PROCESS:
            return Error::UNABLE_TO_PROCESS;
        case FINGERPRINT_ERROR_TIMEOUT:
            return Error::TIMEOUT;
        case FINGERPRINT_ERROR_NO_SPACE:
            return Error::NO_SPACE;
        case FINGERPRINT_ERROR_CANCELED:
            return Error::CANCELED;
        case FINGERPRINT_ERROR_UNABLE_TO_REMOVE:
            return Error::UNABLE_TO_REMOVE;
        case FINGERPRINT_ERROR_LOCKOUT:
            *vendorCode = FINGERPRINT_ERROR_LOCKOUT;
            return Error::VENDOR;
        default:
            if (error >= FINGERPRINT_ERROR_VENDOR_BASE) {
                *vendorCode = error - FINGERPRINT_ERROR_VENDOR_BASE;
                return Error::VENDOR;
            }
            return Error::UNABLE_TO_PROCESS;
    }
}

AcquiredInfo Session::VendorAcquiredFilter(int32_t info, int32_t* vendorCode) {
    *vendorCode = 0;
    switch (info) {
        case FINGERPRINT_ACQUIRED_GOOD:
            return AcquiredInfo::GOOD;
        case FINGERPRINT_ACQUIRED_PARTIAL:
            return AcquiredInfo::PARTIAL;
        case FINGERPRINT_ACQUIRED_INSUFFICIENT:
            return AcquiredInfo::INSUFFICIENT;
        case FINGERPRINT_ACQUIRED_IMAGER_DIRTY:
            return AcquiredInfo::SENSOR_DIRTY;
        case FINGERPRINT_ACQUIRED_TOO_SLOW:
            return AcquiredInfo::TOO_SLOW;
        case FINGERPRINT_ACQUIRED_TOO_FAST:
            return AcquiredInfo::TOO_FAST;
        default:
            if (info >= FINGERPRINT_ACQUIRED_VENDOR_BASE) {
                *vendorCode = info - FINGERPRINT_ACQUIRED_VENDOR_BASE;
                return AcquiredInfo::VENDOR;
            }
            return AcquiredInfo::UNKNOWN;
    }
}

bool Session::checkSensorLockout() {
    auto mode = mLockoutTracker.getMode();
    if (mode == LockoutTracker::LockoutMode::kPermanent) {
        mCb->onLockoutPermanent();
        mIsLockoutTimerAborted.store(true, std::memory_order_release);
        return true;
    }
    if (mode == LockoutTracker::LockoutMode::kTimed) {
        int64_t left = mLockoutTracker.getLockoutTimeLeft();
        mCb->onLockoutTimed(left);
        if (!mIsLockoutTimerStarted.load(std::memory_order_acquire)) {
            startLockoutTimer(left);
        }
        return true;
    }
    return false;
}

void Session::clearLockout(bool clearAttemptCounter) {
    mLockoutTracker.reset(clearAttemptCounter);
    mCb->onLockoutCleared();
}

void Session::startLockoutTimer(int64_t timeoutMs) {
    mIsLockoutTimerStarted.store(true, std::memory_order_release);
    std::thread([this, timeoutMs]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        if (!mClosed.load(std::memory_order_acquire) &&
            !mIsLockoutTimerAborted.load(std::memory_order_acquire)) {
            clearLockout(false);
        }
        mIsLockoutTimerStarted.store(false, std::memory_order_release);
        mIsLockoutTimerAborted.store(false, std::memory_order_release);
    }).detach();
}

void Session::notify(const fingerprint_msg_t* msg) {
    if (!msg || !mCb || isClosed()) return;

    switch (msg->type) {
        case FINGERPRINT_ERROR: {
            setHbm(false);
            sendVendorCmd(kCmdSileadUiReady, 0);
            int32_t vendorCode = 0;
            Error result = VendorErrorFilter(msg->data.error, &vendorCode);
            mCb->onError(result, vendorCode);
            break;
        }
        case FINGERPRINT_ACQUIRED: {
            int32_t vendorCode = 0;
            AcquiredInfo result =
                    VendorAcquiredFilter(msg->data.acquired.acquired_info, &vendorCode);
            mCb->onAcquired(result, vendorCode);
            break;
        }
        case FINGERPRINT_TEMPLATE_ENROLLING: {
            if (msg->data.enroll.samples_remaining == 0) {
                setHbm(false);
                sendVendorCmd(kCmdSileadUiReady, 0);
            }
            mCb->onEnrollmentProgress(msg->data.enroll.finger.fid,
                                      msg->data.enroll.samples_remaining);
            break;
        }
        case FINGERPRINT_TEMPLATE_REMOVED: {
            std::vector<int32_t> removed{static_cast<int32_t>(msg->data.removed.finger.fid)};
            mCb->onEnrollmentsRemoved(removed);
            break;
        }
        case FINGERPRINT_AUTHENTICATED: {
            setHbm(false);
            if (msg->data.authenticated.finger.fid != 0) {
                sendVendorCmd(kCmdSileadUiReady, 0);
                keymaster::HardwareAuthToken authToken{};
                translate(msg->data.authenticated.hat, authToken);
                mCb->onAuthenticationSucceeded(msg->data.authenticated.finger.fid, authToken);
                mLockoutTracker.reset(true);
            } else {
                mCb->onAuthenticationFailed();
                mLockoutTracker.addFailedAttempt();
                checkSensorLockout();
            }
            break;
        }
        case FINGERPRINT_TEMPLATE_ENUMERATING: {
            static std::vector<int32_t> enrollments;
            if (msg->data.enumerated.finger.fid != 0) {
                enrollments.push_back(static_cast<int32_t>(msg->data.enumerated.finger.fid));
            }
            if (msg->data.enumerated.remaining_templates == 0) {
                mCb->onEnrollmentsEnumerated(enrollments);
                enrollments.clear();
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
