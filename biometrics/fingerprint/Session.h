//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <aidl/android/hardware/biometrics/fingerprint/BnSession.h>
#include <aidl/android/hardware/biometrics/fingerprint/ISessionCallback.h>
#include <hardware/fingerprint.h>

#include <atomic>
#include <memory>

#include "LockoutTracker.h"

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

using FingerprintCmdFn = int (*)(fingerprint_device_t*, uint32_t, uint32_t);

class Session : public BnSession {
  public:
    Session(fingerprint_device_t* device, FingerprintCmdFn cmdFn, int32_t userId,
            std::shared_ptr<ISessionCallback> cb, LockoutTracker lockoutTracker);

    ndk::ScopedAStatus generateChallenge() override;
    ndk::ScopedAStatus revokeChallenge(int64_t challenge) override;
    ndk::ScopedAStatus enroll(const keymaster::HardwareAuthToken& hat,
                              std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus authenticate(int64_t operationId,
                                    std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus detectInteraction(
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus enumerateEnrollments() override;
    ndk::ScopedAStatus removeEnrollments(const std::vector<int32_t>& enrollmentIds) override;
    ndk::ScopedAStatus getAuthenticatorId() override;
    ndk::ScopedAStatus invalidateAuthenticatorId() override;
    ndk::ScopedAStatus resetLockout(const keymaster::HardwareAuthToken& hat) override;
    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus onPointerDown(int32_t pointerId, int32_t x, int32_t y, float minor,
                                     float major) override;
    ndk::ScopedAStatus onPointerUp(int32_t pointerId) override;
    ndk::ScopedAStatus onUiReady() override;
    ndk::ScopedAStatus authenticateWithContext(
            int64_t operationId, const common::OperationContext& context,
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus enrollWithContext(
            const keymaster::HardwareAuthToken& hat, const common::OperationContext& context,
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus detectInteractionWithContext(
            const common::OperationContext& context,
            std::shared_ptr<common::ICancellationSignal>* out) override;
    ndk::ScopedAStatus onPointerDownWithContext(const PointerContext& context) override;
    ndk::ScopedAStatus onPointerUpWithContext(const PointerContext& context) override;
    ndk::ScopedAStatus onContextChanged(const common::OperationContext& context) override;
    ndk::ScopedAStatus onPointerCancelWithContext(const PointerContext& context) override;
    ndk::ScopedAStatus setIgnoreDisplayTouches(bool shouldIgnore) override;

    ndk::ScopedAStatus cancel();
    void notify(const fingerprint_msg_t* msg);
    void linkToDeath(AIBinder* binder);
    bool isClosed() const { return mClosed.load(std::memory_order_acquire); }

  private:
    bool checkSensorLockout();
    void clearLockout(bool clearAttemptCounter);
    void startLockoutTimer(int64_t timeoutMs);
    void setHbm(bool enable);
    void sendVendorCmd(uint32_t cmd, uint32_t arg);

    static Error VendorErrorFilter(int32_t error, int32_t* vendorCode);
    static AcquiredInfo VendorAcquiredFilter(int32_t info, int32_t* vendorCode);

    fingerprint_device_t* mDevice;
    FingerprintCmdFn mCmdFn;
    LockoutTracker mLockoutTracker;
    int32_t mUserId;
    std::shared_ptr<ISessionCallback> mCb;
    AIBinder_DeathRecipient* mDeathRecipient = nullptr;

    std::atomic<bool> mClosed{false};
    std::atomic<bool> mIsLockoutTimerStarted{false};
    std::atomic<bool> mIsLockoutTimerAborted{false};
};

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
