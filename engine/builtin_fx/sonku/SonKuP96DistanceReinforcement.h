// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Private P9.6 sound-reinforcement hypothesis, downstream of the SAME measured
// two-loudspeaker HRIR and world-anchored LF handover. Split only DIRECT ear
// pressure, not late/early reflections, the original L/R music or dry side.
// The 170 Hz / 4.2 kHz complementary crossovers are causal one-poles; their
// components sum to the exact unfiltered ear signal at equal gains.
// No allocations, FFT, virtual mono sub, added latency or callback trig.
namespace armix::sonku::p96 {
class DistanceReinforcement final {
public:
    struct Ears {float left=0.f,right=0.f;};
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate) ||
           (std::abs(rate-44100.)>.1 && std::abs(rate-48000.)>.1))return false;
        constexpr double pi=3.14159265358979323846;
        lowAlpha_=static_cast<float>(1.-std::exp(-2.*pi*170./rate));
        highAlpha_=static_cast<float>(1.-std::exp(-2.*pi*4200./rate));
        depthAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.035)));
        reset();
        return true;
    }
    void reset() noexcept {low_.fill(0.f);wide_.fill(0.f);depth_=0.f;}
    bool active() const noexcept {return depth_!=0.f;}
    Ears process(float left,float right,float distanceGain,
                 float distanceMeters,bool roomEnabled) noexcept {
        // Preserve OFF and <=2m P9.5 PCM exactly. The state filters still
        // warm every sample, preventing stale memory on a live distance edit.
        const float target=roomEnabled?
            std::clamp((distanceMeters-2.f)/8.f,0.f,1.f):0.f;
        depth_+=depthAlpha_*(target-depth_);
        if(std::abs(depth_-target)<1.e-6f)depth_=target;
        const std::array<float,2> ears{left,right};
        std::array<float,2> out{};
        for(int c=0;c<2;++c){
            low_[c]+=lowAlpha_*(ears[c]-low_[c]);
            wide_[c]+=highAlpha_*(ears[c]-wide_[c]);
        }
        if(depth_==0.f)
            return {distanceGain*left,distanceGain*right};
        // The normal distance cue remains in the mid-band and in the native
        // early/late DRR. Reinforced speaker direct LF and HF retain musical
        // body/air instead of making 10m sound like only a midrange reverb.
        // This is NOT a physical free-field SPL model or a loudness guarantee.
        const float reinforcible=std::max(0.f,1.f-distanceGain)*depth_;
        const float midGain=distanceGain+.24f*reinforcible;
        const float lowLift=.52f*reinforcible;  // total LF retention 0.76
        const float highLift=.43f*reinforcible; // total HF retention 0.67
        for(int c=0;c<2;++c)
            out[c]=midGain*ears[c]+lowLift*low_[c]+highLift*(ears[c]-wide_[c]);
        return {out[0],out[1]};
    }
private:
    std::array<float,2> low_{},wide_{};
    float lowAlpha_=0.f,highAlpha_=0.f,depthAlpha_=0.f,depth_=0.f;
};
} // namespace armix::sonku::p96
