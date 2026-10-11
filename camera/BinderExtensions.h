// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <android/binder_ibinder.h>
#include <android/binder_interface_utils.h>
#include <log/log.h>

namespace nx809j::camera {

// Attach extensions while creating the binder: BnCInterface caches it weakly.
// Attaching an extension to a temporary asBinder() result would lose it.
inline ndk::SpAIBinder withOriginalExtension(ndk::SpAIBinder binder,
                                             const ndk::SpAIBinder& original) {
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

}  // namespace nx809j::camera
