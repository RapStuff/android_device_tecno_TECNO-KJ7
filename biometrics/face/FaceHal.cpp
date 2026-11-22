//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "FaceHalKJ7"

#include "FaceHal.h"

#include <aidl/android/hardware/biometrics/face/AcquiredInfo.h>

#include <android-base/file.h>
#include <android/dlext.h>
#include <dlfcn.h>
#include <log/log.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>

namespace aidl::android::hardware::biometrics::face::kj7 {

namespace {

constexpr int32_t kFaceMsgError = -1;
constexpr int32_t kFaceMsgAcquired = 1;
constexpr int32_t kFaceMsgTemplateRemoved = 2;
constexpr int32_t kFaceMsgTemplateEnrolling = 3;
constexpr int32_t kFaceMsgAuthenticated = 4;
constexpr int32_t kFaceMsgTemplateEnumerating = 5;

constexpr const char* kFaceDataDir = "/data/vendor_de/0/facedata";
constexpr const char* kTemplatePath = "/data/vendor_de/0/facedata/template_file.data";
constexpr const char* kOcularFeaturePath = "/data/vendor_de/0/facedata/feature_ocular.txt";
constexpr const char* kYuvPath = "/data/vendor_de/0/facedata/faceIdEnroll.yuv";
constexpr const char* kYuvTmpPath = "/data/vendor_de/0/facedata/faceIdEnroll.tmp";
constexpr int kFrameWidth = 640;
constexpr int kFrameHeight = 480;
constexpr size_t kNv21Bytes = kFrameWidth * kFrameHeight * 3 / 2;

FaceHal* gInstancePtr = nullptr;

using FnOpenCamera = int (*)(void* mgr, const char* cameraId, void* callback, void** device);
FnOpenCamera gRealOpenCamera = nullptr;

using FnGetWindow = int (*)(void* reader, void** window);
FnGetWindow gRealGetWindow = nullptr;

int hookedOpenCamera(void* mgr, const char* cameraId, void* callback, void** device) {
    ALOGI("ACameraManager_openCamera intercepted (requested='%s', forcing='1')",
          cameraId ? cameraId : "null");
    if (!gRealOpenCamera) {
        ALOGE("gRealOpenCamera is null");
        return -1;
    }
    return gRealOpenCamera(mgr, "1", callback, device);
}

int hookedGetWindowNativeHandle(void* reader, void** outWindow) {
    static std::atomic<bool> loggedOnce{false};
    if (!loggedOnce.exchange(true, std::memory_order_relaxed)) {
        ALOGI("AImageReader_getWindowNativeHandle intercepted -> forwarding to AImageReader_getWindow");
    }
    if (!gRealGetWindow) {
        ALOGE("gRealGetWindow is null");
        if (outWindow) *outWindow = nullptr;
        return -10000;
    }
    return gRealGetWindow(reader, outWindow);
}

int dummyStFaceInitLicense(const char* /*lic*/) {
    ALOGI("st_face_init_license intercepted -> returning 0");
    return 0;
}

void ensureDir(const char* path) {
    mkdir("/data/vendor_de", 0700);
    mkdir("/data/vendor_de/0", 0700);
    mkdir(path, 0700);
}

void* dlopenInNamespace(const char* path, const char* nsName) {
    using FnGetNs = struct android_namespace_t* (*)(const char*);
    static FnGetNs getNs = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        void* h = dlopen("libdl_android.so", RTLD_LAZY);
        getNs = reinterpret_cast<FnGetNs>(
                dlsym(h ? h : RTLD_DEFAULT, "android_get_exported_namespace"));
    }
    if (!getNs) return nullptr;
    auto* ns = getNs(nsName);
    if (!ns) return nullptr;
    android_dlextinfo info{};
    info.flags = ANDROID_DLEXT_USE_NAMESPACE;
    info.library_namespace = ns;
    return android_dlopen_ext(path, RTLD_NOW, &info);
}

RawFaceHat toRawHat(const keymaster::HardwareAuthToken& hat) {
    RawFaceHat raw{};
    raw.version = 0;
    raw.challenge = static_cast<uint64_t>(hat.challenge);
    raw.userId = static_cast<uint64_t>(hat.userId);
    raw.authenticatorId = static_cast<uint64_t>(hat.authenticatorId);
    raw.authenticatorType = static_cast<uint32_t>(hat.authenticatorType);
    raw.timestamp = static_cast<uint64_t>(hat.timestamp.milliSeconds);
    const size_t n = std::min(hat.mac.size(), sizeof(raw.hmac));
    if (n > 0) {
        std::memcpy(raw.hmac, hat.mac.data(), n);
    }
    return raw;
}

bool readVarint(const std::vector<uint8_t>& data, size_t& pos, uint64_t& out) {
    uint64_t res = 0;
    int shift = 0;
    while (pos < data.size()) {
        uint8_t b = data[pos++];
        res |= static_cast<uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            out = res;
            return true;
        }
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

bool readVarintSpan(const uint8_t* data, size_t len, size_t& pos, uint64_t& out) {
    uint64_t res = 0;
    int shift = 0;
    while (pos < len) {
        uint8_t b = data[pos++];
        res |= static_cast<uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            out = res;
            return true;
        }
        shift += 7;
        if (shift >= 64) return false;
    }
    return false;
}

int setSensetimeOrientation(int orientation) {
    using FnSetOrient = int (*)(int);
    static FnSetOrient fn = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        void* sym = dlsym(RTLD_DEFAULT, "st_auth_set_face_orientation");
        if (!sym) {
            if (void* h = dlopen("/vendor/lib64/libstfaceunlockppl.so", RTLD_NOW)) {
                sym = dlsym(h, "st_auth_set_face_orientation");
            }
        }
        fn = reinterpret_cast<FnSetOrient>(sym);
    }
    if (!fn) {
        ALOGW("st_auth_set_face_orientation symbol not found");
        return -1;
    }
    return fn(orientation);
}

}  // namespace

extern "C" int st_face_init_license(const char* lic) {
    return dummyStFaceInitLicense(lic);
}

std::vector<int32_t> parseEnrolledFaceIds() {
    std::string raw;
    if (!::android::base::ReadFileToString(kTemplatePath, &raw) || raw.empty()) {
        return {};
    }
    std::vector<uint8_t> data(raw.begin(), raw.end());
    std::vector<int32_t> ids;
    size_t pos = 0;
    while (pos < data.size()) {
        uint64_t tag = 0;
        if (!readVarint(data, pos, tag)) break;
        const uint8_t wire = tag & 7;
        const uint32_t field = static_cast<uint32_t>(tag >> 3);
        if (wire == 0) {
            uint64_t tmp = 0;
            if (!readVarint(data, pos, tmp)) break;
        } else if (wire == 1) {
            if (pos + 8 > data.size()) break;
            pos += 8;
        } else if (wire == 5) {
            if (pos + 4 > data.size()) break;
            pos += 4;
        } else if (wire == 2) {
            uint64_t len64 = 0;
            if (!readVarint(data, pos, len64)) break;
            size_t len = static_cast<size_t>(len64);
            if (pos + len > data.size()) break;
            const uint8_t* sub = data.data() + pos;
            pos += len;
            if (field == 5) {
                size_t spos = 0;
                while (spos < len) {
                    uint64_t stag = 0;
                    if (!readVarintSpan(sub, len, spos, stag)) break;
                    const uint8_t swire = stag & 7;
                    const uint32_t sfield = static_cast<uint32_t>(stag >> 3);
                    if (swire == 0) {
                        uint64_t sval = 0;
                        if (!readVarintSpan(sub, len, spos, sval)) break;
                        if (sfield == 1 && sval > 0) {
                            ids.push_back(static_cast<int32_t>(sval));
                        }
                    } else if (swire == 1) {
                        if (spos + 8 > len) break;
                        spos += 8;
                    } else if (swire == 5) {
                        if (spos + 4 > len) break;
                        spos += 4;
                    } else if (swire == 2) {
                        uint64_t slen = 0;
                        if (!readVarintSpan(sub, len, spos, slen)) break;
                        if (spos + static_cast<size_t>(slen) > len) break;
                        spos += static_cast<size_t>(slen);
                    } else {
                        break;
                    }
                }
            }
        } else {
            break;
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

std::shared_ptr<FaceHal> FaceHal::getInstance() {
    static std::mutex initMutex;
    static std::shared_ptr<FaceHal> instance;

    std::lock_guard<std::mutex> lock(initMutex);
    if (!instance) {
        auto hal = std::shared_ptr<FaceHal>(new FaceHal());
        if (!hal->openInternal()) {
            return nullptr;
        }
        instance = hal;
        gInstancePtr = instance.get();
    }
    return instance;
}

bool FaceHal::openInternal() {
    const char* candidates[] = {
            "/vendor/lib64/hw/face.default.so",
            "/vendor/lib64/hw/face.mt6789.so",
            "/odm/lib64/hw/face.default.so",
            "/system/lib64/hw/face.default.so",
    };

    void* handle = nullptr;
    const char* loadedPath = nullptr;
    for (const char* path : candidates) {
        handle = dlopen(path, RTLD_NOW);
        if (!handle) {
            handle = dlopenInNamespace(path, "sphal");
        }
        if (handle) {
            loadedPath = path;
            ALOGI("Loaded legacy face HAL: %s", path);
            break;
        }
    }
    if (!handle) {
        ALOGE("Failed to dlopen face HAL: %s", dlerror());
        return false;
    }

    auto* module = reinterpret_cast<const hw_module_t*>(dlsym(handle, "HMI"));
    if (!module || module->tag != HARDWARE_MODULE_TAG || !module->methods ||
        !module->methods->open) {
        ALOGE("Invalid HMI in %s", loadedPath);
        return false;
    }

    Dl_info dli{};
    if (dladdr(module, &dli) && dli.dli_fbase) {
        mLibBase = reinterpret_cast<uintptr_t>(dli.dli_fbase);
    } else {
        mLibBase = reinterpret_cast<uintptr_t>(module) - 0xfaa0;
    }
    ALOGI("Face HAL base: 0x%lx", static_cast<unsigned long>(mLibBase));

    patchSensetimeLicense(handle, mLibBase);
    patchCameraId(handle, mLibBase);
    patchImageReader(handle, mLibBase);
    patchChallengeCheck(mLibBase);

    hw_device_t* dev = nullptr;
    int ret = module->methods->open(module, "face", &dev);
    if (ret != 0 || !dev) {
        ALOGE("face module open failed: %d", ret);
        return false;
    }

    mDevice = reinterpret_cast<face_device_t*>(dev);
    gInstancePtr = this;

    if (mDevice->set_notify) {
        ret = mDevice->set_notify(mDevice, &FaceHal::notifyTrampoline);
        ALOGI("set_notify returned %d", ret);
    }

    ensureDir(kFaceDataDir);
    if (mDevice->set_active_group) {
        ret = mDevice->set_active_group(mDevice, 0, kFaceDataDir);
        ALOGI("Initial set_active_group(0, %s) returned %d", kFaceDataDir, ret);
    }

    return true;
}

void FaceHal::patchSensetimeLicense(void* handle, uintptr_t libBase) {
    const long pageSize = sysconf(_SC_PAGESIZE) > 0 ? sysconf(_SC_PAGESIZE) : 4096;
    auto origLic = reinterpret_cast<uintptr_t>(dlsym(handle, "st_face_init_license"));
    auto* gotEntry = reinterpret_cast<uintptr_t*>(libBase + 0xe770);
    uintptr_t pageAddr = reinterpret_cast<uintptr_t>(gotEntry) & ~(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(pageAddr), pageSize * 2, PROT_READ | PROT_WRITE) == 0) {
        *gotEntry = reinterpret_cast<uintptr_t>(&dummyStFaceInitLicense);
        if (origLic != 0) {
            auto* start = reinterpret_cast<uintptr_t*>(libBase + 0xe000);
            const size_t count = 0x1000 / sizeof(uintptr_t);
            for (size_t i = 0; i < count; ++i) {
                if (start[i] == origLic) {
                    start[i] = reinterpret_cast<uintptr_t>(&dummyStFaceInitLicense);
                }
            }
        }
    }

    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("libstfaceunlockppl.so") == std::string::npos) continue;
        size_t dash = line.find('-');
        if (dash == std::string::npos) break;
        uintptr_t pplBase = strtoul(line.substr(0, dash).c_str(), nullptr, 16);
        if (!pplBase) break;

        auto* flagPtr = reinterpret_cast<uint8_t*>(pplBase + 0x33914e8);
        uintptr_t flagPage = reinterpret_cast<uintptr_t>(flagPtr) & ~(pageSize - 1);
        if (mprotect(reinterpret_cast<void*>(flagPage), pageSize, PROT_READ | PROT_WRITE) == 0) {
            *flagPtr = 1;
        }

        auto* protFn = reinterpret_cast<uint32_t*>(pplBase + 0x30b334);
        uintptr_t protPage = reinterpret_cast<uintptr_t>(protFn) & ~(pageSize - 1);
        if (mprotect(reinterpret_cast<void*>(protPage), pageSize,
                     PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            protFn[0] = 0x52800000;  // mov w0, #0
            protFn[1] = 0xd65f03c0;  // ret
            __builtin___clear_cache(reinterpret_cast<char*>(protFn),
                                    reinterpret_cast<char*>(protFn + 2));
            mprotect(reinterpret_cast<void*>(protPage), pageSize, PROT_READ | PROT_EXEC);
        }
        break;
    }
}

void FaceHal::patchCameraId(void* handle, uintptr_t libBase) {
    const long pageSize = sysconf(_SC_PAGESIZE) > 0 ? sysconf(_SC_PAGESIZE) : 4096;
    const uintptr_t offsets[] = {0x4fb5, 0x5190};
    for (uintptr_t off : offsets) {
        auto* strAddr = reinterpret_cast<char*>(libBase + off);
        uintptr_t pageAddr = reinterpret_cast<uintptr_t>(strAddr) & ~(pageSize - 1);
        if (mprotect(reinterpret_cast<void*>(pageAddr), pageSize, PROT_READ | PROT_WRITE) == 0) {
            std::memset(strAddr, 0, 9);
            strAddr[0] = '1';
            mprotect(reinterpret_cast<void*>(pageAddr), pageSize, PROT_READ);
        }
    }

    gRealOpenCamera = reinterpret_cast<FnOpenCamera>(dlsym(handle, "ACameraManager_openCamera"));
    auto* gotEntry = reinterpret_cast<uintptr_t*>(libBase + 0xe9d8);
    uintptr_t gotPage = reinterpret_cast<uintptr_t>(gotEntry) & ~(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(gotPage), pageSize, PROT_READ | PROT_WRITE) == 0) {
        if (!gRealOpenCamera && *gotEntry != 0) {
            gRealOpenCamera = reinterpret_cast<FnOpenCamera>(*gotEntry);
        }
        *gotEntry = reinterpret_cast<uintptr_t>(&hookedOpenCamera);
    }
}

void FaceHal::patchImageReader(void* handle, uintptr_t libBase) {
    const long pageSize = sysconf(_SC_PAGESIZE) > 0 ? sysconf(_SC_PAGESIZE) : 4096;

    void* mediaHandle = dlopen("libmediandk.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!mediaHandle) {
        mediaHandle = dlopen("libmediandk.so", RTLD_LAZY);
    }
    if (!mediaHandle) {
        mediaHandle = dlopenInNamespace("libmediandk.so", "sphal");
    }

    auto resolveSym = [&](const char* name) -> void* {
        void* sym = dlsym(handle, name);
        if (!sym && mediaHandle) sym = dlsym(mediaHandle, name);
        if (!sym) sym = dlsym(RTLD_DEFAULT, name);
        return sym;
    };

    gRealGetWindow = reinterpret_cast<FnGetWindow>(resolveSym("AImageReader_getWindow"));
    auto origGetHandle = reinterpret_cast<uintptr_t>(resolveSym("AImageReader_getWindowNativeHandle"));
    if (!gRealGetWindow) {
        ALOGE("Failed to resolve AImageReader_getWindow; skipping ImageReader GOT hook");
        return;
    }

    // face.default.so (AOSP 12 vendor prebuilt) calls AImageReader_getWindowNativeHandle and passes
    // the result straight to AOSP 17 ACaptureSessionOutput_create(ANativeWindow*) /
    // ACameraOutputTarget_create(ANativeWindow*), which dereferences it as ANativeWindow* -> SIGBUS.
    auto* gotEntry = reinterpret_cast<uintptr_t*>(libBase + 0xe9d0);
    uintptr_t gotPage = reinterpret_cast<uintptr_t>(gotEntry) & ~(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(gotPage), pageSize * 2, PROT_READ | PROT_WRITE) == 0) {
        *gotEntry = reinterpret_cast<uintptr_t>(&hookedGetWindowNativeHandle);
        if (origGetHandle != 0) {
            auto* start = reinterpret_cast<uintptr_t*>(libBase + 0xe000);
            const size_t count = 0x1000 / sizeof(uintptr_t);
            for (size_t i = 0; i < count; ++i) {
                if (start[i] == origGetHandle) {
                    start[i] = reinterpret_cast<uintptr_t>(&hookedGetWindowNativeHandle);
                }
            }
        }
        ALOGI("Hooked AImageReader_getWindowNativeHandle -> AImageReader_getWindow (%p)",
              gRealGetWindow);
    } else {
        ALOGE("mprotect failed while patching AImageReader GOT entry");
    }
}

void FaceHal::patchChallengeCheck(uintptr_t libBase) {
    const long pageSize = sysconf(_SC_PAGESIZE) > 0 ? sysconf(_SC_PAGESIZE) : 4096;
    auto* insn = reinterpret_cast<uint32_t*>(libBase + 0x9ae8);
    uintptr_t pageAddr = reinterpret_cast<uintptr_t>(insn) & ~(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(pageAddr), pageSize,
                 PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        if (*insn == 0x54000621u) {
            *insn = 0xd503201fu;  // nop
            __builtin___clear_cache(reinterpret_cast<char*>(insn),
                                    reinterpret_cast<char*>(insn + 1));
        }
        mprotect(reinterpret_cast<void*>(pageAddr), pageSize, PROT_READ | PROT_EXEC);
    }
}

void FaceHal::setCallback(std::shared_ptr<ISessionCallback> cb) {
    std::lock_guard<std::mutex> lock(mCbMutex);
    mCb = std::move(cb);
}

int FaceHal::setActiveGroup(int32_t userId, const char* storePath) {
    const char* target = (storePath && storePath[0]) ? storePath : kFaceDataDir;
    ensureDir(target);
    if (!mDevice || !mDevice->set_active_group) return -1;
    // Transsion face HAL requires uid 0 to initialize SenseTime auth templates cleanly.
    int ret = mDevice->set_active_group(mDevice, 0, target);
    ALOGI("set_active_group(fwk=%d, hw=0, path=%s) -> %d", userId, target, ret);
    return ret;
}

uint64_t FaceHal::preEnroll() {
    if (!mDevice || !mDevice->pre_enroll) return 0;
    uint64_t challenge = 0;
    int ret = mDevice->pre_enroll(mDevice, 60, &challenge);
    ALOGI("pre_enroll -> ret=%d challenge=0x%llx", ret, static_cast<unsigned long long>(challenge));
    return challenge;
}

int FaceHal::postEnroll() {
    if (!mDevice || !mDevice->post_enroll) return 0;
    return mDevice->post_enroll(mDevice);
}

int FaceHal::enroll(const keymaster::HardwareAuthToken& hat, uint32_t timeoutSec) {
    mActive.store(true, std::memory_order_release);
    std::vector<int32_t> current = parseEnrolledFaceIds();
    int32_t nextId = 1;
    for (int32_t id = 1; id <= 5; ++id) {
        if (std::find(current.begin(), current.end(), id) == current.end()) {
            nextId = id;
            break;
        }
    }
    mEnrollingFaceId.store(nextId, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(mIdsMutex);
        mExistingFaceIds = std::move(current);
    }

    setSensetimeOrientation(8);
    if (!mDevice || !mDevice->enroll) return -1;
    RawFaceHat raw = toRawHat(hat);
    int ret = mDevice->enroll(mDevice, &raw, timeoutSec, nullptr, 0);
    setSensetimeOrientation(8);
    return ret;
}

int FaceHal::authenticate(uint64_t operationId) {
    {
        std::lock_guard<std::mutex> lock(mOpMutex);
        if (mLastCancelTime.time_since_epoch().count() != 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - mLastCancelTime);
            if (elapsed.count() < 150) {
                std::this_thread::sleep_for(std::chrono::milliseconds(150) - elapsed);
            }
        }
    }
    mActive.store(true, std::memory_order_release);
    setSensetimeOrientation(8);
    // face_pre_enroll clears face.default.so's cached mFaceId; re-enumerate to repopulate it.
    if (mDevice && mDevice->enumerate) {
        mDevice->enumerate(mDevice);
    }
    if (!mDevice || !mDevice->authenticate) return -1;
    int ret = mDevice->authenticate(mDevice, operationId);
    if (ret != 0) {
        ALOGW("authenticate(op=0x%llx) failed (%d), retrying after camera release",
              static_cast<unsigned long long>(operationId), ret);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        setSensetimeOrientation(8);
        ret = mDevice->authenticate(mDevice, operationId);
    }
    return ret;
}

int FaceHal::cancel() {
    mActive.store(false, std::memory_order_release);
    stopEnrollCamera();
    if (!mDevice || !mDevice->cancel) return 0;
    int ret = mDevice->cancel(mDevice);
    {
        std::lock_guard<std::mutex> lock(mOpMutex);
        mLastCancelTime = std::chrono::steady_clock::now();
    }
    return ret;
}

int FaceHal::enumerate() {
    if (!mDevice || !mDevice->enumerate) return 0;
    return mDevice->enumerate(mDevice);
}

int FaceHal::remove(uint32_t faceId) {
    if (!mDevice || !mDevice->remove) return 0;
    return mDevice->remove(mDevice, faceId);
}

uint64_t FaceHal::getAuthenticatorId() {
    if (!mDevice || !mDevice->get_authenticator_id) return 0;
    uint64_t authId = 0;
    mDevice->get_authenticator_id(mDevice, &authId);
    return authId;
}

int FaceHal::resetLockout(const keymaster::HardwareAuthToken& hat) {
    if (!mDevice || !mDevice->reset_lockout) return 0;
    RawFaceHat raw = toRawHat(hat);
    return mDevice->reset_lockout(mDevice, &raw);
}

std::vector<Feature> FaceHal::getFeatures() {
    std::vector<Feature> enabledFeatures;
    bool ocularEnabled = false;
    const auto enrolled = parseEnrolledFaceIds();
    const int32_t faceId = enrolled.empty() ? 0 : enrolled.front();

    if (mDevice && mDevice->get_feature && faceId > 0) {
        int ret = mDevice->get_feature(
                mDevice, static_cast<int32_t>(Feature::REQUIRE_ATTENTION), faceId, &ocularEnabled);
        ALOGI("get_feature(REQUIRE_ATTENTION, fid=%d) -> ret=%d enabled=%d",
              faceId, ret, ocularEnabled ? 1 : 0);
        if (ret == 0) {
            if (ocularEnabled) {
                enabledFeatures.push_back(Feature::REQUIRE_ATTENTION);
            }
            return enabledFeatures;
        }
    }

    // Fallback: face.default.so defaults to eye-open check enabled when feature_ocular.txt is absent.
    std::ifstream ifs(kOcularFeaturePath, std::ios::binary);
    if (ifs.is_open()) {
        char ch = '0';
        ifs.get(ch);
        ocularEnabled = (ch == '1');
    } else {
        ocularEnabled = true;
    }
    if (ocularEnabled) {
        enabledFeatures.push_back(Feature::REQUIRE_ATTENTION);
    }
    return enabledFeatures;
}

bool FaceHal::setFeature(Feature feature, bool enabled,
                         const keymaster::HardwareAuthToken& hat) {
    if (feature != Feature::REQUIRE_ATTENTION) {
        return false;
    }
    const auto enrolled = parseEnrolledFaceIds();
    const int32_t faceId = enrolled.empty() ? 0 : enrolled.front();
    RawFaceHat raw = toRawHat(hat);
    int ret = -1;
    if (mDevice && mDevice->set_feature) {
        ret = mDevice->set_feature(mDevice, static_cast<int32_t>(feature), enabled, &raw, faceId);
        ALOGI("set_feature(REQUIRE_ATTENTION, enabled=%d, fid=%d) -> %d",
              enabled ? 1 : 0, faceId, ret);
    }
    if (ret != 0 && (hat.challenge != 0 || hat.userId != 0)) {
        ensureDir(kFaceDataDir);
        std::ofstream ofs(kOcularFeaturePath, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs << (enabled ? '1' : '0');
            ofs.close();
            ret = ofs.good() ? 0 : -1;
            ALOGW("set_feature synced %s='%c' (ret=%d)",
                  kOcularFeaturePath, enabled ? '1' : '0', ret);
        }
    }
    return ret == 0;
}

void FaceHal::attachPreviewWindow(ANativeWindow* window) {
    mSurfaceBridge.attachNativeWindow(window);
}

void FaceHal::attachPreviewToken(const std::vector<int32_t>& ints) {
    if (ints.size() < 2) return;
    size_t declaredLen = static_cast<size_t>(std::max(0, ints[0]));
    size_t availBytes = (ints.size() - 1) * sizeof(int32_t);
    size_t tokenLen = std::min(declaredLen, availBytes);
    if (tokenLen > 0) {
        mSurfaceBridge.attachHidlToken(reinterpret_cast<const uint8_t*>(&ints[1]), tokenLen);
    }
}

void FaceHal::clearPreviewSurface() {
    mSurfaceBridge.clear();
}

void FaceHal::yuvFeederLoop() {
    ensureDir(kFaceDataDir);
    auto lastWrite = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<uint8_t> localBuf(kNv21Bytes);

    while (true) {
        {
            std::unique_lock<std::mutex> lock(mFeedMutex);
            mFeedCv.wait(lock, [this] { return mHasFrame; });
            mHasFrame = false;
            localBuf.swap(mFeedBuf);
            if (mFeedBuf.size() != kNv21Bytes) mFeedBuf.resize(kNv21Bytes);
        }

        if (!mCameraActive.load(std::memory_order_acquire)) {
            continue;
        }

        struct stat st{};
        const bool needInitial = (stat(kYuvPath, &st) != 0 || st.st_size < 100);
        const auto now = std::chrono::steady_clock::now();
        if (!needInitial &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastWrite).count() < 150) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            continue;
        }

        std::ofstream ofs(kYuvTmpPath, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(localBuf.data()), localBuf.size());
            ofs.close();
            if (ofs.good() && rename(kYuvTmpPath, kYuvPath) == 0) {
                lastWrite = now;
            }
        }
    }
}

void FaceHal::previewCallback(const uint8_t* data, int32_t frameIndex) {
    if (!data || !gInstancePtr) return;
    if (!gInstancePtr->mCameraActive.load(std::memory_order_acquire)) return;

    gInstancePtr->mSurfaceBridge.renderNv21(data, kFrameWidth, kFrameHeight);

    std::lock_guard<std::mutex> lock(gInstancePtr->mFeedMutex);
    if (!gInstancePtr->mFeederStarted) {
        gInstancePtr->mFeederStarted = true;
        gInstancePtr->mFeedBuf.resize(kNv21Bytes);
        std::thread([hal = gInstancePtr] { hal->yuvFeederLoop(); }).detach();
    }
    std::memcpy(gInstancePtr->mFeedBuf.data(), data, kNv21Bytes);
    gInstancePtr->mFeedIndex = frameIndex;
    gInstancePtr->mHasFrame = true;
    gInstancePtr->mFeedCv.notify_one();
}

void FaceHal::firstFrameCallback() {
    ALOGI("Front camera preview produced first frame");
}

void FaceHal::startEnrollCamera() {
    if (mCameraActive.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    setSensetimeOrientation(8);
    if (!mLibBase) {
        mCameraActive.store(false, std::memory_order_release);
        return;
    }

    unlink(kYuvTmpPath);
    unlink(kYuvPath);

    using FnStartCapture = int (*)(void* ctx, int32_t timeoutMs,
                                   void (*previewCb)(const uint8_t*, int32_t),
                                   void (*firstFrameCb)());
    void* cameraCtx = reinterpret_cast<void*>(mLibBase + 0x10f38);
    auto startFn = reinterpret_cast<FnStartCapture>(mLibBase + 0xb6cc);
    int ret = startFn(cameraCtx, 1000, &FaceHal::previewCallback, &FaceHal::firstFrameCallback);
    ALOGI("startCameraCapture returned %d", ret);
    if (ret != 0) {
        mCameraActive.store(false, std::memory_order_release);
    }
}

void FaceHal::stopEnrollCamera() {
    if (!mCameraActive.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    mSurfaceBridge.clear();
    if (mLibBase) {
        using FnCloseCamera = int (*)(void* ctx, int32_t arg);
        void* cameraCtx = reinterpret_cast<void*>(mLibBase + 0x10f38);
        auto closeFn = reinterpret_cast<FnCloseCamera>(mLibBase + 0x87b4);
        int ret = closeFn(cameraCtx, 0);
        ALOGI("closeCamera returned %d", ret);
    }
    unlink(kYuvTmpPath);
    unlink(kYuvPath);
    std::lock_guard<std::mutex> lock(mFeedMutex);
    mHasFrame = false;
}

void FaceHal::notifyTrampoline(const FaceMsg* msg) {
    if (msg && gInstancePtr) {
        gInstancePtr->onNotify(msg);
    }
}

void FaceHal::onNotify(const FaceMsg* msg) {
    std::shared_ptr<ISessionCallback> cb;
    {
        std::lock_guard<std::mutex> lock(mCbMutex);
        cb = mCb;
    }
    if (!cb) return;

    switch (msg->type) {
        case kFaceMsgAuthenticated: {
            mActive.store(false, std::memory_order_release);
            // face.default.so only emits kFaceMsgAuthenticated on st_auth_unlock == 201,
            // yet face_pre_enroll clears mFaceId and face_authenticate never refreshes it.
            int32_t fid = msg->faceIdOrError;
            auto enrolled = parseEnrolledFaceIds();
            if (fid <= 0 || (!enrolled.empty() &&
                std::find(enrolled.begin(), enrolled.end(), fid) == enrolled.end())) {
                fid = enrolled.empty() ? 1 : enrolled.front();
            }
            RawFaceHat raw{};
            std::memcpy(&raw, msg->extra, sizeof(raw));
            keymaster::HardwareAuthToken hat{};
            hat.challenge = static_cast<int64_t>(raw.challenge);
            hat.userId = static_cast<int64_t>(raw.userId);
            hat.authenticatorId = static_cast<int64_t>(raw.authenticatorId);
            hat.authenticatorType =
                    static_cast<keymaster::HardwareAuthenticatorType>(raw.authenticatorType);
            hat.timestamp.milliSeconds = static_cast<int64_t>(raw.timestamp);
            hat.mac.assign(raw.hmac, raw.hmac + sizeof(raw.hmac));
            cb->onAuthenticationSucceeded(fid, hat);
            break;
        }
        case kFaceMsgTemplateEnrolling: {
            int32_t remaining = 0;
            std::memcpy(&remaining, msg->extra, sizeof(remaining));
            int32_t fid = mEnrollingFaceId.load(std::memory_order_acquire);
            if (remaining == 0) {
                mActive.store(false, std::memory_order_release);
                stopEnrollCamera();
                auto updated = parseEnrolledFaceIds();
                std::lock_guard<std::mutex> lock(mIdsMutex);
                for (int32_t id : updated) {
                    if (std::find(mExistingFaceIds.begin(), mExistingFaceIds.end(), id) ==
                        mExistingFaceIds.end()) {
                        fid = id;
                        mEnrollingFaceId.store(id, std::memory_order_release);
                        break;
                    }
                }
            }
            cb->onEnrollmentProgress(fid, remaining);
            break;
        }
        case kFaceMsgError: {
            mActive.store(false, std::memory_order_release);
            stopEnrollCamera();
            const int32_t err = msg->faceIdOrError;
            if (err > 8) {
                cb->onError(Error::VENDOR, err);
            } else {
                cb->onError(static_cast<Error>(err), 0);
            }
            break;
        }
        case kFaceMsgAcquired: {
            const int32_t code = msg->faceIdOrError;
            BaseFrame frame{};
            if (code == 0 || code == 20) {
                frame.acquiredInfo = AcquiredInfo::START;
            } else if (code >= 1 && code <= 24 && code != 22) {
                frame.acquiredInfo = static_cast<AcquiredInfo>(code);
            } else if (code >= 1000) {
                frame.acquiredInfo = AcquiredInfo::VENDOR;
                frame.vendorCode = code - 1000;
            } else if (code > 0) {
                frame.acquiredInfo = AcquiredInfo::VENDOR;
                frame.vendorCode = code;
            } else {
                break;
            }
            if (mCameraActive.load(std::memory_order_acquire)) {
                EnrollmentFrame ef{};
                ef.data = frame;
                cb->onEnrollmentFrame(ef);
            } else {
                AuthenticationFrame af{};
                af.data = frame;
                cb->onAuthenticationFrame(af);
            }
            break;
        }
        case kFaceMsgTemplateRemoved:
        case kFaceMsgTemplateEnumerating:
        default:
            break;
    }
}

}  // namespace aidl::android::hardware::biometrics::face::kj7
