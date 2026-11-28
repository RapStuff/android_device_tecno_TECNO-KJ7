# SPDX-License-Identifier: Apache-2.0
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, device/tecno/kj7/device.mk)
$(call inherit-product, vendor/custom/config/common_full_phone.mk)

TARGET_PROVIDES_FACE_UNLOCK_HAL := true
SHINKAI_MAINTAINER := ErOneSoul

PRODUCT_DEVICE := kj7
PRODUCT_NAME := shinkai_kj7
PRODUCT_BRAND := TECNO
PRODUCT_MODEL := Tecno Spark 20 Pro+
PRODUCT_MANUFACTURER := TECNO

PRODUCT_BRAND_FOR_ATTESTATION := TECNO
PRODUCT_DEVICE_FOR_ATTESTATION := TECNO-KJ7
PRODUCT_MANUFACTURER_FOR_ATTESTATION := TECNO
PRODUCT_MODEL_FOR_ATTESTATION := TECNO KJ7
PRODUCT_NAME_FOR_ATTESTATION := KJ7-OP

PRODUCT_GMS_CLIENTID_BASE := android-transsion

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="KJ7-OP-user 14 UP1A.231005.007 241214V2496 release-keys" \
    BuildFingerprint=TECNO/KJ7-OP/TECNO-KJ7:14/UP1A.231005.007/241214V2496:user/release-keys \
    DeviceName=TECNO-KJ7 \
    DeviceProduct=KJ7-OP
