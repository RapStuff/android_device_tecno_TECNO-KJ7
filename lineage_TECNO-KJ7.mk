# SPDX-License-Identifier: Apache-2.0
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)
$(call inherit-product, device/tecno/TECNO-KJ7/device.mk)
ifeq ($(wildcard hardware/interfaces/compatibility_matrices/compatibility_matrix.6.xml),)
$(call inherit-product, device/tecno/TECNO-KJ7/aosp17/product.mk)
endif
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_DEVICE := TECNO_KJ7
PRODUCT_NAME := lineage_TECNO_KJ7
PRODUCT_BRAND := TECNO
PRODUCT_MODEL := TECNO KJ7
PRODUCT_MANUFACTURER := TECNO
