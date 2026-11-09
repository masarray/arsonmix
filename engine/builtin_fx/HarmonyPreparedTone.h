// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Control-thread prepared Harmony timbre filter and equal-temperament
// interval ratios. No transcendental function or allocation on AUDIO.
namespace armix::harmony {
class RateStableColor final {
public:
    static constexpr std::size_t segments=256;
    // 48k is the previously approved Color response; all other rates
    // preserve the same one-pole physical-time attenuation.
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        for(std::size_t i=0;i<=segments;++i){
            const double color=static_cast<double>(i)/segments;
            const double previous=.07+.77*color;
            coefficients_[i]=static_cast<float>(
                1.-std::pow(1.-previous,48000./rate));
        }
        return true;
    }
    float coefficient(float color) const noexcept {
        const float position=std::clamp(color,0.f,1.f)*segments;
        const auto index=static_cast<std::size_t>(position);
        const auto next=std::min(index+1,segments);
        const float fraction=position-static_cast<float>(index);
        const float current=coefficients_[index];
        return current+(coefficients_[next]-current)*fraction;
    }
private:
    std::array<float,segments+1> coefficients_{};
};
class PreparedPitchIntervals final {
public:
    static constexpr int maximumSemitones=24;
    void prepare() noexcept {
        for(int delta=-maximumSemitones;delta<=maximumSemitones;++delta)
            ratios_[static_cast<std::size_t>(delta+maximumSemitones)]=
                std::pow(2.f,static_cast<float>(delta)/12.f);
    }
    // Caller guarantees scale-degree intervals remain inside +/-24.
    // Clamping makes an unexpected new music scale safe rather than OOB.
    float ratio(int semitones) const noexcept {
        const int index=std::clamp(semitones,-maximumSemitones,maximumSemitones);
        return ratios_[static_cast<std::size_t>(index+maximumSemitones)];
    }
private:
    std::array<float,2*maximumSemitones+1> ratios_{};
};
} // namespace armix::harmony
