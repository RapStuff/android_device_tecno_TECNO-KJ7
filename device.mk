# SPDX-License-Identifier: Apache-2.0
LOCAL_PATH := device/tecno/TECNO-KJ7

$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/compression.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/developer_gsi_keys.mk)

PRODUCT_SHIPPING_API_LEVEL := 31
PRODUCT_EXTRA_VNDK_VERSIONS := 31
PRODUCT_USE_DYNAMIC_PARTITIONS := true
PRODUCT_BUILD_SUPER_PARTITION := true
PRODUCT_BUILD_INIT_BOOT_IMAGE := false
PRODUCT_CHARACTERISTICS := nosdcard

TARGET_SCREEN_HEIGHT := 2436
TARGET_SCREEN_WIDTH := 1080
TARGET_SCREEN_DENSITY := 480
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xxhdpi
PRODUCT_SYSTEM_PROPERTIES += ro.sf.lcd_density=480
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay
ifneq ($(TARGET_PRODUCT),aosp_TECNO_KJ7)
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay-lineage
endif

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/fstab.mt6789:$(TARGET_COPY_OUT_VENDOR_RAMDISK)/first_stage_ramdisk/fstab.mt6789

$(call inherit-product, vendor/tecno/TECNO-KJ7/TECNO-KJ7-vendor.mk)
