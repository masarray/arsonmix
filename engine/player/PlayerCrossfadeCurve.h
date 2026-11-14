// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Immutable equal-power transition table. Filled exactly once on CONTROL;
// audio callback does bounded integer indexing, no trig, heap or locks.
namespace armix::player {
class PlayerCrossfadeCurve final {
public:
    static constexpr int resolution=1024;
    struct Gains {float oldGain=1.f,newGain=0.f;};
    PlayerCrossfadeCurve() noexcept {
        constexpr double pi=3.14159265358979323846;
        for(int i=0;i<=resolution;++i) {
            const double t=(pi/2)*i/resolution;
            old_[static_cast<std::size_t>(i)]=
                static_cast<float>(std::cos(t));
            next_[static_cast<std::size_t>(i)]=
                static_cast<float>(std::sin(t));
        }
    }
    Gains gains(int position,int total) const noexcept {
        const int index=std::clamp(static_cast<int>(
            (static_cast<std::int64_t>(std::max(0,position))*resolution)/
            std::max(1,total)),0,resolution);
        return {old_[static_cast<std::size_t>(index)],
                next_[static_cast<std::size_t>(index)]};
    }
private:
    std::array<float,resolution+1> old_{},next_{};
};
} // namespace armix::player
