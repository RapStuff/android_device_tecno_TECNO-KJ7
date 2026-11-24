# SPDX-License-Identifier: Apache-2.0
PRODUCT_MAKEFILES := \
    shinkai_TECNO_KJ7:$(LOCAL_DIR)/shinkai_TECNO_KJ7.mk

# Android 14+ requires PRODUCT_NAME to be a C identifier.
COMMON_LUNCH_CHOICES := \
    shinkai_TECNO_KJ7-cp2a-userdebug \
    shinkai_TECNO_KJ7-cp2a-eng \
    shinkai_TECNO_KJ7-userdebug \
    shinkai_TECNO_KJ7-eng
