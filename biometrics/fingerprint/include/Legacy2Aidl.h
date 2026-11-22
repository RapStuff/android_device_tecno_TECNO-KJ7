/*
 * Copyright (C) 2024 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/android/hardware/keymaster/HardwareAuthToken.h>
#include <endian.h>
#include <hardware/hw_auth_token.h>
#include <algorithm>
#include <iterator>

namespace aidl::android::hardware::biometrics::fingerprint::kj7 {

inline void translate(const keymaster::HardwareAuthToken& authToken, hw_auth_token_t& hat) {
    hat.version = HW_AUTH_TOKEN_VERSION;
    hat.challenge = static_cast<uint64_t>(authToken.challenge);
    hat.user_id = static_cast<uint64_t>(authToken.userId);
    hat.authenticator_id = static_cast<uint64_t>(authToken.authenticatorId);
    hat.authenticator_type = htobe32(static_cast<uint32_t>(authToken.authenticatorType));
    hat.timestamp = htobe64(static_cast<uint64_t>(authToken.timestamp.milliSeconds));
    const size_t n = std::min(authToken.mac.size(), sizeof(hat.hmac));
    if (n > 0) {
        std::copy_n(authToken.mac.begin(), n, hat.hmac);
    }
}

inline void translate(const hw_auth_token_t& hat, keymaster::HardwareAuthToken& authToken) {
    authToken.challenge = static_cast<int64_t>(hat.challenge);
    authToken.userId = static_cast<int64_t>(hat.user_id);
    authToken.authenticatorId = static_cast<int64_t>(hat.authenticator_id);
    authToken.authenticatorType = static_cast<keymaster::HardwareAuthenticatorType>(
            be32toh(hat.authenticator_type));
    authToken.timestamp.milliSeconds = static_cast<int64_t>(be64toh(hat.timestamp));
    authToken.mac.assign(std::begin(hat.hmac), std::end(hat.hmac));
}

}  // namespace aidl::android::hardware::biometrics::fingerprint::kj7
