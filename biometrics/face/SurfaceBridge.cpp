//
// SPDX-License-Identifier: Apache-2.0
//

#define LOG_TAG "FaceSurfaceBridgeKJ7"

#include "SurfaceBridge.h"

#include <android/hardware/graphics/bufferqueue/2.0/IGraphicBufferProducer.h>
#include <bufferqueueconverter/BufferQueueConverter.h>
#include <hidl/HidlSupport.h>
#include <hidl/HybridInterface.h>
#include <log/log.h>
#include <utils/StrongPointer.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace aidl::android::hardware::biometrics::face::kj7 {

namespace {

constexpr int kTargetWidth = 480;
constexpr int kTargetHeight = 640;

inline uint8_t clampU8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<uint8_t>(v);
}

}  // namespace

SurfaceBridge::~SurfaceBridge() {
    clear();
}

void SurfaceBridge::resetLocked() {
    if (mWindow && mOwnsWindowRef) {
        ANativeWindow_release(mWindow);
    }
    mWindow = nullptr;
    mOwnsWindowRef = false;
    mFrameCount = 0;
}

bool SurfaceBridge::attachNativeWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(mMutex);
    resetLocked();
    if (!window) {
        return false;
    }
    ANativeWindow_acquire(window);
    mWindow = window;
    mOwnsWindowRef = true;

    int rc = ANativeWindow_setBuffersGeometry(mWindow, kTargetWidth, kTargetHeight,
                                              AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    if (rc != 0) {
        ALOGW("ANativeWindow_setBuffersGeometry RGBA_8888 failed (%d), falling back to default", rc);
        ANativeWindow_setBuffersGeometry(mWindow, 0, 0, 0);
    }
    ALOGI("Attached direct ANativeWindow preview surface %p", mWindow);
    return true;
}

bool SurfaceBridge::attachHidlToken(const uint8_t* tokenData, size_t tokenLen) {
    std::lock_guard<std::mutex> lock(mMutex);
    resetLocked();
    if (!tokenData || tokenLen == 0) {
        return false;
    }

    ::android::hardware::hidl_vec<uint8_t> vec;
    vec.setToExternal(const_cast<uint8_t*>(tokenData), tokenLen);
    ::android::sp<::android::hidl::base::V1_0::IBase> base =
            ::android::retrieveHalInterface(vec);
    if (!base) {
        ALOGW("retrieveHalInterface returned null for token (%zu bytes)", tokenLen);
        return false;
    }

    ::android::sp<::HGraphicBufferProducer> hgbp = ::HGraphicBufferProducer::castFrom(base);
    if (!hgbp) {
        ALOGW("HIDL token is not IGraphicBufferProducer V2_0");
        return false;
    }

    ::android::sp<ANativeWindow> window = ::android::getNativeWindowFromHGBP(hgbp);
    if (!window) {
        ALOGW("getNativeWindowFromHGBP returned null");
        return false;
    }

    ANativeWindow_acquire(window.get());
    mWindow = window.get();
    mOwnsWindowRef = true;
    int rc = ANativeWindow_setBuffersGeometry(mWindow, kTargetWidth, kTargetHeight,
                                              AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
    if (rc != 0) {
        ANativeWindow_setBuffersGeometry(mWindow, 0, 0, 0);
    }
    ALOGI("Attached HIDL V2_0 preview surface %p", mWindow);
    return true;
}

void SurfaceBridge::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mFrameCount > 0) {
        ALOGI("Releasing preview surface after %d frames", mFrameCount);
    }
    resetLocked();
}

void SurfaceBridge::renderNv21(const uint8_t* nv21, int width, int height) {
    if (!nv21 || width <= 0 || height <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    if (!mWindow) {
        return;
    }

    ANativeWindow_Buffer buf{};
    if (ANativeWindow_lock(mWindow, &buf, nullptr) != 0 || !buf.bits) {
        return;
    }

    const int bw = buf.width;
    const int bh = buf.height;
    const int bstride = buf.stride;
    if (bw <= 0 || bh <= 0 || bstride <= 0) {
        ANativeWindow_unlockAndPost(mWindow);
        return;
    }

    const uint8_t* yPlane = nv21;
    const uint8_t* vuPlane = nv21 + (width * height);

    std::vector<int> colLut(bh);
    std::vector<int> rowLut(bw);
    std::vector<int> rowHalfWLut(bw);

    const int hDenom = bh > 1 ? (bh - 1) : 1;
    for (int dy = 0; dy < bh; ++dy) {
        int col = ((width - 1) * (bh - 1 - dy)) / hDenom;
        colLut[dy] = std::clamp(col, 0, width - 1);
    }
    const int wDenom = bw > 1 ? (bw - 1) : 1;
    for (int dx = 0; dx < bw; ++dx) {
        int row = ((height - 1) * dx) / wDenom;
        rowLut[dx] = std::clamp(row, 0, height - 1);
        rowHalfWLut[dx] = (rowLut[dx] >> 1) * width;
    }

    if (buf.format == AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM) {
        auto* dst = static_cast<uint16_t*>(buf.bits);
        for (int dy = 0; dy < bh; ++dy) {
            const int sc = colLut[dy];
            const int uvCol = (sc >> 1) << 1;
            const uint8_t* yCol = yPlane + sc;
            uint16_t* outRow = dst + dy * bstride;
            for (int dx = 0; dx < bw; ++dx) {
                const int yVal = yCol[rowLut[dx] * width];
                const int uvIdx = rowHalfWLut[dx] + uvCol;
                const int vVal = vuPlane[uvIdx];
                const int uVal = vuPlane[uvIdx + 1];
                const int c = std::max(0, yVal - 16);
                const int d = uVal - 128;
                const int e = vVal - 128;
                const uint8_t r = clampU8((298 * c + 409 * e + 128) >> 8);
                const uint8_t g = clampU8((298 * c - 100 * d - 208 * e + 128) >> 8);
                const uint8_t b = clampU8((298 * c + 516 * d + 128) >> 8);
                outRow[dx] = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    } else {
        auto* dst = static_cast<uint32_t*>(buf.bits);
        for (int dy = 0; dy < bh; ++dy) {
            const int sc = colLut[dy];
            const int uvCol = (sc >> 1) << 1;
            const uint8_t* yCol = yPlane + sc;
            uint32_t* outRow = dst + dy * bstride;
            for (int dx = 0; dx < bw; ++dx) {
                const int yVal = yCol[rowLut[dx] * width];
                const int uvIdx = rowHalfWLut[dx] + uvCol;
                const int vVal = vuPlane[uvIdx];
                const int uVal = vuPlane[uvIdx + 1];
                const int c = std::max(0, yVal - 16);
                const int d = uVal - 128;
                const int e = vVal - 128;
                const uint8_t r = clampU8((298 * c + 409 * e + 128) >> 8);
                const uint8_t g = clampU8((298 * c - 100 * d - 208 * e + 128) >> 8);
                const uint8_t b = clampU8((298 * c + 516 * d + 128) >> 8);
                outRow[dx] = 0xFF000000u | (static_cast<uint32_t>(b) << 16) |
                             (static_cast<uint32_t>(g) << 8) | r;
            }
        }
    }

    ANativeWindow_unlockAndPost(mWindow);
    ++mFrameCount;
}

}  // namespace aidl::android::hardware::biometrics::face::kj7
