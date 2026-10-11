// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "NX809JCameraCompat"

#include "CameraProviderAdapter.h"

#include <android/binder_manager.h>
#include <dlfcn.h>
#include <log/log.h>

#include <cstring>

// The preload is scoped to the OEM camera provider by its ODM init service.
// Leave its ZTE transfer, AON and offline-camera registrations untouched.
extern "C" __attribute__((visibility("default"))) binder_exception_t AServiceManager_addService(
        AIBinder* binder, const char* instance) {
    using AddService = binder_exception_t (*)(AIBinder*, const char*);
    static const auto addService =
            reinterpret_cast<AddService>(dlsym(RTLD_NEXT, "AServiceManager_addService"));
    if (addService == nullptr) {
        ALOGE("Cannot resolve original AServiceManager_addService: %s", dlerror());
        return EX_ILLEGAL_STATE;
    }
    if (instance == nullptr ||
        std::strcmp(instance, "android.hardware.camera.provider.ICameraProvider/vendor_qti/0") != 0) {
        return addService(binder, instance);
    }

    AIBinder_incStrong(binder);
    const auto original = aidl::android::hardware::camera::provider::ICameraProvider::fromBinder(
            ndk::SpAIBinder(binder));
    const auto wrapped = nx809j::camera::wrapCameraProvider(original);
    if (!wrapped || wrapped == original) return addService(binder, instance);
    const auto wrappedBinder = wrapped->asBinder();
    const auto result = addService(wrappedBinder.get(), instance);
    if (result == EX_NONE) ALOGI("Registered device-tree camera metadata adapter for %s", instance);
    return result;
}
