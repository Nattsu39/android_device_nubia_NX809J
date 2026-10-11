# Copyright (C) 2026 The Avium Project
# SPDX-License-Identifier: Apache-2.0

PRODUCT_PACKAGES += libnx809j_camera_provider
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/camera/nx809j-camera-provider.rc:$(TARGET_COPY_OUT_ODM)/etc/init/nx809j-camera-provider.rc

# The HAL adapter exposes API1 indices 0 -> rear ID0 and 1 -> front ID3.
# Sense already supports this explicit API1 index; keep its source unchanged.
PRODUCT_SYSTEM_EXT_PROPERTIES += ro.face.sense_service.camera_id=1
