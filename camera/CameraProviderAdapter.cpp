// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "NX809JCameraCompat"

#include "CameraProviderAdapter.h"
#include "PhysicalCameraMetadata.h"

#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <android/binder_ibinder.h>
#include <log/log.h>

#include <algorithm>
#include <utility>

namespace nx809j::camera {
namespace {

namespace device = aidl::android::hardware::camera::device;
namespace provider = aidl::android::hardware::camera::provider;

// Create and attach extensions together. BnCInterface caches its binder weakly,
// so attaching an extension to a temporary asBinder() result would lose it.
ndk::SpAIBinder withOriginalExtension(ndk::SpAIBinder binder, const ndk::SpAIBinder& original) {
    ndk::SpAIBinder extension;
    auto status = AIBinder_getExtension(original.get(), extension.getR());
    if (status == STATUS_OK && extension.get() != nullptr) {
        status = AIBinder_setExtension(binder.get(), extension.get());
    }
    if (status != STATUS_OK) {
        ALOGE("Cannot preserve camera binder extension: %d; retaining original interface", status);
        return original;
    }
    return binder;
}

class CameraDeviceAdapter : public device::ICameraDeviceDelegator {
public:
    CameraDeviceAdapter(std::shared_ptr<device::ICameraDevice> original,
                        std::shared_ptr<provider::ICameraProvider> provider, std::string name)
        : ICameraDeviceDelegator(original), mOriginal(std::move(original)),
          mProvider(std::move(provider)), mName(std::move(name)) {}

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

    ndk::ScopedAStatus getCameraDeviceInterface(
            const std::string& name, std::shared_ptr<device::ICameraDevice>* output) override {
        auto status = mOriginal->getCameraDeviceInterface(name, output);
        if (!status.isOk() || !*output) return status;
        int32_t version = 0;
        if (!(*output)->getInterfaceVersion(&version).isOk() ||
            version != device::ICameraDevice::version) {
            ALOGE("%s: unsupported device interface version %d; retaining original", name.c_str(),
                  version);
            return status;
        }
        *output = ndk::SharedRefBase::make<CameraDeviceAdapter>(*output, mOriginal, name);
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
