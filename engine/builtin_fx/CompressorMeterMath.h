// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

// Read-only native block-peak metering. Does not influence PCM or introduce
// lookahead. Logarithmic conversion runs ONLY on the message/UI thread.
namespace armix::builtin_fx {
inline float stereoBlockPeak(const float* left,const float* right,
                             std::size_t frames) noexcept {
    if(!left||!right||!frames)return 0.f;
    float peak=0.f;
    for(std::size_t i=0;i<frames;++i) {
        const float l=std::abs(left[i]),r=std::abs(right[i]);
        if(std::isfinite(l))peak=std::max(peak,l);
        if(std::isfinite(r))peak=std::max(peak,r);
    }
    return peak;
}
inline float peakToDbfs(float peak) noexcept {
    if(!std::isfinite(peak)||peak<=0.f)return -72.f;
    return std::clamp(20.f*std::log10(peak),-72.f,12.f);
}
} // namespace armix::builtin_fx
