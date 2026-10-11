// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/camera/device/BnCameraDeviceCallback.h>
#include <fmq/AidlMessageQueue.h>

#include <functional>
#include <mutex>

namespace nx809j::camera {

using MetadataQueue = android::AidlMessageQueue<int8_t, android::SynchronizedReadWrite>;
using MetadataQueueDescriptor = android::MQDescriptor<int8_t, android::SynchronizedReadWrite>;

// Owns the HAL-side result reader and a separate framework-side result writer.
// This avoids changing shared packets while another process is reading them.
class CameraResultAdapter : public aidl::android::hardware::camera::device::BnCameraDeviceCallback {
   public:
    explicit CameraResultAdapter(
        std::shared_ptr<aidl::android::hardware::camera::device::ICameraDeviceCallback> original);

    ndk::ScopedAStatus getResultQueue(
        const std::function<ndk::ScopedAStatus(MetadataQueueDescriptor*)>& getOriginalQueue,
        MetadataQueueDescriptor* output);
    ndk::ScopedAStatus processCaptureResult(
        const std::vector<aidl::android::hardware::camera::device::CaptureResult>& results)
        override;
    ndk::ScopedAStatus notify(
        const std::vector<aidl::android::hardware::camera::device::NotifyMsg>& messages) override;
    ndk::ScopedAStatus requestStreamBuffers(
        const std::vector<aidl::android::hardware::camera::device::BufferRequest>& requests,
        std::vector<aidl::android::hardware::camera::device::StreamBufferRet>* buffers,
        aidl::android::hardware::camera::device::BufferRequestStatus* output) override;
    ndk::ScopedAStatus returnStreamBuffers(
        const std::vector<aidl::android::hardware::camera::device::StreamBuffer>& buffers) override;
    binder_status_t dump(int fd, const char** args, uint32_t count) override;
    std::shared_ptr<aidl::android::hardware::camera::device::ICameraDeviceCallback> destination()
        const {
        return mOriginal;
    }

   protected:
    ndk::SpAIBinder createBinder() override;

   private:
    bool translateMetadata(int64_t* fmqSize,
                           aidl::android::hardware::camera::device::CameraMetadata* metadata);

    const std::shared_ptr<aidl::android::hardware::camera::device::ICameraDeviceCallback> mOriginal;
    std::mutex mResultLock;
    bool mQueueInitialized = false;
    std::unique_ptr<MetadataQueue> mOriginalQueue;
    std::unique_ptr<MetadataQueue> mResultQueue;
};

}  // namespace nx809j::camera
