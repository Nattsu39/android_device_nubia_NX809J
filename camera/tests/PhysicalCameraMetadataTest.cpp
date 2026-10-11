// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <system/camera_metadata.h>

#include <cstring>
#include <iterator>
#include <memory>

#include "PhysicalCameraMetadata.h"

namespace nx809j::camera {
namespace {

using Metadata = std::unique_ptr<camera_metadata_t, decltype(&free_camera_metadata)>;

std::vector<uint8_t> makeMetadata(const std::vector<uint8_t>& physicalIds,
                                  uint8_t facing = ANDROID_LENS_FACING_BACK) {
    Metadata metadata(allocate_camera_metadata(8, 128), free_camera_metadata);
    EXPECT_NE(metadata, nullptr);
    const uint8_t capabilities[] = {
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE,
        ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA,
    };
    const int32_t orientation = 90;
    EXPECT_EQ(add_camera_metadata_entry(metadata.get(), ANDROID_LENS_FACING, &facing, 1), 0);
    EXPECT_EQ(
        add_camera_metadata_entry(metadata.get(), ANDROID_SENSOR_ORIENTATION, &orientation, 1), 0);
    EXPECT_EQ(add_camera_metadata_entry(metadata.get(), ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                                        capabilities, std::size(capabilities)),
              0);
    EXPECT_EQ(add_camera_metadata_entry(metadata.get(), ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                                        physicalIds.data(), physicalIds.size()),
              0);
    const auto* bytes = reinterpret_cast<const uint8_t*>(metadata.get());
    return {bytes, bytes + get_camera_metadata_size(metadata.get())};
}

FacingResolver publicFacing = [](const std::string& id) -> std::optional<uint8_t> {
    if (id == "1" || id == "2") return ANDROID_LENS_FACING_BACK;
    if (id == "3") return ANDROID_LENS_FACING_FRONT;
    return std::nullopt;
};

std::vector<uint8_t> physicalIds(const std::vector<uint8_t>& bytes) {
    camera_metadata_ro_entry_t entry;
    EXPECT_EQ(
        find_camera_metadata_ro_entry(reinterpret_cast<const camera_metadata_t*>(bytes.data()),
                                      ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS, &entry),
        0);
    return {entry.data.u8, entry.data.u8 + entry.count};
}

TEST(PhysicalCameraMetadata, RemovesFrontFromRearLogicalGroup) {
    auto bytes = makeMetadata({'2', '\0', '1', '\0', '3', '\0'});
    const auto original = bytes;
    ASSERT_TRUE(filterPhysicalCameraIds(&bytes, publicFacing));
    EXPECT_EQ(physicalIds(bytes), (std::vector<uint8_t>{'2', '\0', '1', '\0'}));

    const auto* before = reinterpret_cast<const camera_metadata_t*>(original.data());
    const auto* after = reinterpret_cast<const camera_metadata_t*>(bytes.data());
    ASSERT_EQ(get_camera_metadata_entry_count(before), get_camera_metadata_entry_count(after));
    for (size_t index = 0; index < get_camera_metadata_entry_count(before); ++index) {
        camera_metadata_ro_entry_t oldEntry, newEntry;
        ASSERT_EQ(get_camera_metadata_ro_entry(before, index, &oldEntry), 0);
        if (oldEntry.tag == ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS) continue;
        ASSERT_EQ(find_camera_metadata_ro_entry(after, oldEntry.tag, &newEntry), 0);
        ASSERT_EQ(oldEntry.type, newEntry.type);
        ASSERT_EQ(oldEntry.count, newEntry.count);
        const size_t size = oldEntry.count * camera_metadata_type_size[oldEntry.type];
        EXPECT_EQ(memcmp(oldEntry.data.u8, newEntry.data.u8, size), 0);
    }
}

TEST(PhysicalCameraMetadata, KeepsSameFacingGroupByteForByte) {
    auto bytes = makeMetadata({'2', '\0', '1', '\0'});
    const auto original = bytes;
    EXPECT_FALSE(filterPhysicalCameraIds(&bytes, publicFacing));
    EXPECT_EQ(bytes, original);
}

TEST(PhysicalCameraMetadata, KeepsHiddenPhysicalIds) {
    auto bytes = makeMetadata({'2', '\0', '1', '\0', '4', '\0', '3', '\0'});
    ASSERT_TRUE(filterPhysicalCameraIds(&bytes, publicFacing));
    EXPECT_EQ(physicalIds(bytes), (std::vector<uint8_t>{'2', '\0', '1', '\0', '4', '\0'}));
}

TEST(PhysicalCameraMetadata, RejectsUnterminatedIdsWithoutChangingMetadata) {
    auto bytes = makeMetadata({'2', '\0', '1', '\0', '3'});
    const auto original = bytes;
    EXPECT_FALSE(filterPhysicalCameraIds(&bytes, publicFacing));
    EXPECT_EQ(bytes, original);
}

TEST(PhysicalCameraMetadata, DoesNotLeaveAnInvalidLogicalGroup) {
    auto bytes = makeMetadata({'1', '\0', '3', '\0'});
    const auto original = bytes;
    EXPECT_FALSE(filterPhysicalCameraIds(&bytes, publicFacing));
    EXPECT_EQ(bytes, original);
}

TEST(PhysicalCameraMetadata, RejectsTruncatedMetadata) {
    auto bytes = makeMetadata({'2', '\0', '1', '\0', '3', '\0'});
    bytes.resize(16);
    const auto original = bytes;
    EXPECT_FALSE(filterPhysicalCameraIds(&bytes, publicFacing));
    EXPECT_EQ(bytes, original);
}

}  // namespace
}  // namespace nx809j::camera
