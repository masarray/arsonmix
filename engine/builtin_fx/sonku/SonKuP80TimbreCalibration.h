// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// P8.0: experimental direction-INDEPENDENT KEMAR spectral reference.
// Prepare off the audio callback; one identical EQ for both ears preserves
// interaural level/time differences. This is NOT Sennheiser Clarity,
// headphone-specific inverse EQ or a claim of dearVR sonic parity.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace armix::sonku::p80 {
class MeasuredTimbreCalibration final {
public:
    static constexpr float maxLowShelfDb=4.5f;
    static constexpr float referenceLowHz=125.f;
    static constexpr float referenceMidHz=1000.f;

    // Bank: int16 FIR [direction][ear][tap], identical to the SonKu
    // source kernel. Coefficients are CONTROL-prepared, not live-sampled.
    template<std::size_t Taps>
    bool prepare(double rate,const std::int16_t (*bank)[2][Taps],
                 int directionCount,int spacingDegrees) noexcept {
        ready_=false;
        if(!bank || (std::abs(rate-44100.)>.1 && std::abs(rate-48000.)>.1) ||
           directionCount<2 || spacingDegrees<=0)return false;
        double powerLow=0.,powerMid=0.;
        int count=0;
        // Reference positions: front, quarter turn and rear. Collect
        // both ears to avoid direction-dependent channel gain/ILD shifts.
        for(int deg:{0,90,180}) {
            const int index=std::clamp(deg/spacingDegrees,0,directionCount-1);
            for(int ear=0;ear<2;++ear) {
                powerLow+=powerAt<Taps>(bank[index][ear],rate,referenceLowHz);
                powerMid+=powerAt<Taps>(bank[index][ear],rate,referenceMidHz);
                ++count;
            }
        }
        if(count!=6 || powerLow<1.e-12 || powerMid<1.e-12 ||
           !std::isfinite(powerLow) || !std::isfinite(powerMid))return false;
        const double spectralRatioDb=10.*std::log10(powerMid/powerLow);
        // Never guess a +10 dB boost from a HRTF notch. A bounded
        // low-shelf may improve monitoring without losing safety headroom.
        gainDb_=static_cast<float>(std::clamp(spectralRatioDb,0.,
                    static_cast<double>(maxLowShelfDb)));
        measuredLowMinusMidDb_=-static_cast<float>(spectralRatioDb);
        if(gainDb_>1.e-4f){
            if(!prepareShelf(rate,180.,gainDb_))return false;
        }else{
            b0_=1.;b1_=b2_=a1_=a2_=0.;
        }
        reset();
        ready_=true;
        return true;
    }

    void reset() noexcept {for(auto& z:memory_)z.fill(0.);}
    bool ready() const noexcept{return ready_;}
    float shelfGainDb() const noexcept{return gainDb_;}
    float measuredLowMinusMidDb() const noexcept{
        return measuredLowMinusMidDb_;
    }
    void process(float& left,float& right) noexcept {
        if(!ready_)return;
        left=step(left,memory_[0]);
        right=step(right,memory_[1]);
    }

private:
    template<std::size_t Taps>
    static double powerAt(const std::int16_t (&fir)[Taps],
                          double rate,double frequency) noexcept{
        const double w=6.2831853071795864769*frequency/rate;
        double re=0.,im=0.;
        for(std::size_t i=0;i<Taps;++i){
            const double phase=w*static_cast<double>(i);
            re+=static_cast<double>(fir[i])*std::cos(phase);
            im-=static_cast<double>(fir[i])*std::sin(phase);
        }
        return re*re+im*im;
    }
    bool prepareShelf(double rate,double cutoff,double db) noexcept {
        // RBJ Audio EQ Cookbook low shelf, slope S=1. Same stable
        // minimum-phase coefficients on both ears: no differential delay.
        constexpr double pi=3.14159265358979323846;
        const double A=std::pow(10.,db/40.);
        const double w=2.*pi*cutoff/rate;
        const double sn=std::sin(w),cs=std::cos(w);
        const double alpha=sn/std::sqrt(2.);
        const double beta=2.*std::sqrt(A)*alpha;
        const double den=(A+1.)+(A-1.)*cs+beta;
        if(!std::isfinite(den)||den<=0.)return false;
        b0_=A*((A+1.)-(A-1.)*cs+beta)/den;
        b1_=2.*A*((A-1.)-(A+1.)*cs)/den;
        b2_=A*((A+1.)-(A-1.)*cs-beta)/den;
        a1_=-2.*((A-1.)+(A+1.)*cs)/den;
        a2_=((A+1.)+(A-1.)*cs-beta)/den;
        return std::isfinite(b0_)&&std::isfinite(b1_)&&
               std::isfinite(b2_)&&std::isfinite(a1_)&&std::isfinite(a2_);
    }
    static float clean(double x) noexcept {
        return std::isfinite(x) && std::abs(x)<10000.?
            static_cast<float>(x):0.f;
    }
    float step(float input,std::array<double,2>& z) noexcept {
        if(gainDb_<=1.e-4f)return input;
        const double in=static_cast<double>(input);
        const double y=b0_*in+z[0];
        const double next0=b1_*in-a1_*y+z[1];
        const double next1=b2_*in-a2_*y;
        z[0]=std::abs(next0)>1.e-24?next0:0.;
        z[1]=std::abs(next1)>1.e-24?next1:0.;
        return clean(y);
    }
    std::array<std::array<double,2>,2> memory_{};
    double b0_=1.,b1_=0.,b2_=0.,a1_=0.,a2_=0.;
    float gainDb_=0.,measuredLowMinusMidDb_=0.;
    bool ready_=false;
};
} // namespace armix::sonku::p80
