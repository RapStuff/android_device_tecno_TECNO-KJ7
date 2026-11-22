//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <aidl/android/hardware/biometrics/face/ISessionCallback.h>
#include <aidl/android/hardware/keymaster/HardwareAuthToken.h>
#include <hardware/hardware.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "SurfaceBridge.h"

namespace aidl::android::hardware::biometrics::face::kj7 {

struct __attribute__((packed)) RawFaceHat {
    uint8_t version;
    uint64_t challenge;
    uint64_t userId;
    uint64_t authenticatorId;
    uint32_t authenticatorType;
    uint64_t timestamp;
    uint8_t hmac[32];
};

static_assert(sizeof(RawFaceHat) == 69, "RawFaceHat must be 69 bytes");

struct FaceMsg {
    int32_t type;
    int32_t reserved;
    int32_t faceIdOrError;
    uint8_t extra[80];
};

using face_notify_t = void (*)(const FaceMsg* msg);

struct face_device_t {
    hw_device_t common;
    uint64_t reserved_78;
    face_notify_t notify;
    int (*set_notify)(face_device_t* dev, face_notify_t notify);
    int (*pre_enroll)(face_device_t* dev, uint32_t timeoutSec, uint64_t* challenge);
    int (*enroll)(face_device_t* dev, const RawFaceHat* hat, uint32_t timeoutSec,
                  const int32_t* disabledFeatures, size_t count);
    int (*post_enroll)(face_device_t* dev);
    int (*set_feature)(face_device_t* dev, int32_t feature, bool enabled, const RawFaceHat* hat,
                       int32_t faceId);
    int (*get_feature)(face_device_t* dev, int32_t feature, int32_t faceId, bool* result);
    int (*get_authenticator_id)(face_device_t* dev, uint64_t* authId);
    int (*cancel)(face_device_t* dev);
    int (*enumerate)(face_device_t* dev);
    int (*remove)(face_device_t* dev, uint32_t faceId);
    int (*set_active_group)(face_device_t* dev, uint32_t userId, const char* storePath);
    int (*authenticate)(face_device_t* dev, uint64_t operationId);
    uint64_t reserved_e8;
    int (*reset_lockout)(face_device_t* dev, const RawFaceHat* hat);
};

std::vector<int32_t> parseEnrolledFaceIds();

class FaceHal {
  public:
    static std::shared_ptr<FaceHal> getInstance();

    void setCallback(std::shared_ptr<ISessionCallback> cb);
    int setActiveGroup(int32_t userId, const char* storePath);
    uint64_t preEnroll();
    int postEnroll();
    int enroll(const keymaster::HardwareAuthToken& hat, uint32_t timeoutSec);
    int authenticate(uint64_t operationId);
    int cancel();
    int enumerate();
    int remove(uint32_t faceId);
    uint64_t getAuthenticatorId();
    int resetLockout(const keymaster::HardwareAuthToken& hat);

    void attachPreviewWindow(ANativeWindow* window);
    void attachPreviewToken(const std::vector<int32_t>& ints);
    void clearPreviewSurface();
    void startEnrollCamera();
    void stopEnrollCamera();

  private:
    FaceHal() = default;
    bool openInternal();
    static void notifyTrampoline(const FaceMsg* msg);
    static void previewCallback(const uint8_t* data, int32_t frameIndex);
    static void firstFrameCallback();
    void onNotify(const FaceMsg* msg);
    void yuvFeederLoop();

    static void patchSensetimeLicense(void* handle, uintptr_t libBase);
    static void patchCameraId(void* handle, uintptr_t libBase);
    static void patchImageReader(void* handle, uintptr_t libBase);
    static void patchChallengeCheck(uintptr_t libBase);

    face_device_t* mDevice = nullptr;
    uintptr_t mLibBase = 0;

    std::mutex mCbMutex;
    std::shared_ptr<ISessionCallback> mCb;

    std::atomic<bool> mActive{false};
    std::atomic<bool> mCameraActive{false};
    std::atomic<int32_t> mEnrollingFaceId{1};

    std::mutex mIdsMutex;
    std::vector<int32_t> mExistingFaceIds;

    SurfaceBridge mSurfaceBridge;

    std::mutex mFeedMutex;
    std::condition_variable mFeedCv;
    std::vector<uint8_t> mFeedBuf;
    int32_t mFeedIndex = 0;
    bool mHasFrame = false;
    bool mFeederStarted = false;
};

}  // namespace aidl::android::hardware::biometrics::face::kj7
