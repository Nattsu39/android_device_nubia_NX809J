// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/android/hardware/camera/provider/ICameraProvider.h>

namespace nx809j::camera {

std::shared_ptr<aidl::android::hardware::camera::provider::ICameraProvider>
wrapCameraProvider(const std::shared_ptr<aidl::android::hardware::camera::provider::ICameraProvider>&
                           provider);

}  // namespace nx809j::camera
