# SPDX-License-Identifier: Apache-2.0
# AOSP android17-release, upgraded Android 12 vendor interface.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_system.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/handheld_system_ext.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/telephony_system_ext.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_product.mk)
$(call inherit-product, device/tecno/TECNO-KJ7/device.mk)
$(call inherit-product, device/tecno/TECNO-KJ7/aosp17/product.mk)

PRODUCT_NAME := aosp_TECNO_KJ7
PRODUCT_DEVICE := TECNO_KJ7
PRODUCT_BRAND := TECNO
PRODUCT_MODEL := TECNO KJ7
PRODUCT_MANUFACTURER := TECNO
