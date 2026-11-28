# SPDX-License-Identifier: Apache-2.0

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/window_extensions.mk)

# Inherit from kj7 device
$(call inherit-product, device/tecno/kj7/device.mk)

# Maintainer & GApps
MIKU_MASTER := ErOneSoul
MIKU_GAPPS ?= true

# Inherit some common Miku UI stuff.
$(call inherit-product, vendor/miku/build/product/miku_product.mk)

PRODUCT_DEVICE := kj7
PRODUCT_NAME := miku_kj7
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
