# SPDX-License-Identifier: Apache-2.0
PRODUCT_MAKEFILES := \
    lineage_KJ7:$(LOCAL_DIR)/lineage_KJ7.mk

# Android 14+ requires PRODUCT_NAME to be a C identifier.
COMMON_LUNCH_CHOICES := \
    lineage_KJ7-cp2a-userdebug \
    lineage_KJ7-cp2a-eng \
    lineage_KJ7-userdebug \
    lineage_KJ7-eng
