// Copyright (C) 2026 The Avium Project
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <android/binder_auto_utils.h>
#include <android/binder_parcel.h>

#include <vector>

namespace nx809j::camera {

// Capture packets contain move-only file descriptors. A parcel round trip
// duplicates their handles using Binder's normal ownership rules.
template <typename T>
binder_status_t copyCameraPackets(const std::vector<T>& input, std::vector<T>* output) {
    output->clear();
    output->reserve(input.size());
    for (const auto& packet : input) {
        ndk::ScopedAParcel parcel(AParcel_create());
        if (!parcel.get()) return STATUS_NO_MEMORY;
        auto status = packet.writeToParcel(parcel.get());
        if (status != STATUS_OK) return status;
        status = AParcel_setDataPosition(parcel.get(), 0);
        if (status != STATUS_OK) return status;
        T copy;
        status = copy.readFromParcel(parcel.get());
        if (status != STATUS_OK) return status;
        output->push_back(std::move(copy));
    }
    return STATUS_OK;
}

}  // namespace nx809j::camera
