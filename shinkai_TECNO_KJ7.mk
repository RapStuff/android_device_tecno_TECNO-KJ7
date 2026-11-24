# SPDX-License-Identifier: Apache-2.0
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, device/tecno/TECNO_KJ7/device.mk)
$(call inherit-product, vendor/custom/config/common_full_phone.mk)

TARGET_PROVIDES_FACE_UNLOCK_HAL := true
SHINKAI_MAINTAINER := ErOneSoul

PRODUCT_DEVICE := TECNO_KJ7
PRODUCT_NAME := shinkai_TECNO_KJ7
PRODUCT_BRAND := TECNO
PRODUCT_MODEL := Tecno Spark 20 Pro+
PRODUCT_MANUFACTURER := TECNO
