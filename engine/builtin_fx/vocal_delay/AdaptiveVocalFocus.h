// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace armix::builtin_fx::vocal_delay {
// AUDIO-owned causal detector. No lookahead, allocations, classifier or
// changes to the dry signal. Fixed absolute floor avoids tracking silence;
// relative peak reference decays slowly so quieter singers still duck.
// This is an activity heuristic, NOT isolation of voice from backing/noise.
class AdaptiveVocalFocus final {
public:
    void prepare(double rate) noexcept{
        fastAttack_=coefficient(rate,5);fastRelease_=coefficient(rate,30);
        referenceRelease_=coefficient(rate,1800);
        holdLength_=static_cast<std::uint32_t>(std::ceil(rate*.035));
        reset();
    }
    void reset() noexcept{fast_=reference_=activity_=0;hold_=0;}
    float tick(float peak,float attack,float release) noexcept{
        fast_=clean(fast_+(peak-fast_)*(peak>fast_?fastAttack_:fastRelease_));
        reference_=std::max(fast_,clean(reference_+(fast_-reference_)*referenceRelease_));
        const float lower=std::max(.005f,.10f*reference_);
        const float upper=std::max(.025f,.50f*reference_);
        const float x=std::clamp((fast_-lower)/(upper-lower),0.f,1.f);
        const float goal=x*x*(3.f-2.f*x);
        if(goal>=activity_){
            if(goal>.05f)hold_=holdLength_;
            activity_=clean(activity_+(goal-activity_)*attack);
        }else if(hold_>0)--hold_;
        else activity_=clean(activity_+(goal-activity_)*release);
        return activity_;
    }
private:
    static float coefficient(double rate,double ms) noexcept{return float(-std::expm1(-1./(rate*ms*.001)));}
    static float clean(float v) noexcept{return v<1.e-20f?0.f:v;}
    float fastAttack_=0,fastRelease_=0,referenceRelease_=0,fast_=0,reference_=0,activity_=0;
    std::uint32_t holdLength_=0,hold_=0;
};
} // namespace
