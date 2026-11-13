// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>

// SonKu native music-integrity guard. Two coherent, equal-angle virtual
// sources carrying L and inverted R cancel mathematically in BOTH ear FIRs.
// Do not sum their side channel into a mono object; preserve that content
// as a bounded stereo difference bed only while strong negative correlation
// is MEASURED. This is not a dearVR/AMBEO implementation or virtual BRIR.
//
// Per-instance, audio-callback-owned fixed POD state. No heap, locks,
// timers, lookahead, UI/audio routing authority or added host latency.
// A hysteretic 20 ms correlation detector avoids reacting to ordinary
// uncorrelated stereo; 15 ms gain slewing avoids a hard switch.
namespace armix::sonku {
class SonKuStereoIntegrity final {
public:
    bool prepare(double rate) noexcept {
        if (!std::isfinite(rate) || rate<40000. || rate>48000.) return false;
        evidenceAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.020)));
        blendAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.015)));
        reset();
        return true;
    }
    void reset() noexcept {
        energyL_=0.f;energyR_=0.f;cross_=0.f;amount_=0.f;
        antiCorrelated_=false;
    }
    // Returns a *single* difference signal. Caller adds it to LEFT and
    // subtracts it from RIGHT after HRTF/room sums, not before the FIR.
    // Sanitized finite x0/x1 are still bounded for correlation evidence:
    // corrupt/overscale host audio must not produce NaN in the detector.
    float preservedSide(float x0,float x1) noexcept {
        const float l=std::clamp(x0,-4.f,4.f);
        const float r=std::clamp(x1,-4.f,4.f);
        energyL_+=evidenceAlpha_*(l*l-energyL_);
        energyR_+=evidenceAlpha_*(r*r-energyR_);
        cross_+=evidenceAlpha_*(l*r-cross_);
        const float power=.5f*(energyL_+energyR_);
        if(power<1.e-8f)antiCorrelated_=false;
        else if(antiCorrelated_){
            if(cross_>-.55f*power)antiCorrelated_=false;
        }else if(cross_<-.78f*power)antiCorrelated_=true;
        amount_+=blendAlpha_*((antiCorrelated_?1.f:0.f)-amount_);
        if(std::abs(amount_)<1.e-6f)amount_=0.f;
        // At exact L=R the side is mathematically zero. Under opposite
        // polarity L=-R, this returns the original side; it never converts
        // the source to mono or modifies the measured HRTF kernel.
        return .5f*(l-r)*amount_;
    }
    float recoveryAmount()const noexcept{return amount_;}
private:
    float evidenceAlpha_=0.f,blendAlpha_=0.f;
    float energyL_=0.f,energyR_=0.f,cross_=0.f,amount_=0.f;
    bool antiCorrelated_=false;
};
} // namespace armix::sonku
