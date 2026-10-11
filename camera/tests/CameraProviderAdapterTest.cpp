// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#include "CameraProviderAdapter.h"
#include "PhysicalCameraMetadata.h"

#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <android/binder_manager.h>
#include <gtest/gtest.h>
#include <system/camera_metadata.h>

#include <cstring>

namespace nx809j::camera {
namespace {

namespace device = aidl::android::hardware::camera::device;
namespace provider = aidl::android::hardware::camera::provider;
constexpr const char* kInstance = "android.hardware.camera.provider.ICameraProvider/vendor_qti/0";
constexpr const char* kRear = "device@1.1/vendor_qti/0";

class CameraProviderAdapterTest : public testing::Test {
protected:
    void SetUp() override {
        original = provider::ICameraProvider::fromBinder(
                ndk::SpAIBinder(AServiceManager_checkService(kInstance)));
        ASSERT_NE(original, nullptr) << "The NX809J OEM camera provider must be running";
        int32_t version = 0;
        ASSERT_TRUE(original->getInterfaceVersion(&version).isOk());
        ASSERT_EQ(version, provider::ICameraProvider::version);
        adapted = wrapCameraProvider(original);
        ASSERT_NE(adapted, original);
    }

    std::shared_ptr<provider::ICameraProvider> original;
    std::shared_ptr<provider::ICameraProvider> adapted;
};

TEST_F(CameraProviderAdapterTest, RealProviderPreservesIdsVendorTagsAndConcurrentGroups) {
    std::vector<std::string> beforeIds, afterIds;
    ASSERT_TRUE(original->getCameraIdList(&beforeIds).isOk());
    ASSERT_TRUE(adapted->getCameraIdList(&afterIds).isOk());
    ASSERT_EQ(beforeIds.size(), 4u);
    EXPECT_EQ(beforeIds, afterIds);
    std::vector<aidl::android::hardware::camera::common::VendorTagSection> beforeTags, afterTags;
    ASSERT_TRUE(original->getVendorTags(&beforeTags).isOk());
    ASSERT_TRUE(adapted->getVendorTags(&afterTags).isOk());
    EXPECT_EQ(beforeTags, afterTags);
    std::vector<provider::ConcurrentCameraIdCombination> beforeGroups, afterGroups;
    ASSERT_TRUE(original->getConcurrentCameraIds(&beforeGroups).isOk());
    ASSERT_TRUE(adapted->getConcurrentCameraIds(&afterGroups).isOk());
    EXPECT_EQ(beforeGroups, afterGroups);
}

TEST_F(CameraProviderAdapterTest, RealMetadataChangesOnlyCrossFacingPhysicalIds) {
    std::vector<std::string> names;
    ASSERT_TRUE(original->getCameraIdList(&names).isOk());
    for (const auto& name : names) {
        std::shared_ptr<device::ICameraDevice> beforeDevice, afterDevice;
        ASSERT_TRUE(original->getCameraDeviceInterface(name, &beforeDevice).isOk());
        ASSERT_TRUE(adapted->getCameraDeviceInterface(name, &afterDevice).isOk());
        ASSERT_NE(beforeDevice, nullptr);
        ASSERT_NE(afterDevice, nullptr);
        device::CameraMetadata before, after;
        ASSERT_TRUE(beforeDevice->getCameraCharacteristics(&before).isOk());
        ASSERT_TRUE(afterDevice->getCameraCharacteristics(&after).isOk());
        EXPECT_EQ(getCameraFacing(before.metadata), getCameraFacing(after.metadata));
        if (name != kRear) {
            EXPECT_EQ(before.metadata, after.metadata) << name;
            continue;
        }
        const auto* oldMetadata = reinterpret_cast<const camera_metadata_t*>(before.metadata.data());
        const auto* newMetadata = reinterpret_cast<const camera_metadata_t*>(after.metadata.data());
        ASSERT_EQ(get_camera_metadata_entry_count(oldMetadata),
                  get_camera_metadata_entry_count(newMetadata));
        for (size_t index = 0; index < get_camera_metadata_entry_count(oldMetadata); ++index) {
            camera_metadata_ro_entry_t oldEntry, newEntry;
            ASSERT_EQ(get_camera_metadata_ro_entry(oldMetadata, index, &oldEntry), 0);
            ASSERT_EQ(find_camera_metadata_ro_entry(newMetadata, oldEntry.tag, &newEntry), 0);
            ASSERT_EQ(oldEntry.type, newEntry.type);
            if (oldEntry.tag == ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS) {
                EXPECT_EQ((std::vector<uint8_t>{oldEntry.data.u8, oldEntry.data.u8 + oldEntry.count}),
                          (std::vector<uint8_t>{'2', '\0', '1', '\0', '3', '\0'}));
                EXPECT_EQ((std::vector<uint8_t>{newEntry.data.u8, newEntry.data.u8 + newEntry.count}),
                          (std::vector<uint8_t>{'2', '\0', '1', '\0'}));
            } else {
                ASSERT_EQ(oldEntry.count, newEntry.count);
                EXPECT_EQ(memcmp(oldEntry.data.u8, newEntry.data.u8,
                                 oldEntry.count * camera_metadata_type_size[oldEntry.type]), 0)
                        << "Changed unrelated metadata tag " << oldEntry.tag;
            }
        }
        aidl::android::hardware::camera::common::CameraResourceCost beforeCost, afterCost;
        ASSERT_TRUE(beforeDevice->getResourceCost(&beforeCost).isOk());
        ASSERT_TRUE(afterDevice->getResourceCost(&afterCost).isOk());
        EXPECT_EQ(beforeCost, afterCost);
    }
}

// These local delegators attach non-null extensions without registering any
// service or changing the live HAL. All metadata still comes from the real HAL.
class ExtendedProvider : public provider::ICameraProviderDelegator {
public:
    ExtendedProvider(std::shared_ptr<provider::ICameraProvider> original,
                     std::shared_ptr<device::ICameraDevice> rear)
        : ICameraProviderDelegator(original), mOriginal(std::move(original)), mRear(std::move(rear)) {}

    ndk::ScopedAStatus getCameraDeviceInterface(
            const std::string& name, std::shared_ptr<device::ICameraDevice>* output) override {
        if (name == kRear) {
            *output = mRear;
            return ndk::ScopedAStatus::ok();
        }
        return mOriginal->getCameraDeviceInterface(name, output);
    }

private:
    const std::shared_ptr<provider::ICameraProvider> mOriginal;
    const std::shared_ptr<device::ICameraDevice> mRear;
};

TEST_F(CameraProviderAdapterTest, PreservesNonNullProviderAndDeviceBinderExtensions) {
    std::shared_ptr<device::ICameraDevice> rear;
    ASSERT_TRUE(original->getCameraDeviceInterface(kRear, &rear).isOk());
    auto extendedDevice = ndk::SharedRefBase::make<device::ICameraDeviceDelegator>(rear);
    const auto deviceBinder = extendedDevice->asBinder();
    const auto extension = original->asBinder();
    ASSERT_EQ(AIBinder_setExtension(deviceBinder.get(), extension.get()), STATUS_OK);
    auto extendedProvider = ndk::SharedRefBase::make<ExtendedProvider>(original, extendedDevice);
    const auto providerBinder = extendedProvider->asBinder();
    ASSERT_EQ(AIBinder_setExtension(providerBinder.get(), extension.get()), STATUS_OK);

    auto wrapped = wrapCameraProvider(extendedProvider);
    ndk::SpAIBinder providerExtension;
    ASSERT_EQ(AIBinder_getExtension(wrapped->asBinder().get(), providerExtension.getR()), STATUS_OK);
    EXPECT_EQ(providerExtension.get(), extension.get());
    std::shared_ptr<device::ICameraDevice> wrappedDevice;
    ASSERT_TRUE(wrapped->getCameraDeviceInterface(kRear, &wrappedDevice).isOk());
    ndk::SpAIBinder deviceExtension;
    ASSERT_EQ(AIBinder_getExtension(wrappedDevice->asBinder().get(), deviceExtension.getR()), STATUS_OK);
    EXPECT_EQ(deviceExtension.get(), extension.get());
}

}  // namespace
}  // namespace nx809j::camera
