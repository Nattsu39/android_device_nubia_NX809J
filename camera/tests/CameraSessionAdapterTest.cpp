// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#include <aidl/android/hardware/camera/device/ICameraOfflineSession.h>
#include <gtest/gtest.h>
#include <system/camera_metadata.h>

#include "CameraSessionAdapter.h"

namespace nx809j::camera {
namespace {

namespace device = aidl::android::hardware::camera::device;

class Callback : public device::ICameraDeviceCallbackDefault {
   public:
    ndk::ScopedAStatus getInterfaceVersion(int32_t* output) override {
        *output = device::ICameraDeviceCallback::version;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus processCaptureResult(
        const std::vector<device::CaptureResult>& results) override {
        for (const auto& result : results) {
            for (const auto& physical : result.physicalCameraMetadata) {
                physicalIds.push_back(physical.physicalCameraId);
            }
        }
        return ndk::ScopedAStatus::ok();
    }
    std::vector<std::string> physicalIds;
};

class OfflineSession : public device::ICameraOfflineSessionDefault {
   public:
    ndk::ScopedAStatus getInterfaceVersion(int32_t* output) override {
        *output = device::ICameraOfflineSession::version;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getCaptureResultMetadataQueue(MetadataQueueDescriptor* output) override {
        *output = MetadataQueueDescriptor{};
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus setCallback(
        const std::shared_ptr<device::ICameraDeviceCallback>& input) override {
        callback = input;
        return ndk::ScopedAStatus::ok();
    }
    std::shared_ptr<device::ICameraDeviceCallback> callback;
};

class Session : public device::ICameraDeviceSessionDefault {
   public:
    Session() : offline(ndk::SharedRefBase::make<OfflineSession>()) {}
    ndk::ScopedAStatus getInterfaceVersion(int32_t* output) override {
        *output = device::ICameraDeviceSession::version;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus configureStreams(const device::StreamConfiguration& input,
                                        std::vector<device::HalStream>* output) override {
        configuredPhysicalIds.clear();
        output->clear();
        for (const auto& stream : input.streams) {
            configuredPhysicalIds.push_back(stream.physicalCameraId);
            device::HalStream configured;
            configured.id = stream.id;
            configured.physicalCameraId = stream.physicalCameraId;
            output->push_back(configured);
        }
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus configureStreamsV2(const device::StreamConfiguration& input,
                                          device::ConfigureStreamsRet* output) override {
        return configureStreams(input, &output->halStreams);
    }
    ndk::ScopedAStatus processCaptureRequest(const std::vector<device::CaptureRequest>& requests,
                                             const std::vector<device::BufferCache>&,
                                             int32_t* output) override {
        requestedPhysicalIds.clear();
        for (const auto& request : requests) {
            for (const auto& physical : request.physicalCameraSettings) {
                requestedPhysicalIds.push_back(physical.physicalCameraId);
            }
        }
        *output = requests.size();
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus switchToOffline(
        const std::vector<int32_t>&, device::CameraOfflineSessionInfo*,
        std::shared_ptr<device::ICameraOfflineSession>* output) override {
        *output = offline;
        return ndk::ScopedAStatus::ok();
    }
    std::vector<std::string> configuredPhysicalIds, requestedPhysicalIds;
    std::shared_ptr<OfflineSession> offline;
};

class CameraSessionAdapterTest : public testing::Test {
   protected:
    void SetUp() override {
        original = ndk::SharedRefBase::make<Session>();
        callback = ndk::SharedRefBase::make<Callback>();
        wrapped =
            wrapCameraSession(original, ndk::SharedRefBase::make<CameraResultAdapter>(callback));
    }
    std::shared_ptr<Session> original;
    std::shared_ptr<Callback> callback;
    std::shared_ptr<device::ICameraDeviceSession> wrapped;
};

TEST_F(CameraSessionAdapterTest, BothStreamConfigurationVersionsMapPhysicalIdsInBothDirections) {
    device::StreamConfiguration configuration;
    configuration.streams.resize(3);
    configuration.streams[0].physicalCameraId = "3";
    configuration.streams[1].physicalCameraId = "2";
    configuration.streams[2].physicalCameraId = "";
    std::vector<device::HalStream> streams;
    ASSERT_TRUE(wrapped->configureStreams(configuration, &streams).isOk());
    EXPECT_EQ(original->configuredPhysicalIds, (std::vector<std::string>{"1", "2", ""}));
    EXPECT_EQ(streams[0].physicalCameraId, "3");
    EXPECT_EQ(configuration.streams[0].physicalCameraId, "3");
    device::ConfigureStreamsRet configured;
    ASSERT_TRUE(wrapped->configureStreamsV2(configuration, &configured).isOk());
    EXPECT_EQ(configured.halStreams[0].physicalCameraId, "3");
}

TEST_F(CameraSessionAdapterTest, PhysicalCaptureSettingsUseOemIdsAndKeepCallerPackets) {
    std::vector<device::CaptureRequest> requests(1);
    requests[0].physicalCameraSettings.resize(2);
    requests[0].physicalCameraSettings[0].physicalCameraId = "3";
    requests[0].physicalCameraSettings[1].physicalCameraId = "2";
    int32_t processed = 0;
    ASSERT_TRUE(wrapped->processCaptureRequest(requests, {}, &processed).isOk());
    EXPECT_EQ(processed, 1);
    EXPECT_EQ(original->requestedPhysicalIds, (std::vector<std::string>{"1", "2"}));
    EXPECT_EQ(requests[0].physicalCameraSettings[0].physicalCameraId, "3");
}

TEST_F(CameraSessionAdapterTest, OfflineResultsUseNewCallbackAndTranslatePhysicalIdsOnce) {
    std::shared_ptr<device::ICameraOfflineSession> offline;
    device::CameraOfflineSessionInfo info;
    ASSERT_TRUE(wrapped->switchToOffline({}, &info, &offline).isOk());
    MetadataQueueDescriptor queue;
    ASSERT_TRUE(offline->getCaptureResultMetadataQueue(&queue).isOk());
    auto newCallback = ndk::SharedRefBase::make<Callback>();
    ASSERT_TRUE(offline->setCallback(newCallback).isOk());
    ASSERT_NE(original->offline->callback, nullptr);
    std::vector<device::CaptureResult> results(1);
    results[0].physicalCameraMetadata.resize(1);
    results[0].physicalCameraMetadata[0].physicalCameraId = "1";
    ASSERT_TRUE(original->offline->callback->processCaptureResult(results).isOk());
    EXPECT_TRUE(callback->physicalIds.empty());
    EXPECT_EQ(newCallback->physicalIds, (std::vector<std::string>{"3"}));
}

class InjectionSession : public device::ICameraInjectionSessionDefault {
   public:
    explicit InjectionSession(std::shared_ptr<Session> session) : session(std::move(session)) {}
    ndk::ScopedAStatus getInterfaceVersion(int32_t* output) override {
        *output = device::ICameraInjectionSession::version;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus configureInjectionStreams(const device::StreamConfiguration& configuration,
                                                 const device::CameraMetadata& metadata) override {
        for (const auto& stream : configuration.streams)
            physicalIds.push_back(stream.physicalCameraId);
        characteristics = metadata;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus getCameraDeviceSession(
        std::shared_ptr<device::ICameraDeviceSession>* output) override {
        *output = session;
        return ndk::ScopedAStatus::ok();
    }
    const std::shared_ptr<Session> session;
    std::vector<std::string> physicalIds;
    device::CameraMetadata characteristics;
};

TEST_F(CameraSessionAdapterTest, InjectionMapsCharacteristicsAndReturnedSession) {
    auto injection = ndk::SharedRefBase::make<InjectionSession>(original);
    auto wrappedInjection =
        wrapInjectionSession(injection, ndk::SharedRefBase::make<CameraResultAdapter>(callback));
    device::StreamConfiguration configuration;
    configuration.streams.resize(1);
    configuration.streams[0].physicalCameraId = "3";
    auto* metadata = allocate_camera_metadata(2, 32);
    ASSERT_NE(metadata, nullptr);
    const uint8_t physicalIds[] = {'2', '\0', '3', '\0'};
    ASSERT_EQ(add_camera_metadata_entry(metadata, ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                                        physicalIds, sizeof(physicalIds)),
              0);
    device::CameraMetadata characteristics;
    const auto* bytes = reinterpret_cast<const uint8_t*>(metadata);
    characteristics.metadata.assign(bytes, bytes + get_camera_metadata_size(metadata));
    free_camera_metadata(metadata);
    ASSERT_TRUE(wrappedInjection->configureInjectionStreams(configuration, characteristics).isOk());
    EXPECT_EQ(injection->physicalIds, (std::vector<std::string>{"1"}));
    camera_metadata_ro_entry_t entry;
    ASSERT_EQ(find_camera_metadata_ro_entry(reinterpret_cast<const camera_metadata_t*>(
                                                injection->characteristics.metadata.data()),
                                            ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS, &entry),
              0);
    EXPECT_EQ((std::vector<uint8_t>{entry.data.u8, entry.data.u8 + entry.count}),
              (std::vector<uint8_t>{'2', '\0', '1', '\0'}));
    std::shared_ptr<device::ICameraDeviceSession> session;
    ASSERT_TRUE(wrappedInjection->getCameraDeviceSession(&session).isOk());
    ASSERT_NE(session, nullptr);
    std::vector<device::HalStream> streams;
    ASSERT_TRUE(session->configureStreams(configuration, &streams).isOk());
    EXPECT_EQ(original->configuredPhysicalIds, (std::vector<std::string>{"1"}));
    EXPECT_EQ(streams[0].physicalCameraId, "3");
}

}  // namespace
}  // namespace nx809j::camera
