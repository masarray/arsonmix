// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

// ImerSip-specific concert width from *already rendered* P8.6 directional
// early wall reflections. No synthetic delay, decorrelator, new reverb,
// extra HRTF, new audio bus, FFT, phase inversion, or worker. The existing
// stereo direct front speakers and original bass are the authorities.
//
// Two-pole 250 Hz highpass + two-pole 3800 Hz lowpass isolate the
// true early-lateral musical presence band. Sub-bass and the P8.7
// measured-direct air band are strongly suppressed.
// The enhancement is MID-orthogonal: additional left == -additional right.
// Therefore mono fold-down, center vocal pressure and original dry remain
// exactly unchanged by THIS stage. This is a private audition candidate.
namespace armix::imersip {
class ConcertStage final {
public:
    struct Ears {float left=0.f,right=0.f;};
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate) ||
           (std::abs(rate-44100.)>.1 && std::abs(rate-48000.)>.1))
            return false;
        constexpr double twoPi=6.2831853071795864769;
        lowerAlpha_=static_cast<float>(1.-std::exp(-twoPi*250./rate));
        upperAlpha_=static_cast<float>(1.-std::exp(-twoPi*3800./rate));
        slew_=static_cast<float>(1.-std::exp(-1./(rate*.025)));
        reset();
        return true;
    }
    void setScene(float immersion,float room,float width) noexcept {
        // Called only with validated ImerSip settings on the native
        // control path or SPSC audio edit. Affine, bounded, no exp/pow.
        // Real P8.6 ER energy was only ~0.004 RMS of source even with the
        // first 70% lift. A calibrated 4x lateral *reflected* component,
        // NOT 4x master/dry gain, gives a measurable musical room presence
        // without reviving the ceiling/floor or late FDN wash.
        target_=4.0f*immersion*room*width;
    }
    void reset() noexcept {
        low250First_=low250Second_=0.f;
        low3800First_=low3800Second_=0.f;
        gain_=target_;
    }
    Ears process(float measuredEarlyL,float measuredEarlyR) noexcept {
        const float side=.5f*(measuredEarlyL-measuredEarlyR);
        low250First_+=lowerAlpha_*(side-low250First_);
        const float hp1=side-low250First_;
        low250Second_+=lowerAlpha_*(hp1-low250Second_);
        const float hp2=hp1-low250Second_;
        low3800First_+=upperAlpha_*(hp2-low3800First_);
        low3800Second_+=upperAlpha_*(low3800First_-low3800Second_);
        gain_+=slew_*(target_-gain_);
        const float lateral=gain_*low3800Second_;
        return {lateral,-lateral};
    }
    float targetGain() const noexcept{return target_;}
private:
    float lowerAlpha_=0.f,upperAlpha_=0.f,slew_=0.f;
    float low250First_=0.f,low250Second_=0.f;
    float low3800First_=0.f,low3800Second_=0.f;
    float gain_=0.f,target_=0.f;
};
} // namespace armix::imersip
