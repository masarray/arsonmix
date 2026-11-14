// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cmath>

// P8.4 OFFLINE research only. NOT Sennheiser's patented CLARITY.
// This does not prove virtual loudspeaker externalization.
// Two existing native P82 and P83 PCM outputs are *offline* references.
// Never run two full DSP cores in the live JUCE callback.
// The production milestone must fuse into one native graph if approved.
namespace armix::sonku::p84 {
class ClarityAnchor final {
public:
    bool prepare(double rate,float influence=.18f,float crossover=750.f) noexcept {
        if(!std::isfinite(rate)||rate<44000.||rate>48100.||
           !std::isfinite(influence)||influence<0.f||influence>.30f||
           !std::isfinite(crossover)||crossover<400.f||crossover>2400.f)return false;
        strength_=influence;
        alpha_=static_cast<float>(1.-std::exp(-2.*3.14159265358979323846*crossover/rate));
        reset();return true;
    }
    void reset() noexcept {low_.fill(0.f);}
    void process(float refL,float refR,float physicalL,float physicalR,
                 float& outL,float& outR) noexcept {
        if(strength_==0.f){outL=refL;outR=refR;return;}
        const float deltaL=physicalL-refL,deltaR=physicalR-refR;
        low_[0]+=alpha_*(deltaL-low_[0]);
        low_[1]+=alpha_*(deltaR-low_[1]);
        outL=refL+strength_*(deltaL-low_[0]);
        outR=refR+strength_*(deltaR-low_[1]);
    }
private:
    std::array<float,2> low_{};
    float strength_=0.f,alpha_=0.f;
};
} // namespace armix::sonku::p84
