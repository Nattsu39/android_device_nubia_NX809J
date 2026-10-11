// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/camera/device/ICameraDeviceSession.h>
#include <aidl/android/hardware/camera/device/ICameraInjectionSession.h>

#include "CameraResultAdapter.h"

namespace nx809j::camera {

std::shared_ptr<aidl::android::hardware::camera::device::ICameraDeviceSession> wrapCameraSession(
    const std::shared_ptr<aidl::android::hardware::camera::device::ICameraDeviceSession>& original,
    const std::shared_ptr<CameraResultAdapter>& callback);
std::shared_ptr<aidl::android::hardware::camera::device::ICameraInjectionSession>
wrapInjectionSession(
    const std::shared_ptr<aidl::android::hardware::camera::device::ICameraInjectionSession>&
        original,
    const std::shared_ptr<CameraResultAdapter>& callback);

}  // namespace nx809j::camera
