// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

// Read-only native block-peak metering. The Compressor core measures
// pre/post samples in its existing DSP pass; dB conversion is CONTROL-only.
namespace armix::builtin_fx {
inline float peakToDbfs(float peak) noexcept {
    if(!std::isfinite(peak)||peak<=0.f)return -72.f;
    return std::clamp(20.f*std::log10(peak),-72.f,12.f);
}
} // namespace armix::builtin_fx
