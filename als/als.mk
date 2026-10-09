# Copyright (C) 2026 The Avium Project
# SPDX-License-Identifier: Apache-2.0

PRODUCT_PACKAGES += nx809j-als-content-service
PRODUCT_SYSTEM_EXT_PROPERTIES += ro.nx809j.als_content_compensation=true

# Exact dependency closure, verified by als/verify_libraries.py.
# Reuse existing vendor inputs without changing /vendor or its policy/labels.
NX809J_ALS_LIBRARIES := \
    android.hardware.sensors@1.0.so \
    libbase.so \
    libcutils.so \
    libhidlbase.so \
    libhidltransport.so \
    libhwbinder.so \
    libprotobuf-cpp-lite-21.12.so \
    libqmi_cci.so \
    libqmi_common_so.so \
    libqmi_encdec.so \
    libqsh.so \
    libsensorapi_vendor.so \
    libsensorcal_vendor_vendor.so \
    libsns_set_brightness_vendor.so \
    libsnsapi.so \
    libsnsutils.so \
    libutils.so

PRODUCT_COPY_FILES += $(foreach library,$(NX809J_ALS_LIBRARIES), \
    vendor/nubia/NX809J/proprietary/vendor/lib64/$(library):$(TARGET_COPY_OUT_SYSTEM_EXT)/lib64/nx809j-als/$(library))
