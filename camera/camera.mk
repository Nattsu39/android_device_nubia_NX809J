# Copyright (C) 2026 The Avium Project
# SPDX-License-Identifier: Apache-2.0

PRODUCT_PACKAGES += libnx809j_camera_provider nx809j-camera-provider
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/camera/nx809j-camera-provider.rc:$(TARGET_COPY_OUT_ODM)/etc/init/nx809j-camera-provider.rc
