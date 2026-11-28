# SPDX-License-Identifier: Apache-2.0
PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/miku_kj7.mk

# Android 14+ requires PRODUCT_NAME to be a C identifier.
COMMON_LUNCH_CHOICES := \
    miku_kj7-cp2a-userdebug \
    miku_kj7-cp2a-eng \
    miku_kj7-userdebug \
    miku_kj7-eng
