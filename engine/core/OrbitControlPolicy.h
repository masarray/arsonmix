// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
namespace armix::core {
inline float circularDistance(float a,float b) noexcept {
    if(!std::isfinite(a)||!std::isfinite(b)||a<0.f||b<0.f||a>1.f||b>1.f)
        return std::numeric_limits<float>::infinity();
    const auto diff=std::abs(a-b);return std::min(diff,1.f-diff);
}
inline bool shouldPublishOrbit(float value,float previous,int steps) noexcept {
    if(!std::isfinite(value)||value<0.f||value>1.f)return false;
    if(!std::isfinite(previous))return true;
    const auto tolerance=steps>=72&&steps<1000000 ?
        std::min(.005f,.45f/static_cast<float>(steps)):.00001f;
    return circularDistance(value,previous)>tolerance;
}
struct OrbitDispatchStats {
    std::uint64_t ticks=0,writes=0,coalesced=0,lateTicks=0;
    double maxGapMs=0.0;
    void observe(double ms) noexcept {
        if(!std::isfinite(ms)||ms<0.)return;
        maxGapMs=std::max(maxGapMs,ms);
        if(ms>50.0)++lateTicks; // NOT a hardware xrun
    }
};
}
