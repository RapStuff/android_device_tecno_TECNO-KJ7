# SPDX-License-Identifier: Apache-2.0
PRODUCT_MAKEFILES := \
    shinkai_kj7:$(LOCAL_DIR)/shinkai_kj7.mk

# Android 14+ requires PRODUCT_NAME to be a C identifier.
COMMON_LUNCH_CHOICES := \
    shinkai_kj7-cp2a-userdebug \
    shinkai_kj7-cp2a-eng \
    shinkai_kj7-userdebug \
    shinkai_kj7-eng
