# SPDX-License-Identifier: Apache-2.0
PRODUCT_MAKEFILES := \
    lineage_kj7:$(LOCAL_DIR)/lineage_kj7.mk

# Android 14+ requires PRODUCT_NAME to be a C identifier.
COMMON_LUNCH_CHOICES := \
    lineage_kj7-cp2a-userdebug \
    lineage_kj7-cp2a-eng \
    lineage_kj7-userdebug \
    lineage_kj7-eng
