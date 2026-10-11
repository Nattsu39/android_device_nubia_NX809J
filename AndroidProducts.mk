#
# Copyright (C) 2025 Evolution X (adapted from LineageOS device config)
#
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/evolution_NX809J.mk

COMMON_LUNCH_CHOICES := \
    evolution_NX809J-bp4a-userdebug \
    evolution_NX809J-bp4a-eng

# Register the AviumUI product alongside the shared Evolution X product.
PRODUCT_MAKEFILES += \
    $(LOCAL_DIR)/lineage_NX809J.mk

COMMON_LUNCH_CHOICES += \
    lineage_NX809J-bp4a-userdebug \
    lineage_NX809J-bp4a-eng
