// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuNaturalBalanceTable.generated.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::sonku::p101 {
// Identical minimum-phase transfer in both ears. Not a personalized HRTF,
// not a room inverse or loudness normalization. All coefficients at prepare.
class NaturalBalance final {
    struct C {float b0=1,b1=0,b2=0,a1=0,a2=0;};
    struct S {float z1=0,z2=0;};
    static constexpr std::array<double,5> hz{194,560,1250,2800,6300};
    static constexpr std::array<double,5> q{.55,.65,.65,.75,.70};
    std::array<C,5> coeff_{};
    std::array<std::array<S,5>,2> state_{};
    float strength_=0,step_=0;
    static float run(C c,S& s,float x) noexcept {
        const float y=c.b0*x+s.z1;
        s.z1=c.b1*x-c.a1*y+s.z2;s.z2=c.b2*x-c.a2*y;
        if(std::abs(s.z1)<1.e-25f)s.z1=0;
        if(std::abs(s.z2)<1.e-25f)s.z2=0;
        return y;
    }
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||(std::abs(rate-44100.)>.1&&std::abs(rate-48000.)>.1))return false;
        constexpr double pi=3.14159265358979323846;
        for(unsigned i=0;i<hz.size();++i){
            const double db=calibratedGainDb[i];
            if(!std::isfinite(db)||db< -2.5||db>(i>=3?1.0:2.5))return false;
            const double w=2*pi*hz[i]/rate, a=std::sin(w)/(2*q[i]), A=std::pow(10.,db/40.), d=1+a/A;
            coeff_[i]={float((1+a*A)/d),float(-2*std::cos(w)/d),float((1-a*A)/d),float(-2*std::cos(w)/d),float((1-a/A)/d)};
        }
        step_=float(1.-std::exp(-1./(.05*rate)));reset();return true;
    }
    void reset() noexcept {state_={};strength_=0;}
    void process(float& l,float& r,float mix) noexcept {
        const float target=std::clamp(mix,0.f,1.f);
        strength_+=step_*(target-strength_);
        if(std::abs(strength_-target)<1.e-6f)strength_=target;
        float a=l,b=r;
        for(unsigned i=0;i<coeff_.size();++i){a=run(coeff_[i],state_[0][i],a);b=run(coeff_[i],state_[1][i],b);}
        l+=strength_*(a-l);r+=strength_*(b-r);
    }
};
}
