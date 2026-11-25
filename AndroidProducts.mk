# SPDX-License-Identifier: Apache-2.0
PRODUCT_MAKEFILES := \
    shinkai_KJ7:$(LOCAL_DIR)/shinkai_KJ7.mk

# Android 14+ requires PRODUCT_NAME to be a C identifier.
COMMON_LUNCH_CHOICES := \
    shinkai_KJ7-cp2a-userdebug \
    shinkai_KJ7-cp2a-eng \
    shinkai_KJ7-userdebug \
    shinkai_KJ7-eng
