// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "NX809JCameraCompat"

#include "CameraSessionAdapter.h"

#include <aidl/android/hardware/camera/device/BnCameraDeviceCallback.h>
#include <aidl/android/hardware/camera/device/BnCameraDeviceSession.h>
#include <aidl/android/hardware/camera/device/BnCameraInjectionSession.h>
#include <aidl/android/hardware/camera/device/BnCameraOfflineSession.h>

#include "BinderExtensions.h"
#include "CameraIdMapping.h"
#include "CameraParcelCopy.h"

namespace nx809j::camera {
namespace {

namespace device = aidl::android::hardware::camera::device;

void mapHalStreams(std::vector<device::HalStream>* streams) {
    for (auto& stream : *streams) stream.physicalCameraId = mapCameraId(stream.physicalCameraId);
}

class CameraOfflineAdapter : public device::ICameraOfflineSessionDelegator {
   public:
    explicit CameraOfflineAdapter(std::shared_ptr<device::ICameraOfflineSession> original)
        : ICameraOfflineSessionDelegator(original), mOriginal(std::move(original)) {}

    ndk::ScopedAStatus getCaptureResultMetadataQueue(MetadataQueueDescriptor* output) override {
        return mCallback->getResultQueue(
            [&](auto* descriptor) { return mOriginal->getCaptureResultMetadataQueue(descriptor); },
            output);
    }

    ndk::ScopedAStatus setCallback(
        const std::shared_ptr<device::ICameraDeviceCallback>& callback) override {
        if (!callback) return mOriginal->setCallback(callback);
        // The framework obtains the offline queue before installing its callback.
        // Keep that queue while replacing the callback's destination.
        mRelay->setDestination(callback);
        return mOriginal->setCallback(mCallback);
    }

    // Queue setup precedes setCallback. This relay supplies a stable destination
    // binder while the framework switches from the online to the offline session.
    class CallbackRelay : public device::BnCameraDeviceCallback {
       public:
        explicit CallbackRelay(std::shared_ptr<device::ICameraDeviceCallback> destination)
            : mDestination(std::move(destination)) {}
        void setDestination(std::shared_ptr<device::ICameraDeviceCallback> destination) {
            std::lock_guard<std::mutex> lock(mLock);
            mDestination = std::move(destination);
        }
        ndk::ScopedAStatus notify(const std::vector<device::NotifyMsg>& messages) override {
            return destination()->notify(messages);
        }
        ndk::ScopedAStatus processCaptureResult(
            const std::vector<device::CaptureResult>& results) override {
            return destination()->processCaptureResult(results);
        }
        ndk::ScopedAStatus requestStreamBuffers(const std::vector<device::BufferRequest>& requests,
                                                std::vector<device::StreamBufferRet>* buffers,
                                                device::BufferRequestStatus* output) override {
            return destination()->requestStreamBuffers(requests, buffers, output);
        }
        ndk::ScopedAStatus returnStreamBuffers(
            const std::vector<device::StreamBuffer>& buffers) override {
            return destination()->returnStreamBuffers(buffers);
        }
        binder_status_t dump(int fd, const char** args, uint32_t count) override {
            return AIBinder_dump(destination()->asBinder().get(), fd, args, count);
        }

       protected:
        ndk::SpAIBinder createBinder() override {
            return withOriginalExtension(BnCameraDeviceCallback::createBinder(),
                                         destination()->asBinder());
        }

       private:
        std::shared_ptr<device::ICameraDeviceCallback> destination() {
            std::lock_guard<std::mutex> lock(mLock);
            return mDestination;
        }
        std::mutex mLock;
        std::shared_ptr<device::ICameraDeviceCallback> mDestination;
    };

    void initializeCallback(const std::shared_ptr<device::ICameraDeviceCallback>& destination) {
        mRelay = ndk::SharedRefBase::make<CallbackRelay>(destination);
        mCallback = ndk::SharedRefBase::make<CameraResultAdapter>(mRelay);
    }

    binder_status_t dump(int fd, const char** args, uint32_t count) override {
        return AIBinder_dump(mOriginal->asBinder().get(), fd, args, count);
    }

   protected:
    ndk::SpAIBinder createBinder() override {
        return withOriginalExtension(BnCameraOfflineSession::createBinder(), mOriginal->asBinder());
    }

   private:
    const std::shared_ptr<device::ICameraOfflineSession> mOriginal;
    std::shared_ptr<CallbackRelay> mRelay;
    std::shared_ptr<CameraResultAdapter> mCallback;
};

class CameraSessionAdapter : public device::ICameraDeviceSessionDelegator {
   public:
    CameraSessionAdapter(std::shared_ptr<device::ICameraDeviceSession> original,
                         std::shared_ptr<CameraResultAdapter> callback)
        : ICameraDeviceSessionDelegator(original),
          mOriginal(std::move(original)),
          mCallback(std::move(callback)) {}

    ndk::ScopedAStatus configureStreams(const device::StreamConfiguration& configuration,
                                        std::vector<device::HalStream>* output) override {
        auto status = mOriginal->configureStreams(mapStreamConfiguration(configuration), output);
        if (status.isOk()) mapHalStreams(output);
        return status;
    }

    ndk::ScopedAStatus configureStreamsV2(const device::StreamConfiguration& configuration,
                                          device::ConfigureStreamsRet* output) override {
        auto status = mOriginal->configureStreamsV2(mapStreamConfiguration(configuration), output);
        if (status.isOk()) mapHalStreams(&output->halStreams);
        return status;
    }

    ndk::ScopedAStatus getCaptureResultMetadataQueue(MetadataQueueDescriptor* output) override {
        return mCallback->getResultQueue(
            [&](auto* descriptor) { return mOriginal->getCaptureResultMetadataQueue(descriptor); },
            output);
    }

    ndk::ScopedAStatus processCaptureRequest(const std::vector<device::CaptureRequest>& requests,
                                             const std::vector<device::BufferCache>& caches,
                                             int32_t* output) override {
        std::vector<device::CaptureRequest> mapped;
        const auto copyStatus = copyCameraPackets(requests, &mapped);
        if (copyStatus != STATUS_OK) return ndk::ScopedAStatus::fromStatus(copyStatus);
        for (auto& request : mapped) {
            for (auto& physical : request.physicalCameraSettings) {
                physical.physicalCameraId = mapCameraId(physical.physicalCameraId);
            }
        }
        return mOriginal->processCaptureRequest(mapped, caches, output);
    }

    ndk::ScopedAStatus switchToOffline(
        const std::vector<int32_t>& streams, device::CameraOfflineSessionInfo* info,
        std::shared_ptr<device::ICameraOfflineSession>* output) override {
        auto status = mOriginal->switchToOffline(streams, info, output);
        if (status.isOk() && *output) {
            auto wrapped = ndk::SharedRefBase::make<CameraOfflineAdapter>(*output);
            wrapped->initializeCallback(mCallback->destination());
            *output = wrapped;
        }
        return status;
    }

   protected:
    ndk::SpAIBinder createBinder() override {
        return withOriginalExtension(BnCameraDeviceSession::createBinder(), mOriginal->asBinder());
    }

    binder_status_t dump(int fd, const char** args, uint32_t count) override {
        return AIBinder_dump(mOriginal->asBinder().get(), fd, args, count);
    }

   private:
    const std::shared_ptr<device::ICameraDeviceSession> mOriginal;
    const std::shared_ptr<CameraResultAdapter> mCallback;
};

class CameraInjectionAdapter : public device::ICameraInjectionSessionDelegator {
   public:
    CameraInjectionAdapter(std::shared_ptr<device::ICameraInjectionSession> original,
                           std::shared_ptr<CameraResultAdapter> callback)
        : ICameraInjectionSessionDelegator(original),
          mOriginal(std::move(original)),
          mCallback(std::move(callback)) {}

    ndk::ScopedAStatus configureInjectionStreams(
        const device::StreamConfiguration& configuration,
        const device::CameraMetadata& characteristics) override {
        auto mapped = characteristics;
        mapMetadataCameraIds(&mapped.metadata);
        return mOriginal->configureInjectionStreams(mapStreamConfiguration(configuration), mapped);
    }

    ndk::ScopedAStatus getCameraDeviceSession(
        std::shared_ptr<device::ICameraDeviceSession>* output) override {
        auto status = mOriginal->getCameraDeviceSession(output);
        if (status.isOk() && *output) *output = wrapCameraSession(*output, mCallback);
        return status;
    }

    binder_status_t dump(int fd, const char** args, uint32_t count) override {
        return AIBinder_dump(mOriginal->asBinder().get(), fd, args, count);
    }

   protected:
    ndk::SpAIBinder createBinder() override {
        return withOriginalExtension(BnCameraInjectionSession::createBinder(),
                                     mOriginal->asBinder());
    }

   private:
    const std::shared_ptr<device::ICameraInjectionSession> mOriginal;
    const std::shared_ptr<CameraResultAdapter> mCallback;
};

}  // namespace

std::shared_ptr<device::ICameraDeviceSession> wrapCameraSession(
    const std::shared_ptr<device::ICameraDeviceSession>& original,
    const std::shared_ptr<CameraResultAdapter>& callback) {
    return ndk::SharedRefBase::make<CameraSessionAdapter>(original, callback);
}

std::shared_ptr<device::ICameraInjectionSession> wrapInjectionSession(
    const std::shared_ptr<device::ICameraInjectionSession>& original,
    const std::shared_ptr<CameraResultAdapter>& callback) {
    return ndk::SharedRefBase::make<CameraInjectionAdapter>(original, callback);
}

}  // namespace nx809j::camera
