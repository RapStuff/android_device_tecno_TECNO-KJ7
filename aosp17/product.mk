# SPDX-License-Identifier: Apache-2.0

ifeq ($(TARGET_PRODUCT),aosp_TECNO_KJ7)
# AOSP resolves out/.../kernel through PRODUCT_COPY_FILES. LineageOS
# generates the kernel target from TARGET_PREBUILT_KERNEL in kernel.mk.
PRODUCT_COPY_FILES += device/tecno/TECNO-KJ7-kernel/kernel:kernel
endif

# Retain HIDL discovery for the Android 12 vendor and install its original
# framework FCM as an additional Android 17 system matrix.
PRODUCT_HIDL_ENABLED := true
PRODUCT_PACKAGES += \
    hwservicemanager \
    kj7_a17_fcm6

$(call inherit-product, device/tecno/TECNO_KJ7/aosp17/vintf-staging.mk)

# Keep OTA verification of the actual kernel release and embedded config.
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := true
