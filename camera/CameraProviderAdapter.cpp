// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "NX809JCameraCompat"

#include "CameraProviderAdapter.h"

#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <aidl/android/hardware/camera/provider/BnCameraProviderCallback.h>
#include <android/binder_ibinder.h>
#include <log/log.h>

#include <algorithm>
#include <utility>

#include "BinderExtensions.h"
#include "CameraIdMapping.h"
#include "CameraSessionAdapter.h"
#include "PhysicalCameraMetadata.h"

namespace nx809j::camera {
namespace {

namespace device = aidl::android::hardware::camera::device;
namespace provider = aidl::android::hardware::camera::provider;

namespace common = aidl::android::hardware::camera::common;

class CameraProviderCallbackAdapter : public provider::BnCameraProviderCallback {
   public:
    explicit CameraProviderCallbackAdapter(
        std::shared_ptr<provider::ICameraProviderCallback> original)
        : mOriginal(std::move(original)) {}

    ndk::ScopedAStatus cameraDeviceStatusChange(const std::string& name,
                                                common::CameraDeviceStatus status) override {
        return mOriginal->cameraDeviceStatusChange(mapCameraId(name), status);
    }
    ndk::ScopedAStatus torchModeStatusChange(const std::string& name,
                                             common::TorchModeStatus status) override {
        return mOriginal->torchModeStatusChange(mapCameraId(name), status);
    }
    ndk::ScopedAStatus physicalCameraDeviceStatusChange(
        const std::string& name, const std::string& physical,
        common::CameraDeviceStatus status) override {
        // The front camera is no longer advertised as a physical member of the
        // rear logical camera. Do not emit status for that excluded member.
        if (name.substr(name.find_last_of('/') + 1) == "0" &&
            physical.substr(physical.find_last_of('/') + 1) == "3") {
            return ndk::ScopedAStatus::ok();
        }
        return mOriginal->physicalCameraDeviceStatusChange(mapCameraId(name), mapCameraId(physical),
                                                           status);
    }

    binder_status_t dump(int fd, const char** args, uint32_t count) override {
        return AIBinder_dump(mOriginal->asBinder().get(), fd, args, count);
    }

   protected:
    ndk::SpAIBinder createBinder() override {
        return withOriginalExtension(BnCameraProviderCallback::createBinder(),
                                     mOriginal->asBinder());
    }

   private:
    const std::shared_ptr<provider::ICameraProviderCallback> mOriginal;
};

class CameraDeviceAdapter : public device::ICameraDeviceDelegator {
   public:
    CameraDeviceAdapter(std::shared_ptr<device::ICameraDevice> original,
                        std::shared_ptr<provider::ICameraProvider> provider, std::string name)
        : ICameraDeviceDelegator(original),
          mOriginal(std::move(original)),
          mProvider(std::move(provider)),
          mName(std::move(name)) {}

    ndk::ScopedAStatus getCameraCharacteristics(device::CameraMetadata* output) override {
        auto status = mOriginal->getCameraCharacteristics(output);
        if (!status.isOk()) return status;

        std::vector<std::string> publicNames;
        if (!mProvider->getCameraIdList(&publicNames).isOk()) return status;
        const auto separator = mName.rfind('/');
        if (separator == std::string::npos) return status;
        const auto prefix = mName.substr(0, separator + 1);
        const FacingResolver facing = [&](const std::string& id) -> std::optional<uint8_t> {
            const auto name = prefix + id;
            if (std::find(publicNames.begin(), publicNames.end(), name) == publicNames.end()) {
                return std::nullopt;
            }
            std::shared_ptr<device::ICameraDevice> physical;
            device::CameraMetadata characteristics;
            if (!mProvider->getCameraDeviceInterface(name, &physical).isOk() || !physical ||
                !physical->getCameraCharacteristics(&characteristics).isOk()) {
                return std::nullopt;
            }
            return getCameraFacing(characteristics.metadata);
        };
        if (filterPhysicalCameraIds(&output->metadata, facing)) {
            ALOGI("%s: excluded opposite-facing public camera from logical physical IDs",
                  mName.c_str());
        }
        mapMetadataCameraIds(&output->metadata);
        return status;
    }

    ndk::ScopedAStatus getPhysicalCameraCharacteristics(const std::string& id,
                                                        device::CameraMetadata* output) override {
        auto status = mOriginal->getPhysicalCameraCharacteristics(mapCameraId(id), output);
        if (status.isOk()) mapMetadataCameraIds(&output->metadata);
        return status;
    }

    ndk::ScopedAStatus getResourceCost(common::CameraResourceCost* output) override {
        auto status = mOriginal->getResourceCost(output);
        if (status.isOk()) {
            for (auto& name : output->conflictingDevices) name = mapCameraId(name);
        }
        return status;
    }

    ndk::ScopedAStatus isStreamCombinationSupported(
        const device::StreamConfiguration& configuration, bool* output) override {
        return mOriginal->isStreamCombinationSupported(mapStreamConfiguration(configuration),
                                                       output);
    }

    ndk::ScopedAStatus isStreamCombinationWithSettingsSupported(
        const device::StreamConfiguration& configuration, bool* output) override {
        return mOriginal->isStreamCombinationWithSettingsSupported(
            mapStreamConfiguration(configuration), output);
    }

    ndk::ScopedAStatus getSessionCharacteristics(const device::StreamConfiguration& configuration,
                                                 device::CameraMetadata* output) override {
        auto status =
            mOriginal->getSessionCharacteristics(mapStreamConfiguration(configuration), output);
        if (status.isOk()) mapMetadataCameraIds(&output->metadata);
        return status;
    }

    ndk::ScopedAStatus open(const std::shared_ptr<device::ICameraDeviceCallback>& callback,
                            std::shared_ptr<device::ICameraDeviceSession>* output) override {
        if (!callback) return mOriginal->open(callback, output);
        auto wrappedCallback = ndk::SharedRefBase::make<CameraResultAdapter>(callback);
        auto status = mOriginal->open(wrappedCallback, output);
        if (status.isOk() && *output) *output = wrapCameraSession(*output, wrappedCallback);
        return status;
    }

    ndk::ScopedAStatus openInjectionSession(
        const std::shared_ptr<device::ICameraDeviceCallback>& callback,
        std::shared_ptr<device::ICameraInjectionSession>* output) override {
        if (!callback) return mOriginal->openInjectionSession(callback, output);
        auto wrappedCallback = ndk::SharedRefBase::make<CameraResultAdapter>(callback);
        auto status = mOriginal->openInjectionSession(wrappedCallback, output);
        if (status.isOk() && *output) *output = wrapInjectionSession(*output, wrappedCallback);
        return status;
    }

    binder_status_t dump(int fd, const char** args, uint32_t count) override {
        return AIBinder_dump(mOriginal->asBinder().get(), fd, args, count);
    }

   protected:
    ndk::SpAIBinder createBinder() override {
        return withOriginalExtension(BnCameraDevice::createBinder(), mOriginal->asBinder());
    }

   private:
    const std::shared_ptr<device::ICameraDevice> mOriginal;
    const std::shared_ptr<provider::ICameraProvider> mProvider;
    const std::string mName;
};

class CameraProviderAdapter : public provider::ICameraProviderDelegator {
   public:
    explicit CameraProviderAdapter(std::shared_ptr<provider::ICameraProvider> original)
        : ICameraProviderDelegator(original), mOriginal(std::move(original)) {}

    ndk::ScopedAStatus setCallback(
        const std::shared_ptr<provider::ICameraProviderCallback>& callback) override {
        return mOriginal->setCallback(
            callback ? ndk::SharedRefBase::make<CameraProviderCallbackAdapter>(callback) : nullptr);
    }

    ndk::ScopedAStatus getCameraIdList(std::vector<std::string>* output) override {
        auto status = mOriginal->getCameraIdList(output);
        if (status.isOk()) {
            for (auto& name : *output) name = mapCameraId(name);
            std::sort(output->begin(), output->end());
        }
        return status;
    }

    ndk::ScopedAStatus getConcurrentCameraIds(
        std::vector<provider::ConcurrentCameraIdCombination>* output) override {
        auto status = mOriginal->getConcurrentCameraIds(output);
        if (status.isOk()) {
            for (auto& group : *output) {
                for (auto& id : group.combination) id = mapCameraId(id);
            }
        }
        return status;
    }

    ndk::ScopedAStatus isConcurrentStreamCombinationSupported(
        const std::vector<provider::CameraIdAndStreamCombination>& configurations,
        bool* output) override {
        auto mapped = configurations;
        for (auto& configuration : mapped) {
            configuration.cameraId = mapCameraId(configuration.cameraId);
            configuration.streamConfiguration =
                mapStreamConfiguration(configuration.streamConfiguration);
        }
        return mOriginal->isConcurrentStreamCombinationSupported(mapped, output);
    }

    ndk::ScopedAStatus getCameraDeviceInterface(
        const std::string& name, std::shared_ptr<device::ICameraDevice>* output) override {
        const auto originalName = mapCameraId(name);
        auto status = mOriginal->getCameraDeviceInterface(originalName, output);
        if (!status.isOk() || !*output) return status;
        int32_t version = 0;
        if (!(*output)->getInterfaceVersion(&version).isOk() ||
            version != device::ICameraDevice::version) {
            ALOGE("%s: unsupported device interface version %d; retaining original", name.c_str(),
                  version);
            return status;
        }
        *output = ndk::SharedRefBase::make<CameraDeviceAdapter>(*output, mOriginal, originalName);
        return status;
    }

    binder_status_t dump(int fd, const char** args, uint32_t count) override {
        return AIBinder_dump(mOriginal->asBinder().get(), fd, args, count);
    }

   protected:
    ndk::SpAIBinder createBinder() override {
        return withOriginalExtension(BnCameraProvider::createBinder(), mOriginal->asBinder());
    }

   private:
    const std::shared_ptr<provider::ICameraProvider> mOriginal;
};

}  // namespace

std::shared_ptr<provider::ICameraProvider> wrapCameraProvider(
    const std::shared_ptr<provider::ICameraProvider>& original) {
    int32_t version = 0;
    if (!original || !original->getInterfaceVersion(&version).isOk() ||
        version != provider::ICameraProvider::version) {
        ALOGE("Unsupported provider interface version %d; retaining original", version);
        return original;
    }
    return ndk::SharedRefBase::make<CameraProviderAdapter>(original);
}

}  // namespace nx809j::camera
