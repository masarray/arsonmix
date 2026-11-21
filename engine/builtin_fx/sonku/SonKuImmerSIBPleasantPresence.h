// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

// ImmerSIB-only post-binaural presence comfort correction.
//
// One SAME broad, time-varying tonal transfer for BOTH KEMAR ears:
// NO MID/SIDE recombination, no lateral subtraction, no crossfeed,
// no reverb/bass graph changes or extra delay. Retains IACC/panning
// much better than applying independently-triggered L/R compressors.
// The isolated upper-mid band is sent in parallel ONLY as the input
// of one subtractive tonal filter (not a dry extra renderer).
// Shaping: x - g * BP(x), approximately a very broad peaking CUT.
// A second VERY gentle linked 350Hz body lift (+0.67dB max)
// restores guitar/vocal warmth on BOTH source sides, not just MID.
// BP is a fixed-coefficient stable 2-pole IIR. Gain detector 40ms,
// linked across ears; 20ms attack / 200ms release, no callback allocation.
namespace armix::sonku::immersib {
class PleasantPresence final {
public:
    struct Stereo {float left=0.f,right=0.f;};
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate) ||
           (std::abs(rate-44100.)>.1 && std::abs(rate-48000.)>.1))
            return false;
        constexpr double pi=3.14159265358979323846;
        constexpr double hz=3750., q=.72; // broad 2.5-5.6kHz comfort zone
        const double w=2.*pi*hz/rate;
        const double a=std::sin(w)/(2.*q);
        const double inv=1./(1.+a);
        const Coeff next{
            static_cast<float>(a*inv),0.f,
            static_cast<float>(-a*inv),
            static_cast<float>(-2.*std::cos(w)*inv),
            static_cast<float>((1.-a)*inv)
        };
        coeff_=next;
        // Warm BODY remains audible for both centered voice and the
        // panned acoustic guitar. One identical, gentle band lift at
        // 350Hz (Q=.88) across both ears; not a centered MID boost.
        // Fixed prepared coefficients; at 86Hz the EQ change is <0.1dB.
        constexpr double bodyHz=350.,bodyQ=.88;
        const double bw=2.*pi*bodyHz/rate;
        const double ba=std::sin(bw)/(2.*bodyQ);
        const double binv=1./(1.+ba);
        bodyCoeff_={
            static_cast<float>(ba*binv),0.f,
            static_cast<float>(-ba*binv),
            static_cast<float>(-2.*std::cos(bw)*binv),
            static_cast<float>((1.-ba)*binv)
        };
        energyAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.040)));
        gainAttack_=static_cast<float>(1.-std::exp(-1./(rate*.020)));
        gainRelease_=static_cast<float>(1.-std::exp(-1./(rate*.200)));
        enableSlew_=static_cast<float>(1.-std::exp(-1./(rate*.030)));
        reset();
        return true;
    }
    void reset() noexcept {
        filters_[0]={};filters_[1]={};
        bodyFilters_[0]={};bodyFilters_[1]={};
        bandEnergy_=totalEnergy_=0.f;
        reduction_=baseReduction_;
        enabledMix_=0.f;
    }
    Stereo process(float left,float right,bool enable) noexcept {
        // Process both ear filters even when disengaged so future scene
        // retargeting does not reuse a stale spectral accumulator.
        const float bandL=filters_[0].run(coeff_,left);
        const float bandR=filters_[1].run(coeff_,right);
        const float bodyL=bodyFilters_[0].run(bodyCoeff_,left);
        const float bodyR=bodyFilters_[1].run(bodyCoeff_,right);
        const float bandNow=.5f*(bandL*bandL+bandR*bandR);
        const float totalNow=.5f*(left*left+right*right);
        bandEnergy_+=energyAlpha_*(bandNow-bandEnergy_);
        totalEnergy_+=energyAlpha_*(totalNow-totalEnergy_);
        // Broadband tonality proxy, not a vocal/stem detector. An
        // extremely 3-5k-heavy passage receives a few extra dB relief,
        // regardless of whether its source is voice, guitar or cymbal.
        const float bandShare=bandEnergy_/(totalEnergy_+1.e-8f);
        const float excess=std::clamp((bandShare-.11f)/.40f,0.f,1.f);
        const float target=baseReduction_+.14f*excess;
        reduction_+=(target>reduction_?gainAttack_:gainRelease_)*
                    (target-reduction_);
        enabledMix_+=enableSlew_*((enable?1.f:0.f)-enabledMix_);
        if(!enable)return {left,right}; // exact no-room / 0%-MIX bypass
        const float amount=reduction_*enabledMix_;
        // +0.67dB around 350Hz restores guitar/vocal musical warmth
        // by a SHARED transfer. Do not thicken only the phantom center;
        // 86Hz bass, actual PA world/room and L/R pan remain protected.
        const float bodyAmount=.08f*enabledMix_;
        return {left-amount*bandL+bodyAmount*bodyL,
                right-amount*bandR+bodyAmount*bodyR};
    }
private:
    static constexpr float baseReduction_=.15f; // ~-1.4 dB at band center
    struct Coeff {float b0=0.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f;};
    struct Filter {
        float z1=0.f,z2=0.f;
        float run(const Coeff& c,float x) noexcept {
            const float y=c.b0*x+z1;
            z1=c.b1*x-c.a1*y+z2;
            z2=c.b2*x-c.a2*y;
            return y;
        }
    };
    Coeff coeff_{},bodyCoeff_{};
    Filter filters_[2]{},bodyFilters_[2]{};
    float energyAlpha_=0.f,gainAttack_=0.f,gainRelease_=0.f,enableSlew_=0.f;
    float bandEnergy_=0.f,totalEnergy_=0.f;
    float reduction_=baseReduction_,enabledMix_=0.f;
};
} // namespace armix::sonku::immersib
