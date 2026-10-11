// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#include "CameraIdMapping.h"

#include <system/camera_metadata.h>

#include <algorithm>
#include <memory>

namespace nx809j::camera {

std::string mapCameraId(const std::string& id) {
    const auto separator = id.find_last_of('/');
    const auto start = separator == std::string::npos ? 0 : separator + 1;
    if (start != 0 && id.substr(0, start) != "device@1.1/vendor_qti/") return id;
    if (id.size() - start != 1 || (id[start] != '1' && id[start] != '3')) return id;
    auto mapped = id;
    mapped[start] = id[start] == '1' ? '3' : '1';
    return mapped;
}

void mapMetadataCameraIds(std::vector<uint8_t>* bytes) {
    if (bytes->empty()) return;
    using Metadata = std::unique_ptr<camera_metadata_t, decltype(&free_camera_metadata)>;
    Metadata metadata(allocate_copy_camera_metadata_checked(
                          reinterpret_cast<const camera_metadata_t*>(bytes->data()), bytes->size()),
                      free_camera_metadata);
    if (!metadata || get_camera_metadata_size(metadata.get()) != bytes->size()) return;
    bool changed = false;
    for (const auto tag : {ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                           ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID}) {
        camera_metadata_entry_t entry;
        if (find_camera_metadata_entry(metadata.get(), tag, &entry) != 0 ||
            entry.type != TYPE_BYTE || entry.count == 0 || entry.data.u8[entry.count - 1] != '\0') {
            continue;
        }
        for (size_t start = 0; start < entry.count;) {
            auto* begin = entry.data.u8 + start;
            auto* end = std::find(begin, entry.data.u8 + entry.count, '\0');
            if (end - begin == 1 && (*begin == '1' || *begin == '3')) {
                *begin = *begin == '1' ? '3' : '1';
                changed = true;
            }
            start = end - entry.data.u8 + 1;
        }
    }
    if (changed) {
        const auto* data = reinterpret_cast<const uint8_t*>(metadata.get());
        bytes->assign(data, data + bytes->size());
    }
}

aidl::android::hardware::camera::device::StreamConfiguration mapStreamConfiguration(
    const aidl::android::hardware::camera::device::StreamConfiguration& configuration) {
    auto mapped = configuration;
    for (auto& stream : mapped.streams)
        stream.physicalCameraId = mapCameraId(stream.physicalCameraId);
    return mapped;
}

}  // namespace nx809j::camera
