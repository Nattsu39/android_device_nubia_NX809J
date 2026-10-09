/* Copyright (C) 2026 The Avium Project
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace nx809j::als {

using LcdParams = std::array<int32_t, 6>;

// The caller holds its mutex. There is at most one pending sample and one in-flight call.
class LatestLcdQueue {
  public:
    struct Sample {
        uint64_t generation;
        LcdParams params;
    };

    void setActive(bool active) {
        if (mActive == active) return;
        mActive = active;
        ++mGeneration;
        mLatest.reset();
    }

    bool submit(const LcdParams& params) {
        if (!mActive) return false;
        mLatest = Sample{mGeneration, params};
        return true;
    }

    std::optional<Sample> latest() const { return mLatest; }
    bool isCurrent(const Sample& sample) const {
        return mActive && sample.generation == mGeneration;
    }
    bool active() const { return mActive; }

  private:
    bool mActive = false;
    uint64_t mGeneration = 0;
    std::optional<Sample> mLatest;
};

inline bool validParams(const LcdParams& p) {
    const int base = p[5] / 10;
    const int acl = p[5] % 10;
    return p[0] >= 0 && p[0] <= 255 && p[1] >= 0 && p[1] <= 255
            && p[2] >= 0 && p[2] <= 255 && p[3] >= 0 && p[3] <= 65535
            && p[4] >= -1 && p[4] <= 150
            && (base == 1 || base == 2 || base == 3 || base == 7) && acl >= 0 && acl <= 3;
}

}  // namespace nx809j::als
