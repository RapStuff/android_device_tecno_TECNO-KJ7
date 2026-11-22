//
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <android/native_window.h>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace aidl::android::hardware::biometrics::face::kj7 {

/**
 * Renders NV21 front-camera enrollment preview frames into either a modern
 * Surface ANativeWindow or a legacy HIDL bufferqueue token NativeHandle.
 */
class SurfaceBridge {
  public:
    SurfaceBridge() = default;
    ~SurfaceBridge();

    bool attachNativeWindow(ANativeWindow* window);
    bool attachHidlToken(const uint8_t* tokenData, size_t tokenLen);
    void clear();
    void renderNv21(const uint8_t* nv21, int width, int height);

  private:
    void resetLocked();

    std::mutex mMutex;
    ANativeWindow* mWindow = nullptr;
    bool mOwnsWindowRef = false;
    int mFrameCount = 0;
};

}  // namespace aidl::android::hardware::biometrics::face::kj7
