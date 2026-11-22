// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cmath>
namespace armix::sonku::p100 {
// One stationary, broad wet-MID bell. No detector, lookahead, gain
// compensation or new delay. SIDE is unchanged algebraically.
// This modifies the diffuse FDN RETURN, not a vocal stem or source HRIR.
class LyricFrontPocket final {
public:
    struct Stereo {float left,right;};
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate) || (std::abs(rate-44100.)>.1 && std::abs(rate-48000.)>.1))return false;
        constexpr double pi=3.14159265358979323846;
        // Constant-peak bandpass, Q=.60: broad -2 dB at 1.8kHz.
        const double w=2.*pi*1800./rate, a=std::sin(w)/(2.*.60),inv=1./(1.+a);
        b_=float(a*inv);a1_=float(-2.*std::cos(w)*inv);a2_=float((1.-a)*inv);
        reset();return true;
    }
    void reset() noexcept {z1_=z2_=0.f;}
    Stereo process(float l,float r) noexcept {
        const float m=.5f*(l+r),y=b_*m+z1_;
        z1_=-a1_*y+z2_;z2_=-b_*m-a2_*y;
        // Flush inaudible state without touching the input.
        if(std::abs(z1_)<1.e-25f)z1_=0.f;
        if(std::abs(z2_)<1.e-25f)z2_=0.f;
        const float d=.20567176f*y; // 1 - 10^(-2/20)
        return {l-d,r-d};
    }
private:
    float b_=0.f,a1_=0.f,a2_=0.f,z1_=0.f,z2_=0.f;
};
}
