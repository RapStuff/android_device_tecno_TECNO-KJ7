//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "FingerprintServiceKJ7"

#include "Fingerprint.h"

#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <dlfcn.h>
#include <log/log.h>
#include <unistd.h>

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

namespace {

constexpr int32_t kMaxEnrollmentsPerUser = 5;
constexpr char kHwComponentId[] = "fingerprintSensor";
constexpr char kHwVersion[] = "vendor/model/revision";
constexpr char kFwVersion[] = "1.01";
constexpr char kSerialNumber[] = "00000001";
constexpr char kSwComponentId[] = "matchingAlgorithm";
constexpr char kSwVersion[] = "vendor/version/revision";
constexpr uint16_t kVersion = HARDWARE_MODULE_API_VERSION(2, 1);

Fingerprint* sInstance = nullptr;

template <typename Fn>
Fn resolveSym(void* primary, void* secondary, const char* name) {
    void* sym = primary ? dlsym(primary, name) : nullptr;
    if (!sym && secondary) sym = dlsym(secondary, name);
    if (!sym) sym = dlsym(RTLD_DEFAULT, name);
    return reinterpret_cast<Fn>(sym);
}

}  // namespace

Fingerprint::Fingerprint() : mDevice(openHal()) {
    sInstance = this;
}

Fingerprint::~Fingerprint() {
    if (mDevice && mDevice->common.close) {
        mDevice->common.close(reinterpret_cast<hw_device_t*>(mDevice));
    }
    mDevice = nullptr;
}

fingerprint_device_t* Fingerprint::openHal() {
    std::string vendorName;
    if (::android::base::ReadFileToString("/sys/kernel/tran_fp/vendor_name", &vendorName)) {
        vendorName = ::android::base::Trim(vendorName);
        ALOGI("Detected fingerprint sensor vendor: %s", vendorName.c_str());
    }

    const bool isSilead =
            vendorName.find("silead") != std::string::npos || access("/dev/silead_fp", F_OK) == 0;
    const hw_module_t* hwModule = nullptr;
    const char* libPath = nullptr;
    if (isSilead) {
        libPath = "/vendor/lib64/hw/fingerprint.silead.default.so";
    } else if (vendorName == "goodix" || access("/dev/goodix_fp", F_OK) == 0) {
        libPath = "/vendor/lib64/hw/gf_fingerprint.default.so";
    }

    void* implHandle = nullptr;
    if (isSilead) {
        implHandle = dlopen("/vendor/lib64/libsl_fp_impl.so", RTLD_NOW | RTLD_GLOBAL);
        if (!implHandle) {
            ALOGW("dlopen(/vendor/lib64/libsl_fp_impl.so) failed: %s", dlerror());
        }
    }

    void* halHandle = nullptr;
    if (libPath) {
        halHandle = dlopen(libPath, RTLD_NOW | RTLD_GLOBAL);
        if (halHandle) {
            hwModule = reinterpret_cast<const hw_module_t*>(dlsym(halHandle, "HMI"));
            ALOGI("Loaded %s (HMI=%p)", libPath, hwModule);
        } else {
            ALOGW("dlopen(%s) failed: %s", libPath, dlerror());
        }
    }

    if (isSilead) {
        ::android::base::WriteStringToFile("1", "/proc/fingerprint_status");
        ::android::base::WriteStringToFile("fp1", "/proc/gesture_function");

        mSileadOps.custCmd =
                resolveSym<int (*)(uint32_t, uint32_t)>(implHandle, halHandle, "silfp_finger_cust_cmd");
        mSileadOps.setFingerStatusMode = resolveSym<int (*)(int32_t)>(
                implHandle, halHandle, "silfp_dev_set_finger_status_mode");
        mSileadOps.custInit =
                resolveSym<int (*)(void)>(implHandle, halHandle, "silfp_finger_cust_init");
        mSileadOps.screenStateCallback = resolveSym<int (*)(int32_t)>(
                implHandle, halHandle, "silfp_worker_screen_state_callback");
        if (!mSileadOps.screenStateCallback) {
            mSileadOps.screenStateCallback = resolveSym<int (*)(int32_t)>(
                    implHandle, halHandle, "silfp_worker_set_screen_state_default");
        }
        mSileadOps.syncFingerStatusOptic = resolveSym<int (*)(int32_t)>(
                implHandle, halHandle, "silfp_impl_sync_finger_status_optic");
        mSileadOps.devSyncFingerDown =
                resolveSym<int (*)(void)>(implHandle, halHandle, "silfp_dev_sync_finger_down");
        mSileadOps.devSyncFingerUp =
                resolveSym<int (*)(void)>(implHandle, halHandle, "silfp_dev_sync_finger_up");
        mSileadOps.msgSend =
                resolveSym<int (*)(int32_t)>(implHandle, halHandle, "silfp_msg_send");

        ALOGI("Silead symbols: custCmd=%p setMode=%p custInit=%p screenCb=%p syncOptic=%p "
              "syncDown=%p syncUp=%p msgSend=%p",
              mSileadOps.custCmd, mSileadOps.setFingerStatusMode, mSileadOps.custInit,
              mSileadOps.screenStateCallback, mSileadOps.syncFingerStatusOptic,
              mSileadOps.devSyncFingerDown, mSileadOps.devSyncFingerUp, mSileadOps.msgSend);
    } else if (halHandle) {
        mSileadOps.fingerprintCmd = resolveSym<int (*)(fingerprint_device_t*, uint32_t, uint32_t)>(
                halHandle, nullptr, "_Z19fingerprint_commandP18fingerprint_devicejj");
    }

    if (!hwModule) {
        int err = hw_get_module(FINGERPRINT_HARDWARE_MODULE_ID, &hwModule);
        if (err != 0 || !hwModule) {
            ALOGE("hw_get_module(fingerprint) failed: %d", err);
            return nullptr;
        }
    }

    if (!hwModule->methods || !hwModule->methods->open) {
        ALOGE("Fingerprint module has no open method");
        return nullptr;
    }

    hw_device_t* device = nullptr;
    int err = hwModule->methods->open(hwModule, FINGERPRINT_HARDWARE_MODULE_ID, &device);
    if (err != 0 || !device) {
        ALOGE("Fingerprint open() failed: %d", err);
        return nullptr;
    }

    if (device->version != kVersion) {
        ALOGW("Unexpected fingerprint HAL version 0x%x (expected 0x%x)", device->version, kVersion);
    }

    auto* fpDevice = reinterpret_cast<fingerprint_device_t*>(device);

    if (mSileadOps.setFingerStatusMode) {
        int ret = mSileadOps.setFingerStatusMode(1);
        ALOGI("silfp_dev_set_finger_status_mode(1) -> %d", ret);
    }
    if (mSileadOps.custInit) {
        int ret = mSileadOps.custInit();
        ALOGI("silfp_finger_cust_init() -> %d", ret);
    }
    if (mSileadOps.screenStateCallback) {
        int ret = mSileadOps.screenStateCallback(1);
        ALOGI("silfp_worker_screen_state_callback(1) -> %d", ret);
    }

    if (!fpDevice->set_notify || fpDevice->set_notify(fpDevice, &Fingerprint::notify) != 0) {
        ALOGE("Failed to register fingerprint notify callback");
        return nullptr;
    }

    return fpDevice;
}

std::vector<SensorLocation> Fingerprint::getSensorLocations() {
    std::vector<SensorLocation> locations;
    std::string loc = ::android::base::GetProperty("persist.vendor.fingerprint.sensor_location",
                                                   "540|2204|91");
    for (const auto& entry : ::android::base::Split(loc, ",")) {
        auto dim = ::android::base::Split(entry, "|");
        if (dim.size() != 3 && dim.size() != 4) continue;
        int32_t x = 0;
        int32_t y = 0;
        int32_t r = 0;
        if (!::android::base::ParseInt(dim[0], &x) || !::android::base::ParseInt(dim[1], &y) ||
            !::android::base::ParseInt(dim[2], &r)) {
            continue;
        }
        SensorLocation s{};
        s.sensorLocationX = x;
        s.sensorLocationY = y;
        s.sensorRadius = r;
        if (dim.size() == 4 && !dim[3].empty()) {
            s.display = dim[3];
        }
        locations.push_back(std::move(s));
    }
    return locations;
}

void Fingerprint::notify(const fingerprint_msg_t* msg) {
    if (!sInstance || !sInstance->mSession || sInstance->mSession->isClosed()) {
        ALOGW("Received fingerprint callback with no active session");
        return;
    }
    sInstance->mSession->notify(msg);
}

ndk::ScopedAStatus Fingerprint::getSensorProps(std::vector<SensorProps>* out) {
    std::vector<common::ComponentInfo> componentInfo = {
            {kHwComponentId, kHwVersion, kFwVersion, kSerialNumber, ""},
            {kSwComponentId, "", "", "", kSwVersion},
    };

    const int32_t sensorId = ::android::base::GetIntProperty("persist.vendor.fingerprint.sensor_id", 0);
    const int32_t sensorStrength =
            ::android::base::GetIntProperty("persist.vendor.fingerprint.sensor_strength", 2);
    const bool navGesture =
            ::android::base::GetBoolProperty("persist.vendor.fingerprint.navigation_gesture", false);
    const bool detectInteraction =
            ::android::base::GetBoolProperty("persist.vendor.fingerprint.detect_interaction", false);
    const std::string sensorTypeStr =
            ::android::base::GetProperty("persist.vendor.fingerprint.type", "udfps_optical");

    FingerprintSensorType sensorType = FingerprintSensorType::UNDER_DISPLAY_OPTICAL;
    if (sensorTypeStr == "udfps" || sensorTypeStr == "udfps_optical") {
        sensorType = FingerprintSensorType::UNDER_DISPLAY_OPTICAL;
    } else if (sensorTypeStr == "udfps_ultrasonic") {
        sensorType = FingerprintSensorType::UNDER_DISPLAY_ULTRASONIC;
    } else if (sensorTypeStr == "side") {
        sensorType = FingerprintSensorType::POWER_BUTTON;
    } else if (sensorTypeStr == "home") {
        sensorType = FingerprintSensorType::HOME_BUTTON;
    } else if (sensorTypeStr == "rear") {
        sensorType = FingerprintSensorType::REAR;
    }

    common::CommonProps commonProps{sensorId, static_cast<common::SensorStrength>(sensorStrength),
                                    kMaxEnrollmentsPerUser, componentInfo};

    SensorProps props{};
    props.commonProps = std::move(commonProps);
    props.sensorType = sensorType;
    props.sensorLocations = getSensorLocations();
    props.supportsNavigationGestures = navGesture;
    props.supportsDetectInteraction = detectInteraction;
    props.halHandlesDisplayTouches = false;
    props.halControlsIllumination = false;

    *out = {std::move(props)};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Fingerprint::createSession(int32_t /*sensorId*/, int32_t userId,
                                              const std::shared_ptr<ISessionCallback>& cb,
                                              std::shared_ptr<ISession>* out) {
    if (!mDevice) {
        ALOGE("Fingerprint HAL device is not initialized");
        return ndk::ScopedAStatus::fromServiceSpecificError(-1);
    }

    mSession = ndk::SharedRefBase::make<Session>(mDevice, mSileadOps, userId, cb, mLockoutTracker);
    mSession->linkToDeath(cb->asBinder().get());
    *out = mSession;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
