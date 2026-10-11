// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "NX809JCameraCompat"

#include "CameraResultAdapter.h"

#include <aidl/android/hardware/camera/device/BnCameraDeviceCallback.h>

#include "BinderExtensions.h"
#include "CameraIdMapping.h"
#include "CameraParcelCopy.h"

namespace nx809j::camera {

namespace device = aidl::android::hardware::camera::device;

CameraResultAdapter::CameraResultAdapter(std::shared_ptr<device::ICameraDeviceCallback> original)
    : mOriginal(std::move(original)) {}

ndk::ScopedAStatus CameraResultAdapter::getResultQueue(
    const std::function<ndk::ScopedAStatus(MetadataQueueDescriptor*)>& getOriginalQueue,
    MetadataQueueDescriptor* output) {
    std::lock_guard<std::mutex> lock(mResultLock);
    if (!mQueueInitialized) {
        MetadataQueueDescriptor descriptor;
        auto status = getOriginalQueue(&descriptor);
        if (!status.isOk()) return status;
        if (!descriptor.grantors.empty()) {
            mOriginalQueue = std::make_unique<MetadataQueue>(descriptor, false);
            if (!mOriginalQueue->isValid()) {
                mOriginalQueue.reset();
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
            mResultQueue = std::make_unique<MetadataQueue>(mOriginalQueue->getQuantumCount());
            if (!mResultQueue->isValid()) {
                // The HAL queue can still be consumed; deliver results inline.
                mResultQueue.reset();
            }
        }
        mQueueInitialized = true;
    }
    *output = mResultQueue ? mResultQueue->dupeDesc() : MetadataQueueDescriptor{};
    return ndk::ScopedAStatus::ok();
}

bool CameraResultAdapter::translateMetadata(int64_t* fmqSize, device::CameraMetadata* metadata) {
    const bool fromQueue = *fmqSize > 0;
    if (*fmqSize < 0) return false;
    if (fromQueue) {
        if (!mOriginalQueue ||
            static_cast<uint64_t>(*fmqSize) > mOriginalQueue->availableToRead()) {
            return false;
        }
        metadata->metadata.resize(*fmqSize);
        if (!mOriginalQueue->read(reinterpret_cast<int8_t*>(metadata->metadata.data()), *fmqSize)) {
            return false;
        }
    }
    mapMetadataCameraIds(&metadata->metadata);
    if (fromQueue && mResultQueue &&
        mResultQueue->write(reinterpret_cast<const int8_t*>(metadata->metadata.data()), *fmqSize)) {
        metadata->metadata.clear();
    } else {
        *fmqSize = 0;
    }
    return true;
}

ndk::ScopedAStatus CameraResultAdapter::processCaptureResult(
    const std::vector<device::CaptureResult>& results) {
    std::lock_guard<std::mutex> lock(mResultLock);
    std::vector<device::CaptureResult> mapped;
    const auto copyStatus = copyCameraPackets(results, &mapped);
    if (copyStatus != STATUS_OK) return ndk::ScopedAStatus::fromStatus(copyStatus);
    for (auto& result : mapped) {
        if (!translateMetadata(&result.fmqResultSize, &result.result)) {
            ALOGE("Cannot read result metadata for frame %d", result.frameNumber);
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
        for (auto& physical : result.physicalCameraMetadata) {
            physical.physicalCameraId = mapCameraId(physical.physicalCameraId);
            if (!translateMetadata(&physical.fmqMetadataSize, &physical.metadata)) {
                ALOGE("Cannot read physical result metadata for frame %d", result.frameNumber);
                return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
            }
        }
    }
    return mOriginal->processCaptureResult(mapped);
}

ndk::SpAIBinder CameraResultAdapter::createBinder() {
    return withOriginalExtension(BnCameraDeviceCallback::createBinder(), mOriginal->asBinder());
}

ndk::ScopedAStatus CameraResultAdapter::notify(const std::vector<device::NotifyMsg>& messages) {
    return mOriginal->notify(messages);
}

ndk::ScopedAStatus CameraResultAdapter::requestStreamBuffers(
    const std::vector<device::BufferRequest>& requests,
    std::vector<device::StreamBufferRet>* buffers, device::BufferRequestStatus* output) {
    return mOriginal->requestStreamBuffers(requests, buffers, output);
}

ndk::ScopedAStatus CameraResultAdapter::returnStreamBuffers(
    const std::vector<device::StreamBuffer>& buffers) {
    return mOriginal->returnStreamBuffers(buffers);
}

binder_status_t CameraResultAdapter::dump(int fd, const char** args, uint32_t count) {
    return AIBinder_dump(mOriginal->asBinder().get(), fd, args, count);
}

}  // namespace nx809j::camera
