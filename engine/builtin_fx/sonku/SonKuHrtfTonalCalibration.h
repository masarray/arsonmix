// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "SonKuMeasuredHrir.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// P8.1: direction-INDEPENDENT tonal equalization calibrated from the actual
// measured KEMAR horizontal bank. Applied equally to both ears, and only to
// the direct HRTF source, never SIDE, DRY, ER or diffuse late reverb.
// All analysis/trigonometry/pow is performed ONCE in prepare(), off RT.
// A pair of bounded minimum-phase biquads adds no device/audio-block latency.
// This research-only profile is not a substitute for a proprietary HRTF model
// and never claims dearVR's Clarity algorithm or individualized HRTF.
namespace armix::sonku {
class SonKuHrtfTonalCalibration final {
public:
    using Bank=const std::int16_t (*)[2][hrir::taps];
    bool prepare(double rate,Bank bank) noexcept {
        if(!bank || !std::isfinite(rate) || rate<40000. || rate>50000.)return false;
        constexpr double eps=1.e-8;
        // Horizontal geometric mean of measured ear response; NOT a measured
        // head-related diffuse-field transfer function or a fitted room IR.
        const double low=averageMagnitude(bank,rate,1000.);
        const double middle=averageMagnitude(bank,rate,2000.);
        const double upper=averageMagnitude(bank,rate,4000.);
        const double treble=averageMagnitude(bank,rate,8000.);
        if(low<eps||middle<eps||upper<eps||treble<eps)return false;
        const double adjacent=std::sqrt(low*upper);
        // Correct ONLY the broad measured 2k resonance, capped at 5.5 dB
        // to retain the source-localization HRTF's pinna features.
        presenceDb_=static_cast<float>(std::clamp(
            -20.*std::log10(middle/adjacent),-5.5,0.));
        // Never invert 8k pinna notches fully: modest broad air return.
        airDb_=static_cast<float>(std::clamp(
            .40*20.*std::log10(low/treble),0.,4.));
        presence_=peaking(rate,2200.,.85,presenceDb_);
        air_=highShelf(rate,6200.,airDb_);
        reset();
        return true;
    }
    void reset() noexcept {
        for(auto& x:presenceMemory_)x={};
        for(auto& x:airMemory_)x={};
    }
    void process(float& left,float& right) noexcept {
        left=run(presence_,presenceMemory_[0],left);
        right=run(presence_,presenceMemory_[1],right);
        left=run(air_,airMemory_[0],left);
        right=run(air_,airMemory_[1],right);
    }
    float presenceCutDb() const noexcept{return presenceDb_;}
    float highAirDb() const noexcept{return airDb_;}
private:
    struct Coeff {
        float b0=1.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f;
    };
    struct State{float z1=0.f,z2=0.f;};
    static float run(const Coeff& c,State& s,float x) noexcept {
        const float out=c.b0*x+s.z1;
        s.z1=c.b1*x-c.a1*out+s.z2;
        s.z2=c.b2*x-c.a2*out;
        return out;
    }
    static Coeff normalized(double b0,double b1,double b2,
                            double a0,double a1,double a2) noexcept {
        if(!std::isfinite(a0)||a0<1.e-9)return {};
        const double inverse=1./a0;
        return {static_cast<float>(b0*inverse),
                static_cast<float>(b1*inverse),
                static_cast<float>(b2*inverse),
                static_cast<float>(a1*inverse),
                static_cast<float>(a2*inverse)};
    }
    static Coeff peaking(double sr,double hz,double q,double db) noexcept {
        constexpr double pi=3.14159265358979323846;
        const double omega=2.*pi*hz/sr;
        const double cosw=std::cos(omega),alpha=std::sin(omega)/(2.*q);
        const double A=std::pow(10.,db/40.);
        return normalized(1.+alpha*A,-2.*cosw,1.-alpha*A,
                          1.+alpha/A,-2.*cosw,1.-alpha/A);
    }
    static Coeff highShelf(double sr,double hz,double db) noexcept {
        constexpr double pi=3.14159265358979323846;
        const double omega=2.*pi*hz/sr;
        const double cosw=std::cos(omega);
        const double A=std::pow(10.,db/40.);
        const double root=std::sqrt(A);
        // RBJ high shelf, S=1. Shared common transfer on both ears.
        const double slope=2.*root*(std::sin(omega)*.5*std::sqrt(2.));
        return normalized(A*((A+1.)+(A-1.)*cosw+slope),
                          -2.*A*((A-1.)+(A+1.)*cosw),
                          A*((A+1.)+(A-1.)*cosw-slope),
                          (A+1.)-(A-1.)*cosw+slope,
                          2.*((A-1.)-(A+1.)*cosw),
                          (A+1.)-(A-1.)*cosw-slope);
    }
    static double averageMagnitude(Bank bank,double rate,
                                   double hz) noexcept {
        constexpr double pi=3.14159265358979323846;
        const double w=2.*pi*hz/rate;
        double sumLog=0.;
        for(int direction=0;direction<hrir::directions;++direction)
            for(int ear=0;ear<2;++ear) {
                double real=0.,imag=0.;
                for(int k=0;k<hrir::taps;++k){
                    const double p=w*k;
                    const double x=static_cast<double>(bank[direction][ear][k]);
                    real+=x*std::cos(p);
                    imag-=x*std::sin(p);
                }
                const double magnitude=std::hypot(real,imag)/32768.;
                sumLog+=std::log(std::max(magnitude,1.e-9));
            }
        return std::exp(sumLog/(2.*hrir::directions));
    }
    Coeff presence_{},air_{};
    std::array<State,2> presenceMemory_{},airMemory_{};
    float presenceDb_=0.f,airDb_=0.f;
};
} // namespace armix::sonku
