// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nx809j::camera {

using FacingResolver = std::function<std::optional<uint8_t>(const std::string&)>;

std::optional<uint8_t> getCameraFacing(const std::vector<uint8_t>& metadata);

// Keep hidden physical cameras whose facing cannot be queried. Only an
// independently exposed camera with a different facing is removed from the
// logical camera's public physical-ID list.
bool filterPhysicalCameraIds(std::vector<uint8_t>* metadata, const FacingResolver& resolveFacing);

}  // namespace nx809j::camera
