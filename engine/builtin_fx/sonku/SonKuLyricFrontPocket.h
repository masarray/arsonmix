// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>

// P100 listening candidate: replace ONLY P99's adaptive late-center relief.
// Equal subtraction from both ear returns preserves their differential SIDE.
// Never infer vocal stems, alter HRIR, filter direct/early or touch FDN feedback.
namespace armix::sonku::p100 {
class LyricFrontPocket final {
public:
    struct Stereo { float left=0.f,right=0.f; };
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate) ||
           (std::abs(rate-44100.)>.1 && std::abs(rate-48000.)>.1))return false;
        constexpr double pi=3.14159265358979323846;
        const double w=2.*pi*1800./rate;
        const double a=std::sin(w)/(2.*.65);
        b_=static_cast<float>(a/(1.+a));
        a1_=static_cast<float>(-2.*std::cos(w)/(1.+a));
        a2_=static_cast<float>((1.-a)/(1.+a));
        slew_=static_cast<float>(1.-std::exp(-1./(.055*rate)));
        reset();return true;
    }
    void reset() noexcept {z1_=z2_=gain_=0.f;}
    Stereo process(float l,float r,bool enabled) noexcept {
        // Scene-only 55ms interpolation, no music-driven coefficients.
        constexpr float depth=0.205671765f; // 1-10^(-2/20)
        const float target=enabled?depth:0.f;
        gain_+=slew_*(target-gain_);
        if(std::abs(target-gain_)<1.e-6f)gain_=target;
        if(gain_==0.f && !enabled){reset();return {l,r};}
        const float mid=.5f*(l+r);
        const float y=b_*mid+z1_;
        z1_=-a1_*y+z2_;
        z2_=-b_*mid-a2_*y;
        // Avoid prolonged subnormal IIR states on silence.
        if(std::abs(z1_)<1.e-24f)z1_=0.f;
        if(std::abs(z2_)<1.e-24f)z2_=0.f;
        const float delta=gain_*y;
        return {l-delta,r-delta};
    }
private:
    float b_=0.f,a1_=0.f,a2_=0.f,slew_=0.f,z1_=0.f,z2_=0.f,gain_=0.f;
};
}
