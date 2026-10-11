// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#include <aidl/android/hardware/camera/device/BnCameraDeviceCallback.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <system/camera_metadata.h>
#include <unistd.h>

#include "CameraIdMapping.h"
#include "CameraResultAdapter.h"

namespace nx809j::camera {
namespace {

namespace device = aidl::android::hardware::camera::device;

device::CameraMetadata activeMetadata(const std::string& id) {
    auto* metadata = allocate_camera_metadata(4, 64);
    EXPECT_NE(metadata, nullptr);
    EXPECT_EQ(add_camera_metadata_entry(metadata, ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID,
                                        id.c_str(), id.size() + 1),
              0);
    device::CameraMetadata result;
    const auto* bytes = reinterpret_cast<const uint8_t*>(metadata);
    result.metadata.assign(bytes, bytes + get_camera_metadata_size(metadata));
    free_camera_metadata(metadata);
    return result;
}

std::string activeId(const std::vector<uint8_t>& bytes) {
    camera_metadata_ro_entry_t entry;
    const auto* metadata = reinterpret_cast<const camera_metadata_t*>(bytes.data());
    if (bytes.empty() ||
        find_camera_metadata_ro_entry(metadata, ANDROID_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID,
                                      &entry) != 0) {
        return {};
    }
    return reinterpret_cast<const char*>(entry.data.u8);
}

class ResultReceiver : public device::BnCameraDeviceCallback {
   public:
    std::unique_ptr<MetadataQueue> queue;
    std::vector<std::string> activeIds, physicalIds;
    std::vector<int64_t> queueSizes;
    int calls = 0;
    bool validBufferFd = false;

    ndk::ScopedAStatus processCaptureResult(
        const std::vector<device::CaptureResult>& results) override {
        ++calls;
        for (const auto& result : results) {
            receive(result.fmqResultSize, result.result);
            for (const auto& physical : result.physicalCameraMetadata) {
                physicalIds.push_back(physical.physicalCameraId);
                receive(physical.fmqMetadataSize, physical.metadata);
            }
            if (!result.outputBuffers.empty() && !result.outputBuffers[0].buffer.fds.empty()) {
                validBufferFd = fcntl(result.outputBuffers[0].buffer.fds[0].get(), F_GETFD) >= 0;
            }
        }
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus notify(const std::vector<device::NotifyMsg>&) override {
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus requestStreamBuffers(const std::vector<device::BufferRequest>&,
                                            std::vector<device::StreamBufferRet>*,
                                            device::BufferRequestStatus* output) override {
        *output = device::BufferRequestStatus::FAILED_UNKNOWN;
        return ndk::ScopedAStatus::ok();
    }
    ndk::ScopedAStatus returnStreamBuffers(const std::vector<device::StreamBuffer>&) override {
        return ndk::ScopedAStatus::ok();
    }

   private:
    void receive(int64_t size, const device::CameraMetadata& metadata) {
        queueSizes.push_back(size);
        auto bytes = metadata.metadata;
        if (size > 0) {
            EXPECT_NE(queue, nullptr);
            bytes.resize(size);
            EXPECT_TRUE(queue->read(reinterpret_cast<int8_t*>(bytes.data()), size));
        }
        activeIds.push_back(activeId(bytes));
    }
};

class CameraResultAdapterTest : public testing::Test {
   protected:
    void SetUp() override {
        receiver = ndk::SharedRefBase::make<ResultReceiver>();
        adapter = ndk::SharedRefBase::make<CameraResultAdapter>(receiver);
        producer = std::make_unique<MetadataQueue>(4096);
        ASSERT_TRUE(producer->isValid());
        MetadataQueueDescriptor descriptor;
        ASSERT_TRUE(adapter
                        ->getResultQueue(
                            [&](auto* original) {
                                *original = producer->dupeDesc();
                                return ndk::ScopedAStatus::ok();
                            },
                            &descriptor)
                        .isOk());
        receiver->queue = std::make_unique<MetadataQueue>(descriptor, false);
        ASSERT_TRUE(receiver->queue->isValid());
    }

    device::CaptureResult makeQueuedResult(const std::string& id) {
        device::CaptureResult result;
        const auto metadata = activeMetadata(id);
        result.frameNumber = 27;
        result.fmqResultSize = metadata.metadata.size();
        EXPECT_TRUE(producer->write(reinterpret_cast<const int8_t*>(metadata.metadata.data()),
                                    metadata.metadata.size()));
        return result;
    }

    std::shared_ptr<ResultReceiver> receiver;
    std::shared_ptr<CameraResultAdapter> adapter;
    std::unique_ptr<MetadataQueue> producer;
};

TEST(CameraIdMappingTest, SwapIsReversibleAndKeepsOtherNames) {
    for (const auto& id :
         {"", "0", "1", "2", "3", "10", "31", "hidden", "device@1.1/vendor_qti/3"}) {
        EXPECT_EQ(mapCameraId(mapCameraId(id)), id);
    }
    EXPECT_EQ(mapCameraId("3"), "1");
    EXPECT_EQ(mapCameraId("1"), "3");
    EXPECT_EQ(mapCameraId("10"), "10");
    EXPECT_EQ(mapCameraId("device@1.1/vendor_qti/3"), "device@1.1/vendor_qti/1");
    EXPECT_EQ(mapCameraId("device@1.1/external/3"), "device@1.1/external/3");
}

TEST(CameraIdMappingTest, MalformedMetadataRemainsUnchanged) {
    std::vector<uint8_t> metadata{1, 3, 0, 0, 5};
    const auto original = metadata;
    mapMetadataCameraIds(&metadata);
    EXPECT_EQ(metadata, original);
}

TEST_F(CameraResultAdapterTest, InlineResultsMapPhysicalIdsAndPreserveBufferOwnership) {
    std::vector<device::CaptureResult> results(1);
    results[0].result = activeMetadata("3");
    results[0].physicalCameraMetadata.resize(1);
    results[0].physicalCameraMetadata[0].physicalCameraId = "1";
    results[0].physicalCameraMetadata[0].metadata = activeMetadata("1");
    results[0].outputBuffers.resize(1);
    results[0].outputBuffers[0].buffer.fds.emplace_back(open("/dev/null", O_RDONLY | O_CLOEXEC));
    const int originalFd = results[0].outputBuffers[0].buffer.fds[0].get();
    ASSERT_GE(originalFd, 0);
    ASSERT_TRUE(adapter->processCaptureResult(results).isOk());
    EXPECT_EQ(receiver->activeIds, (std::vector<std::string>{"1", "3"}));
    EXPECT_EQ(receiver->physicalIds, (std::vector<std::string>{"3"}));
    EXPECT_EQ(activeId(results[0].result.metadata), "3");
    EXPECT_TRUE(receiver->validBufferFd);
    EXPECT_GE(fcntl(originalFd, F_GETFD), 0);
}

TEST_F(CameraResultAdapterTest, QueuedResultsTranslateAcrossBothRingBufferBoundaries) {
    std::vector<int8_t> padding(4070, 0);
    ASSERT_TRUE(producer->write(padding.data(), padding.size()));
    ASSERT_TRUE(producer->read(padding.data(), padding.size()));
    ASSERT_TRUE(receiver->queue->write(padding.data(), padding.size()));
    ASSERT_TRUE(receiver->queue->read(padding.data(), padding.size()));
    std::vector<device::CaptureResult> results;
    results.push_back(makeQueuedResult("1"));
    results.push_back(makeQueuedResult("3"));
    const auto physical = activeMetadata("1");
    ASSERT_TRUE(producer->write(reinterpret_cast<const int8_t*>(physical.metadata.data()),
                                physical.metadata.size()));
    results[1].physicalCameraMetadata.resize(1);
    results[1].physicalCameraMetadata[0].physicalCameraId = "1";
    results[1].physicalCameraMetadata[0].fmqMetadataSize = physical.metadata.size();
    ASSERT_TRUE(adapter->processCaptureResult(results).isOk());
    EXPECT_EQ(receiver->activeIds, (std::vector<std::string>{"3", "1", "3"}));
    EXPECT_EQ(receiver->physicalIds, (std::vector<std::string>{"3"}));
    for (auto size : receiver->queueSizes) EXPECT_GT(size, 0);
    EXPECT_EQ(producer->availableToRead(), 0u);
    EXPECT_EQ(receiver->queue->availableToRead(), 0u);
}

TEST_F(CameraResultAdapterTest, FullFrameworkQueueFallsBackToInlineMetadata) {
    std::vector<int8_t> padding(4096, 0);
    ASSERT_TRUE(receiver->queue->write(padding.data(), padding.size()));
    std::vector<device::CaptureResult> results;
    results.push_back(makeQueuedResult("3"));
    ASSERT_TRUE(adapter->processCaptureResult(results).isOk());
    EXPECT_EQ(receiver->activeIds, (std::vector<std::string>{"1"}));
    EXPECT_EQ(receiver->queueSizes, (std::vector<int64_t>{0}));
    EXPECT_EQ(producer->availableToRead(), 0u);
    EXPECT_EQ(receiver->queue->availableToRead(), 4096u);
}

TEST_F(CameraResultAdapterTest, RepeatedQueueLookupKeepsUnreadPackets) {
    std::vector<int8_t> packet(100, 7);
    ASSERT_TRUE(receiver->queue->write(packet.data(), packet.size()));
    MetadataQueueDescriptor descriptor;
    ASSERT_TRUE(adapter
                    ->getResultQueue(
                        [](auto*) {
                            ADD_FAILURE() << "The HAL result queue must only be attached once";
                            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
                        },
                        &descriptor)
                    .isOk());
    MetadataQueue reader(descriptor, false);
    EXPECT_EQ(reader.availableToRead(), packet.size());
}

TEST_F(CameraResultAdapterTest, InvalidQueueLengthStopsBeforeConsumingOrDeliveringResults) {
    std::vector<device::CaptureResult> results;
    results.push_back(makeQueuedResult("3"));
    const auto queued = producer->availableToRead();
    results[0].fmqResultSize = 4097;
    EXPECT_FALSE(adapter->processCaptureResult(results).isOk());
    EXPECT_EQ(producer->availableToRead(), queued);
    EXPECT_EQ(receiver->calls, 0);
}

}  // namespace
}  // namespace nx809j::camera
