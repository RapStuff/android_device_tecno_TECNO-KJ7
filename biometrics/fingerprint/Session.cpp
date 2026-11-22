//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "FingerprintSessionKJ7"

#include "Session.h"

#include <android-base/file.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <log/log.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "CancellationSignal.h"
#include "Legacy2Aidl.h"

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

namespace {

constexpr const char* kLcmHbmPath = "/sys/devices/platform/soc/14013000.dsi/lcm_hbm_state";
constexpr const char* kProcFpStatusPath = "/proc/fingerprint_status";
constexpr const char* kProcGestureFuncPath = "/proc/gesture_function";
constexpr const char* kFtsGestureBufPath = "/sys/class/input/event4/device/device/fts_gesture_buf";

constexpr uint32_t kCmdSileadUiReady = 10;
constexpr uint32_t kCmdSileadFingerDown = 11;
constexpr uint32_t kCmdGoodixHbm = 0x10001;

// Kernel touch keys emitted by mtk-tpd / FocalTech FT3683G in suspend/AOD mode
constexpr uint16_t kKeyGesture38 = 38;
constexpr uint16_t kKeyF20 = 190;
constexpr uint16_t kFodKeycode = 195;

// Calibrated optical sensor area (1080x2436)
constexpr int32_t kFodCenterX = 540;
constexpr int32_t kFodCenterY = 2204;
constexpr int32_t kFodTouchRadiusSq = 150 * 150;

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

bool readFtsGesturePoint(int32_t* outX, int32_t* outY) {
    std::string content;
    if (!::android::base::ReadFileToString(kFtsGestureBufPath, &content)) return false;
    auto start = content.find("0(");
    if (start == std::string::npos) return false;
    auto end = content.find(')', start + 2);
    if (end == std::string::npos) return false;
    std::string coords = content.substr(start + 2, end - (start + 2));
    auto comma = coords.find(',');
    if (comma == std::string::npos) return false;
    *outX = std::atoi(coords.substr(0, comma).c_str());
    *outY = std::atoi(coords.substr(comma + 1).c_str());
    return true;
}

bool isInsideFodArea(int32_t x, int32_t y) {
    if (x == 0 && y == 0) return true;
    if (x < 0 || y < 0) return false;
    int64_t dx = x - kFodCenterX;
    int64_t dy = y - kFodCenterY;
    return (dx * dx + dy * dy) <= kFodTouchRadiusSq;
}

int openFodInputFd() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return -1;
    struct dirent* ent;
    int matchedFd = -1;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        std::string path = std::string("/dev/input/") + ent->d_name;
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128] = {};
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
            std::string(name) == "mtk-tpd") {
            matchedFd = fd;
            ALOGI("Opened screen-off FOD touch input: %s (%s)", path.c_str(), name);
            break;
        }
        close(fd);
    }
    closedir(dir);
    return matchedFd;
}

}  // namespace

Session::Session(fingerprint_device_t* device, SileadOps ops, int32_t userId,
                 std::shared_ptr<ISessionCallback> cb, LockoutTracker lockoutTracker)
    : mDevice(device),
      mOps(ops),
      mLockoutTracker(lockoutTracker),
      mUserId(userId),
      mCb(std::move(cb)) {
    mDeathRecipient = AIBinder_DeathRecipient_new(onClientDeath);
    std::string path = ensureFpDataPath(userId);
    if (mDevice && mDevice->set_active_group) {
        int err = mDevice->set_active_group(mDevice, mUserId, path.c_str());
        ALOGI("set_active_group(user=%d, path=%s) -> %d", mUserId, path.c_str(), err);
    }
    startScreenOffMonitor();
}

Session::~Session() {
    stopScreenOffMonitor();
}

void Session::linkToDeath(AIBinder* binder) {
    if (binder && mDeathRecipient) {
        AIBinder_linkToDeath(binder, mDeathRecipient, this);
    }
}

void Session::setHbm(bool enable) {
    const char* val = enable ? "1\n" : "0\n";
    if (!::android::base::WriteStringToFile(val, kLcmHbmPath)) {
        ALOGW("Failed to write %s to %s", enable ? "1" : "0", kLcmHbmPath);
    }
}

bool Session::enableHbmAndWait(int32_t timeoutMs) {
    setHbm(true);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::string state;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!mFingerDown.load(std::memory_order_acquire) ||
            !mActive.load(std::memory_order_acquire)) {
            setHbm(false);
            return false;
        }
        if (::android::base::ReadFileToString(kLcmHbmPath, &state) &&
            state.find("0xOK") != std::string::npos) {
            return true;
        }
        setHbm(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
    ALOGW("LCM HBM did not confirm 0xOK within %dms (last='%s')", timeoutMs, state.c_str());
    return false;
}

void Session::sendVendorCmd(uint32_t cmd, uint32_t arg) {
    if (mOps.custCmd) {
        mOps.custCmd(cmd, arg);
    } else if (mOps.fingerprintCmd && mDevice) {
        mOps.fingerprintCmd(mDevice, cmd == kCmdSileadFingerDown ? kCmdGoodixHbm : cmd, arg);
    }
}

void Session::syncFingerDown() {
    sendVendorCmd(kCmdSileadUiReady, 1);
    sendVendorCmd(kCmdSileadFingerDown, 1);
    if (mOps.syncFingerStatusOptic) {
        mOps.syncFingerStatusOptic(1);
    } else if (mOps.devSyncFingerDown) {
        mOps.devSyncFingerDown();
    } else if (mOps.msgSend) {
        mOps.msgSend(2);  // FINGER_DOWN = 2
    }
}

void Session::syncFingerUp() {
    sendVendorCmd(kCmdSileadFingerDown, 0);
    if (mOps.syncFingerStatusOptic) {
        mOps.syncFingerStatusOptic(0);
    } else if (mOps.devSyncFingerUp) {
        mOps.devSyncFingerUp();
    } else if (mOps.msgSend) {
        mOps.msgSend(3);  // FINGER_UP = 3
    }
}

void Session::armFodGesture(bool enable) {
    ::android::base::WriteStringToFile(enable ? "1" : "0", kProcFpStatusPath);
    if (enable) {
        ::android::base::WriteStringToFile("fp1", kProcGestureFuncPath);
    }
}

void Session::startScreenOffMonitor() {
    mMonitorStop.store(false, std::memory_order_release);
    if (mMonitorThread.joinable()) return;
    mMonitorThread = std::thread([this]() {
        int fd = -1;
        int32_t curX = -1;
        int32_t curY = -1;
        bool monitorDown = false;
        while (!mMonitorStop.load(std::memory_order_acquire) && !isClosed()) {
            if (fd < 0) {
                fd = openFodInputFd();
                if (fd < 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }
            }
            struct pollfd pfd{fd, POLLIN, 0};
            int pret = poll(&pfd, 1, 50);
            if (pret <= 0) continue;
            struct input_event ev{};
            while (read(fd, &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_MT_POSITION_X) curX = ev.value;
                    else if (ev.code == ABS_MT_POSITION_Y) curY = ev.value;
                    else if (ev.code == ABS_MT_TRACKING_ID && ev.value == -1 && monitorDown) {
                        monitorDown = false;
                        onPointerUp(0);
                    }
                } else if (ev.type == EV_KEY) {
                    if ((ev.code == kFodKeycode || ev.code == kKeyGesture38 || ev.code == kKeyF20) &&
                        ev.value == 1 && mActive.load(std::memory_order_acquire) &&
                        mScreenState.load(std::memory_order_acquire) != 1 && !monitorDown) {
                        int32_t tx = curX;
                        int32_t ty = curY;
                        if (tx < 0 || ty < 0) {
                            if (!readFtsGesturePoint(&tx, &ty)) {
                                tx = kFodCenterX;
                                ty = kFodCenterY;
                            }
                        }
                        if (isInsideFodArea(tx, ty)) {
                            ALOGI("Screen-off FOD touch key=%u at (%d,%d)", ev.code, tx, ty);
                            monitorDown = true;
                            onPointerDown(0, tx, ty, 0.0f, 0.0f);
                        }
                    } else if (ev.code == BTN_TOUCH && ev.value == 0) {
                        curX = -1;
                        curY = -1;
                        if (monitorDown) {
                            monitorDown = false;
                            onPointerUp(0);
                        }
                    }
                }
            }
        }
        if (fd >= 0) ::close(fd);
    });
}

void Session::stopScreenOffMonitor() {
    mMonitorStop.store(true, std::memory_order_release);
    if (mMonitorThread.joinable()) {
        mMonitorThread.join();
    }
}

ndk::ScopedAStatus Session::generateChallenge() {
    auto self = ref<Session>();
    std::thread([self]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (self->isClosed()) return;
        uint64_t challenge = (self->mDevice && self->mDevice->pre_enroll)
                                     ? self->mDevice->pre_enroll(self->mDevice)
                                     : 0;
        self->mCb->onChallengeGenerated(static_cast<int64_t>(challenge));
    }).detach();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::revokeChallenge(int64_t challenge) {
    auto self = ref<Session>();
    std::thread([self, challenge]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (self->isClosed()) return;
        if (self->mDevice && self->mDevice->post_enroll) {
            self->mDevice->post_enroll(self->mDevice);
        }
        self->mCb->onChallengeRevoked(challenge);
    }).detach();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enroll(const keymaster::HardwareAuthToken& hat,
                                   std::shared_ptr<common::ICancellationSignal>* out) {
    hw_auth_token_t authToken{};
    translate(hat, authToken);

    mStepCaptured.store(false, std::memory_order_release);
    mFingerDown.store(false, std::memory_order_release);
    mSavedEnrollFid.store(0, std::memory_order_release);
    mActive.store(true, std::memory_order_release);

    if (mOps.msgSend) {
        mOps.msgSend(3);  // Clear stale finger-up pipe message before enrollment
    }

    int err = (mDevice && mDevice->enroll) ? mDevice->enroll(mDevice, &authToken, mUserId, 60) : -1;
    ALOGI("enroll(user=%d) -> %d", mUserId, err);
    if (err != 0) {
        mActive.store(false, std::memory_order_release);
        ALOGE("enroll failed: %d", err);
        mCb->onError(Error::UNABLE_TO_PROCESS, err);
    }
    *out = ndk::SharedRefBase::make<CancellationSignal>(this);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticate(int64_t operationId,
                                         std::shared_ptr<common::ICancellationSignal>* out) {
    mStepCaptured.store(false, std::memory_order_release);
    mFingerDown.store(false, std::memory_order_release);
    if (!checkSensorLockout()) {
        armFodGesture(true);
        mActive.store(true, std::memory_order_release);
        int err = (mDevice && mDevice->authenticate)
                          ? mDevice->authenticate(mDevice, static_cast<uint64_t>(operationId),
                                                  mUserId)
                          : -1;
        ALOGI("authenticate(opId=0x%llx, user=%d) -> %d",
              static_cast<unsigned long long>(operationId), mUserId, err);
        if (err != 0) {
            mActive.store(false, std::memory_order_release);
            armFodGesture(false);
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
    auto self = ref<Session>();
    std::thread([self]() {
        {
            std::lock_guard<std::mutex> lock(self->mEnumMutex);
            self->mEnumeratedIds.clear();
        }
        self->mEnumCompleted.store(false, std::memory_order_release);

        int err = (self->mDevice && self->mDevice->enumerate)
                          ? self->mDevice->enumerate(self->mDevice)
                          : -1;
        if (err != 0) {
            ALOGE("enumerate failed: %d", err);
            self->mEnumCompleted.store(true, std::memory_order_release);
            if (!self->isClosed()) self->mCb->onEnrollmentsEnumerated({});
            return;
        }

        // Silead does not always invoke callback when database is empty; wait briefly then fall back.
        for (int i = 0; i < 10; ++i) {
            if (self->mEnumCompleted.load(std::memory_order_acquire) || self->isClosed()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!self->mEnumCompleted.exchange(true, std::memory_order_acq_rel) && !self->isClosed()) {
            std::vector<int32_t> snapshot;
            {
                std::lock_guard<std::mutex> lock(self->mEnumMutex);
                snapshot = self->mEnumeratedIds;
            }
            self->mCb->onEnrollmentsEnumerated(snapshot);
        }
    }).detach();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::removeEnrollments(const std::vector<int32_t>& enrollmentIds) {
    auto self = ref<Session>();
    std::vector<int32_t> ids = enrollmentIds;
    std::thread([self, ids = std::move(ids)]() {
        if (!self->mDevice || !self->mDevice->remove) {
            if (!self->isClosed()) self->mCb->onEnrollmentsRemoved({});
            return;
        }
        self->mRemovePending.store(true, std::memory_order_release);
        for (int32_t fid : ids) {
            int err = self->mDevice->remove(self->mDevice, self->mUserId, fid);
            if (err != 0) {
                ALOGE("remove(fid=%d) failed: %d", fid, err);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        if (self->mRemovePending.exchange(false, std::memory_order_acq_rel) && !self->isClosed()) {
            self->mCb->onEnrollmentsRemoved(ids);
        }
    }).detach();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getAuthenticatorId() {
    auto self = ref<Session>();
    std::thread([self]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (self->isClosed()) return;
        uint64_t authId = (self->mDevice && self->mDevice->get_authenticator_id)
                                  ? self->mDevice->get_authenticator_id(self->mDevice)
                                  : 0;
        self->mCb->onAuthenticatorIdRetrieved(static_cast<int64_t>(authId));
    }).detach();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::invalidateAuthenticatorId() {
    auto self = ref<Session>();
    std::thread([self]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (self->isClosed()) return;
        uint64_t authId = (self->mDevice && self->mDevice->get_authenticator_id)
                                  ? self->mDevice->get_authenticator_id(self->mDevice)
                                  : 0;
        self->mCb->onAuthenticatorIdInvalidated(static_cast<int64_t>(authId));
    }).detach();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::resetLockout(const keymaster::HardwareAuthToken& /*hat*/) {
    clearLockout(true);
    mIsLockoutTimerAborted.store(true, std::memory_order_release);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerDown(int32_t /*pointerId*/, int32_t x, int32_t y,
                                          float /*minor*/, float /*major*/) {
    if (!mActive.load(std::memory_order_acquire)) {
        return ndk::ScopedAStatus::ok();
    }
    if (!isInsideFodArea(x, y)) {
        return ndk::ScopedAStatus::ok();
    }
    if (mFingerDown.exchange(true, std::memory_order_acq_rel)) {
        return ndk::ScopedAStatus::ok();
    }
    mStepCaptured.store(false, std::memory_order_release);

    ALOGI("onPointerDown(%d, %d) -> enabling HBM and triggering Silead capture", x, y);
    mCb->onAcquired(AcquiredInfo::START, 0);

    enableHbmAndWait(500);
    if (!mFingerDown.load(std::memory_order_acquire) || !mActive.load(std::memory_order_acquire)) {
        setHbm(false);
        return ndk::ScopedAStatus::ok();
    }

    // Settle after HBM confirmation before Silead wait_ui_ready (30ms) + optical exposure.
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    if (!mFingerDown.load(std::memory_order_acquire) || !mActive.load(std::memory_order_acquire)) {
        setHbm(false);
        return ndk::ScopedAStatus::ok();
    }

    syncFingerDown();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerUp(int32_t /*pointerId*/) {
    mFingerDown.store(false, std::memory_order_release);
    mStepCaptured.store(false, std::memory_order_release);
    setHbm(false);
    syncFingerUp();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onUiReady() {
    sendVendorCmd(kCmdSileadUiReady, 1);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticateWithContext(
        int64_t operationId, const common::OperationContext& context,
        std::shared_ptr<common::ICancellationSignal>* out) {
    onContextChanged(context);
    return authenticate(operationId, out);
}

ndk::ScopedAStatus Session::enrollWithContext(const keymaster::HardwareAuthToken& hat,
                                              const common::OperationContext& context,
                                              std::shared_ptr<common::ICancellationSignal>* out) {
    onContextChanged(context);
    return enroll(hat, out);
}

ndk::ScopedAStatus Session::detectInteractionWithContext(
        const common::OperationContext& context,
        std::shared_ptr<common::ICancellationSignal>* out) {
    onContextChanged(context);
    return detectInteraction(out);
}

ndk::ScopedAStatus Session::onPointerDownWithContext(const PointerContext& context) {
    return onPointerDown(context.pointerId, static_cast<int32_t>(context.x),
                         static_cast<int32_t>(context.y), context.minor, context.major);
}

ndk::ScopedAStatus Session::onPointerUpWithContext(const PointerContext& context) {
    return onPointerUp(context.pointerId);
}

ndk::ScopedAStatus Session::onContextChanged(const common::OperationContext& context) {
    const bool isAodOrOff = (context.displayState == common::DisplayState::AOD ||
                             context.displayState == common::DisplayState::NO_UI ||
                             context.displayState == common::DisplayState::SCREENSAVER);
    const int32_t newState = (context.displayState == common::DisplayState::AOD)     ? 2
                           : (context.displayState == common::DisplayState::NO_UI)   ? 0
                                                                                     : 1;
    const int32_t vendorScreenState = isAodOrOff ? 0 : 1;
    mScreenState.store(newState, std::memory_order_release);
    if (mOps.screenStateCallback) {
        mOps.screenStateCallback(vendorScreenState);
    }
    if (isAodOrOff && mActive.load(std::memory_order_acquire)) {
        armFodGesture(true);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::onPointerCancelWithContext(const PointerContext& context) {
    return onPointerUp(context.pointerId);
}

ndk::ScopedAStatus Session::setIgnoreDisplayTouches(bool /*shouldIgnore*/) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::cancel() {
    mActive.store(false, std::memory_order_release);
    mFingerDown.store(false, std::memory_order_release);
    mStepCaptured.store(false, std::memory_order_release);
    setHbm(false);
    syncFingerUp();
    sendVendorCmd(kCmdSileadUiReady, 0);
    armFodGesture(false);

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
        stopScreenOffMonitor();
        mActive.store(false, std::memory_order_release);
        mFingerDown.store(false, std::memory_order_release);
        setHbm(false);
        syncFingerUp();
        sendVendorCmd(kCmdSileadUiReady, 0);
        armFodGesture(false);
        if (mDeathRecipient) {
            AIBinder_DeathRecipient_delete(mDeathRecipient);
            mDeathRecipient = nullptr;
        }
        auto cb = mCb;
        std::thread([cb]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (cb) {
                cb->onSessionClosed();
            }
        }).detach();
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
            mActive.store(false, std::memory_order_release);
            mFingerDown.store(false, std::memory_order_release);
            setHbm(false);
            syncFingerUp();
            sendVendorCmd(kCmdSileadUiReady, 0);
            armFodGesture(false);
            int32_t vendorCode = 0;
            Error result = VendorErrorFilter(msg->data.error, &vendorCode);
            ALOGI("notify ERROR(%d) -> %d (vendor=%d)", msg->data.error, static_cast<int>(result),
                  vendorCode);
            mCb->onError(result, vendorCode);
            break;
        }
        case FINGERPRINT_ACQUIRED: {
            const int32_t acq = msg->data.acquired.acquired_info;
            // Suppress Silead internal optical handshake codes (1001=down, 1002=up, 1020=wait_ui)
            if (acq == 1001 || acq == 1002 || acq == 1020) {
                break;
            }
            int32_t vendorCode = 0;
            AcquiredInfo result = VendorAcquiredFilter(acq, &vendorCode);
            mCb->onAcquired(result, vendorCode);
            break;
        }
        case FINGERPRINT_TEMPLATE_ENROLLING: {
            const int32_t rawFid = static_cast<int32_t>(msg->data.enroll.finger.fid);
            const int32_t remaining = msg->data.enroll.samples_remaining;
            if (mStepCaptured.exchange(true, std::memory_order_acq_rel)) {
                break;
            }
            if (rawFid != 0) {
                mSavedEnrollFid.store(rawFid, std::memory_order_release);
            }
            mFingerDown.store(false, std::memory_order_release);
            setHbm(false);
            syncFingerUp();

            int32_t reportedFid = 0;
            if (remaining == 0) {
                const int32_t saved = mSavedEnrollFid.load(std::memory_order_acquire);
                reportedFid = (saved != 0) ? saved : (rawFid != 0 ? rawFid : 1);
                mActive.store(false, std::memory_order_release);
                sendVendorCmd(kCmdSileadUiReady, 0);
            }
            ALOGI("notify ENROLLING rawFid=%d reportedFid=%d remaining=%d", rawFid, reportedFid,
                  remaining);
            mCb->onEnrollmentProgress(reportedFid, remaining);
            break;
        }
        case FINGERPRINT_TEMPLATE_REMOVED: {
            mRemovePending.store(false, std::memory_order_release);
            std::vector<int32_t> removed{static_cast<int32_t>(msg->data.removed.finger.fid)};
            mCb->onEnrollmentsRemoved(removed);
            break;
        }
        case FINGERPRINT_AUTHENTICATED: {
            mFingerDown.store(false, std::memory_order_release);
            setHbm(false);
            syncFingerUp();
            const int32_t fid = static_cast<int32_t>(msg->data.authenticated.finger.fid);
            ALOGI("notify AUTHENTICATED fid=%d", fid);
            if (fid != 0) {
                mActive.store(false, std::memory_order_release);
                sendVendorCmd(kCmdSileadUiReady, 0);
                armFodGesture(false);
                keymaster::HardwareAuthToken authToken{};
                translate(msg->data.authenticated.hat, authToken);
                mCb->onAuthenticationSucceeded(fid, authToken);
                mLockoutTracker.reset(true);
            } else {
                mCb->onAuthenticationFailed();
                mLockoutTracker.addFailedAttempt();
                checkSensorLockout();
            }
            break;
        }
        case FINGERPRINT_TEMPLATE_ENUMERATING: {
            const int32_t fid = static_cast<int32_t>(msg->data.enumerated.finger.fid);
            std::vector<int32_t> snapshot;
            bool done = false;
            {
                std::lock_guard<std::mutex> lock(mEnumMutex);
                if (fid != 0) {
                    mEnumeratedIds.push_back(fid);
                }
                if (msg->data.enumerated.remaining_templates == 0) {
                    done = true;
                    snapshot = mEnumeratedIds;
                    mEnumeratedIds.clear();
                }
            }
            if (done) {
                mEnumCompleted.store(true, std::memory_order_release);
                mCb->onEnrollmentsEnumerated(snapshot);
            }
            break;
        }
        default:
            break;
    }
}

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
