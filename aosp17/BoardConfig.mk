# SPDX-License-Identifier: Apache-2.0
# Android 17 owns BOARD_SEPOLICY_VERS; the stock vendor continues to use
# its shipped 31.0 policy and AOSP provides the corresponding system mapping.
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE := \
    $(DEVICE_PATH)/vintf/framework_compatibility_matrix.xml
# The staged manifest and fragments preserve the stock file boundaries and
# metadata versions, including legacy HIDL minor-version declarations.
DEVICE_MANIFEST_FILE :=
TARGET_COPY_OUT_ODM := vendor/odm
ODM_MANIFEST_SKUS := dsds qsqs ss tsts

ifeq ($(KJ7_REBUILD_VENDOR),true)
$(error AOSP 17 bringup currently requires the stock vendor image)
endif
