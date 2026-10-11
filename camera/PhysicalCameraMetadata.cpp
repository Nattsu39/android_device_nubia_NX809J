// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#include "PhysicalCameraMetadata.h"

#include <system/camera_metadata.h>

#include <algorithm>
#include <memory>

namespace nx809j::camera {
namespace {

using Metadata = std::unique_ptr<camera_metadata_t, decltype(&free_camera_metadata)>;

Metadata copyMetadata(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return {nullptr, free_camera_metadata};
    return {allocate_copy_camera_metadata_checked(
                    reinterpret_cast<const camera_metadata_t*>(bytes.data()), bytes.size()),
            free_camera_metadata};
}

std::optional<uint8_t> readFacing(const camera_metadata_t* metadata) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(metadata, ANDROID_LENS_FACING, &entry) != 0 ||
        entry.type != TYPE_BYTE || entry.count != 1) {
        return std::nullopt;
    }
    return entry.data.u8[0];
}

}  // namespace

std::optional<uint8_t> getCameraFacing(const std::vector<uint8_t>& bytes) {
    const auto metadata = copyMetadata(bytes);
    return metadata ? readFacing(metadata.get()) : std::nullopt;
}

bool filterPhysicalCameraIds(std::vector<uint8_t>* bytes, const FacingResolver& resolveFacing) {
    auto metadata = copyMetadata(*bytes);
    if (!metadata) return false;
    const auto facing = readFacing(metadata.get());
    if (!facing) return false;

    camera_metadata_ro_entry_t capabilities, ids;
    if (find_camera_metadata_ro_entry(metadata.get(), ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                      &capabilities) != 0 || capabilities.type != TYPE_BYTE ||
        std::find(capabilities.data.u8, capabilities.data.u8 + capabilities.count,
                  ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA) ==
                capabilities.data.u8 + capabilities.count ||
        find_camera_metadata_ro_entry(metadata.get(), ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                                      &ids) != 0 || ids.type != TYPE_BYTE || ids.count == 0) {
        return false;
    }

    std::vector<uint8_t> retained;
    size_t retainedCount = 0;
    for (size_t start = 0; start < ids.count;) {
        const auto* begin = ids.data.u8 + start;
        const auto* end = std::find(begin, ids.data.u8 + ids.count, '\0');
        if (end == ids.data.u8 + ids.count || end == begin) return false;
        const std::string id(reinterpret_cast<const char*>(begin), end - begin);
        const auto physicalFacing = resolveFacing(id);
        if (!physicalFacing || *physicalFacing == *facing) {
            retained.insert(retained.end(), begin, end + 1);
            ++retainedCount;
        }
        start = end - ids.data.u8 + 1;
    }

    // A logical camera must retain at least two physical devices. Leave
    // unexpected metadata alone instead of advertising an invalid group.
    if (retained.size() == ids.count || retainedCount < 2) return false;
    if (update_camera_metadata_entry(metadata.get(), ids.index, retained.data(),
                                    retained.size(), nullptr) != 0) {
        return false;
    }
    const auto* data = reinterpret_cast<const uint8_t*>(metadata.get());
    bytes->assign(data, data + get_camera_metadata_size(metadata.get()));
    return true;
}

}  // namespace nx809j::camera
