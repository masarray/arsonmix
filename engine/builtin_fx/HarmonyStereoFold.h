// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

// Callback-owned analysis ONLY, not an audible stereo output mixer.
// Recover music/vocal guide PCM when opposite-polarity stereo cancels L+R.
// Normal correlated stereo remains the exact legacy sum; near-opposition
// needs two-threshold hysteresis and a smoothed phase-aware sign fold.
// Constant-work, fixed-state, lock/heap-free, zero added sample latency.
namespace armix::harmony {
class StereoPhaseSafeFold final {
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        evidenceAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.020)));
        blendAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.015)));
        reset();
        return true;
    }
    void reset() noexcept {
        energyL_=0.f;energyR_=0.f;cross_=0.f;amount_=0.f;
        antiCorrelated_=false;
    }
    float process(float left,float right) noexcept {
        // Both callers sanitize nonfinite source PCM. Limit only correlation
        // evidence amplitude, never change a valid dry stereo source.
        const float l=std::clamp(left,-4.f,4.f);
        const float r=std::clamp(right,-4.f,4.f);
        energyL_+=evidenceAlpha_*(l*l-energyL_);
        energyR_+=evidenceAlpha_*(r*r-energyR_);
        cross_+=evidenceAlpha_*(l*r-cross_);
        const float power=.5f*(energyL_+energyR_);
        if(power<1.e-8f)antiCorrelated_=false;
        else if(antiCorrelated_){
            if(cross_>-.55f*power)antiCorrelated_=false;
        }else if(cross_<-.78f*power)antiCorrelated_=true;
        amount_+=blendAlpha_*((antiCorrelated_?1.f:0.f)-amount_);
        // amount=0 -> original 0.5*(L+R);
        // amount=1 -> recovered 0.5*(L-R).
        return .5f*(left+right)-amount_*right;
    }
    float rescueAmount() const noexcept{return amount_;}
private:
    float evidenceAlpha_=0.f,blendAlpha_=0.f;
    float energyL_=0.f,energyR_=0.f,cross_=0.f,amount_=0.f;
    bool antiCorrelated_=false;
};
} // namespace armix::harmony
