/*
 * Copyright (C) 2021 The Android Open Source Project
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/strings.h>
#include <android/binder_interface_utils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

#ifndef CHARGER_FORCE_NO_UI
#define CHARGER_FORCE_NO_UI 0
#endif

#if !CHARGER_FORCE_NO_UI
#include <health-impl/ChargerUtils.h>
#endif

using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;
using aidl::android::hardware::health::HealthInfo;

#if !CHARGER_FORCE_NO_UI
using aidl::android::hardware::health::charger::ChargerCallback;
using aidl::android::hardware::health::charger::ChargerModeMain;
#endif

namespace {

constexpr const char* kInstanceName = "default";
constexpr std::string_view kChargerArg{"--charger"};

constexpr const char* kAdapterCapacityPath = "/sys/devices/platform/charger/adapter_capacity";
constexpr const char* kChargingModePath = "/sys/devices/platform/charger/Charging_mode";
constexpr const char* kAdcChargerVoltagePath = "/sys/devices/platform/charger/ADC_Charger_Voltage";

constexpr const char* kPrimaryDvchgPresentPath = "/sys/class/power_supply/primary_dvchg/present";
constexpr const char* kPrimaryDvchgVoltagePath = "/sys/class/power_supply/primary_dvchg/voltage_now";

constexpr const char* kChargerOnlinePath = "/sys/class/power_supply/charger/online";
constexpr const char* kChargerUsbTypePath = "/sys/class/power_supply/charger/usb_type";
constexpr const char* kChargerVoltageMaxPath = "/sys/class/power_supply/charger/voltage_max";
constexpr const char* kChargerCurrentMaxPath = "/sys/class/power_supply/charger/current_max";
constexpr const char* kChargerInputLimitPath =
        "/sys/class/power_supply/charger/input_current_limit";

bool ReadSysfsString(const char* path, std::string* out) {
    std::string raw;
    if (!android::base::ReadFileToString(path, &raw)) {
        return false;
    }
    *out = android::base::Trim(raw);
    return !out->empty();
}

bool ReadSysfsInt(const char* path, int32_t* out) {
    std::string val;
    if (!ReadSysfsString(path, &val)) {
        return false;
    }
    return android::base::ParseInt(val, out);
}

bool HasActiveUsbTag(const std::string& usb_type, const char* tag) {
    return usb_type.find(tag) != std::string::npos;
}

/**
 * KJ7 keeps switching charger psy type as USB even during 33W PE5.0/PD negotiation,
 * while the SC8548 charge pump exposes no online/current_max/voltage_max attributes.
 * Bridge negotiated adapter wattage and VBUS telemetry into HealthInfo so Android
 * reports accurate fast/rapid charging wattage instead of battery instantaneous draw.
 */
class Kj7Health final : public Health {
  public:
    Kj7Health(std::string_view instance_name, std::unique_ptr<struct healthd_config>&& config,
              bool is_charger_mode)
        : Health(instance_name, std::move(config)), is_charger_mode_(is_charger_mode) {}

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override {
        if (is_charger_mode_ || health_info == nullptr) {
            return;
        }

        int32_t chg_online = 0;
        int32_t dvchg_present = 0;
        ReadSysfsInt(kChargerOnlinePath, &chg_online);
        ReadSysfsInt(kPrimaryDvchgPresentPath, &dvchg_present);

        const bool wired_online = health_info->chargerAcOnline || health_info->chargerUsbOnline ||
                                  chg_online == 1 || dvchg_present == 1;
        if (!wired_online) {
            return;
        }

        int32_t adapter_watts = 0;
        ReadSysfsInt(kAdapterCapacityPath, &adapter_watts);

        std::string chg_mode;
        ReadSysfsString(kChargingModePath, &chg_mode);

        int32_t vbus_mv = 0;
        ReadSysfsInt(kAdcChargerVoltagePath, &vbus_mv);

        int32_t dvchg_uv = 0;
        ReadSysfsInt(kPrimaryDvchgVoltagePath, &dvchg_uv);

        const bool is_pe5 =
                (adapter_watts >= 20 || chg_mode == "P5" || dvchg_present == 1);
        const bool is_hv_fast =
                (adapter_watts > 0 || chg_mode == "PE4" || chg_mode == "PDC" ||
                 chg_mode == "PE2" || chg_mode == "PE");

        if (is_pe5 || is_hv_fast) {
            health_info->chargerAcOnline = true;
            health_info->chargerUsbOnline = false;

            const int32_t watts = (adapter_watts > 0) ? adapter_watts : (is_pe5 ? 33 : 18);
            int32_t voltage_uv = is_pe5 ? 10000000 : 9000000;
            if (vbus_mv >= 6500) {
                voltage_uv = vbus_mv * 1000;
            } else if (dvchg_uv >= 6500000) {
                voltage_uv = dvchg_uv;
            }

            const int64_t microwatts = static_cast<int64_t>(watts) * 1000000000LL;
            health_info->maxChargingVoltageMicrovolts = voltage_uv;
            health_info->maxChargingCurrentMicroamps = static_cast<int32_t>(microwatts / voltage_uv);
            return;
        }

        std::string usb_type;
        ReadSysfsString(kChargerUsbTypePath, &usb_type);

        int32_t chg_vmax_uv = 0;
        int32_t chg_imax_ua = 0;
        int32_t chg_icl_ua = 0;
        ReadSysfsInt(kChargerVoltageMaxPath, &chg_vmax_uv);
        ReadSysfsInt(kChargerCurrentMaxPath, &chg_imax_ua);
        ReadSysfsInt(kChargerInputLimitPath, &chg_icl_ua);

        const bool is_dcp = HasActiveUsbTag(usb_type, "[DCP]");
        const bool is_pd =
                HasActiveUsbTag(usb_type, "[PD]") || HasActiveUsbTag(usb_type, "[PD_DRP]");
        if (is_dcp || is_pd) {
            health_info->chargerAcOnline = true;
            health_info->chargerUsbOnline = false;
        }

        const int32_t voltage_uv = (chg_vmax_uv > 0) ? chg_vmax_uv : 5000000;
        const int32_t current_ua = std::max(chg_imax_ua, chg_icl_ua);
        if (current_ua > 0) {
            health_info->maxChargingVoltageMicrovolts = voltage_uv;
            health_info->maxChargingCurrentMicroamps = current_ua;
        }
    }

  private:
    bool is_charger_mode_ = false;
};

}  // namespace

#if !CHARGER_FORCE_NO_UI
namespace aidl::android::hardware::health {
class ChargerCallbackImpl : public ChargerCallback {
  public:
    using ChargerCallback::ChargerCallback;
    bool ChargerEnableSuspend() override { return true; }
};
}  // namespace aidl::android::hardware::health
#endif

int main(int argc, char** argv) {
#ifdef __ANDROID_RECOVERY__
    android::base::InitLogging(argv, android::base::KernelLogger);
#endif

    const bool is_charger_mode = (argc >= 2 && argv[1] == kChargerArg);

    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    auto binder = ndk::SharedRefBase::make<Kj7Health>(kInstanceName, std::move(config),
                                                      is_charger_mode);

    if (is_charger_mode) {
#if !CHARGER_FORCE_NO_UI
        return ChargerModeMain(
                binder,
                std::make_shared<aidl::android::hardware::health::ChargerCallbackImpl>(binder));
#endif

        LOG(INFO) << "Starting charger mode without UI.";
    } else {
        LOG(INFO) << "Starting health HAL.";
    }

    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
