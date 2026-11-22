// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::sonku::p102 {
// P102.2: retain the P102.1 warmth and add screenshot-inspired separation,
// presence and air. Fixed PA acoustic renderer is untouched.
// Replace ONLY the 193Hz bell with 250Hz +5.5 Q.85 (simulated against P102).
// The original screenshot-derived 3240Hz shelf and the other two bands stay.
// Articulation 1120Hz and local treble relief near 7.4k are approximations
// of the visible graph, not claimed exact readback. No flat-target fit.
class FixedPaMusicalEq final {
    struct C{double b0=1,b1=0,b2=0,a1=0,a2=0;};
    struct S{double z1=0,z2=0;};
    std::array<C,7> c_{};
    std::array<std::array<S,7>,2> state_{};
    float mix_=0,step_=0,release_=0,safety_=1;
    static C norm(double b0,double b1,double b2,double a0,double a1,double a2) noexcept {
        return {b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
    }
    static C bell(double sr,double hz,double q,double db) noexcept {
        const double w=6.283185307179586*hz/sr,A=std::pow(10.,db/40.),a=std::sin(w)/(2*q),co=std::cos(w);
        return norm(1+a*A,-2*co,1-a*A,1+a/A,-2*co,1-a/A);
    }
    static C shelf(double sr,double hz,double db) noexcept {
        const double w=6.283185307179586*hz/sr,A=std::pow(10.,db/40.),co=std::cos(w),k=std::sqrt(2*A)*std::sin(w);
        return norm(A*((A+1)+(A-1)*co+k),-2*A*((A-1)+(A+1)*co),A*((A+1)+(A-1)*co-k),
            (A+1)-(A-1)*co+k,2*((A-1)-(A+1)*co),(A+1)-(A-1)*co-k);
    }
    static double run(const C& c,S& s,double x) noexcept {
        const double y=c.b0*x+s.z1;
        s.z1=c.b1*x-c.a1*y+s.z2;s.z2=c.b2*x-c.a2*y;
        if(std::abs(s.z1)<1.e-25)s.z1=0;
        if(std::abs(s.z2)<1.e-25)s.z2=0;
        return y;
    }
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||(std::abs(rate-44100.)>.1&&std::abs(rate-48000.)>.1))return false;
        // Preserve all four P102.1 filters, then add three common L/R bells.
        // 4460Hz/+1.8/Q.77 is the owner's exact displayed PEQ setting.
        // 300Hz/-2.5/Q2.2 and 10kHz/+3/Q2 are bounded estimates of the graph.
        c_={bell(rate,250.,.85,5.5),bell(rate,1120.,1.2,2.3),
            shelf(rate,3240.,3.3),bell(rate,7400.,1.1,-1.8),
            bell(rate,300.,2.2,-2.5),bell(rate,4460.,.77,1.8),
            bell(rate,10000.,2.,3.)};
        step_=float(1.-std::exp(-1./(.025*rate)));
        release_=float(1.-std::exp(-1./(.08*rate)));reset();return true;
    }
    void reset() noexcept {state_={};mix_=0;safety_=1;}
    void process(float& l,float& r,float mix) noexcept {
        const float t=std::clamp(mix,0.f,1.f);
        mix_+=step_*(t-mix_);
        if(std::abs(mix_-t)<1.e-6f)mix_=t;
        double a=l,b=r;
        for(unsigned i=0;i<c_.size();++i){a=run(c_[i],state_[0][i],a);b=run(c_[i],state_[1][i],b);}
        l+=mix_*(float(a)-l);r+=mix_*(float(b)-r);
        // Post-EQ headroom only when needed: shared gain, no permanent pad.
        const float peak=std::max(std::abs(l),std::abs(r));
        safety_=std::min(peak>.98f?.98f/peak:1.f,safety_+release_);
        l*=safety_;r*=safety_;
    }
};
}
