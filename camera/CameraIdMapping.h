// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/camera/device/StreamConfiguration.h>

#include <string>
#include <vector>

namespace nx809j::camera {

// The swap is its own inverse. IDs within OEM interfaces and vendor metadata
// remain unchanged; only standard camera interfaces use the public numbering.
std::string mapCameraId(const std::string& id);
void mapMetadataCameraIds(std::vector<uint8_t>* metadata);
aidl::android::hardware::camera::device::StreamConfiguration mapStreamConfiguration(
    const aidl::android::hardware::camera::device::StreamConfiguration& configuration);

}  // namespace nx809j::camera
