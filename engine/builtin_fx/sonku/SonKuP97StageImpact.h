// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// PRIVATE P9.7 concert-stage enhancement. No stems, no pseudo-vocal isolation:
// "center presence" applies equally to any center-panned material. Every
// added signal stays in the SAME measured world-speaker binaural graph.
// The source MID band goes THROUGH the existing HRIR. Bass reinforces the
// already HRTF/world-positioned direct ears, and air reinforces actual
// stereo image-source early reflections (not synthetic Haas/stereo widening).
// 0 additional delay; no heap, FFT, worker, I/O, trigonometry or exp in PCM.
namespace armix::sonku::p97 {
class StageImpact final {
public:
    struct Stereo {float left=0.f,right=0.f;};
    bool prepare(double sampleRate) noexcept {
        if(!std::isfinite(sampleRate) ||
           (std::abs(sampleRate-44100.)>.1 &&
            std::abs(sampleRate-48000.)>.1))return false;
        constexpr double pi=3.1415926535897932384626433832795;
        centerCoeff_=bandpass(sampleRate,1900.,.90);
        punchCoeff_=bandpass(sampleRate,85.,.85);
        subAlpha_=static_cast<float>(1.-std::exp(-2.*pi*62./sampleRate));
        airAlpha_=static_cast<float>(1.-std::exp(-2.*pi*4500./sampleRate));
        slew_=static_cast<float>(1.-std::exp(-1./(sampleRate*.045)));
        reset();
        return true;
    }
    void reset() noexcept {
        vocal_={};vocalSecond_={};vocalDirect_={};
        punch_={};sub_={};earlyAir_={};directAir_={};stage_=0.f;
    }
    bool active() const noexcept {return stage_!=0.f;}
    float strength() const noexcept {return stage_;}
    Stereo source(float left,float right,float meters,bool roomEnabled) noexcept {
        // One slow scene-depth transition, shared across the 3 stages.
        const float target=roomEnabled?
            std::clamp((meters-2.f)/4.f,0.f,1.f):0.f;
        stage_+=slew_*(target-stage_);
        if(std::abs(stage_-target)<1.e-6f)stage_=target;
        const float center=.5f*(left+right);
        // Two bandpasses suppress out-of-band leakage, especially cymbals
        // >6kHz that a single 1.9kHz BPF would partly phase-cancel.
        const float band=vocalSecond_.run(centerCoeff_,
            vocal_.run(centerCoeff_,center));
        if(stage_==0.f)return {left,right};
        // Both virtual speakers receive exactly the same center delta.
        // Their established HRIRs must render it; NEVER send to dry L/R.
        const float delta=.19f*stage_*band;
        return {left+delta,right+delta};
    }
    Stereo direct(float left,float right) noexcept {
        const std::array<float,2> x{left,right};
        std::array<float,2> result{};
        for(int c=0;c<2;++c) {
            const float punch=punch_[c].run(punchCoeff_,x[c]);
            sub_[c]+=subAlpha_*(x[c]-sub_[c]);
            directAir_[c]+=airAlpha_*(x[c]-directAir_[c]);
            const float voice=vocalDirect_[c][1].run(centerCoeff_,
                vocalDirect_[c][0].run(centerCoeff_,x[c]));
            // Keep spectral pressure tied to the original KEMAR ears.
            // The second gentle vocal filter is NOT a dry/mono stem.
            result[c]=stage_==0.f?x[c]:
                x[c]+stage_*(.23f*punch+.085f*sub_[c]+
                             .085f*voice+.038f*(x[c]-directAir_[c]));
        }
        return {result[0],result[1]};
    }
    Stereo early(float left,float right) noexcept {
        const std::array<float,2> x{left,right};
        std::array<float,2> result{};
        for(int c=0;c<2;++c){
            earlyAir_[c]+=airAlpha_*(x[c]-earlyAir_[c]);
            // Boost only the real, azimuth-dependent first images above 4.5k.
            // No channel crossing or artificial stereo delay: retain ILD/ITD.
            result[c]=stage_==0.f?x[c]:
                x[c]+.24f*stage_*(x[c]-earlyAir_[c]);
        }
        return {result[0],result[1]};
    }
    Stereo modal(float left,float right) const noexcept {
        if(stage_==0.f)return {left,right};
        // Small damped room mode reinforcement, no feedback alteration.
        const float level=1.f+.12f*stage_;
        return {left*level,right*level};
    }
private:
    struct Coeff {float b0=0.f,b1=0.f,b2=0.f,a1=0.f,a2=0.f;};
    struct Filter {
        float z1=0.f,z2=0.f;
        float run(const Coeff& c,float in) noexcept {
            const float out=c.b0*in+z1;
            z1=c.b1*in-c.a1*out+z2;
            z2=c.b2*in-c.a2*out;
            return out;
        }
    };
    static Coeff bandpass(double sampleRate,double frequency,double q) noexcept {
        constexpr double pi=3.1415926535897932384626433832795;
        const double w=2.*pi*frequency/sampleRate;
        const double alpha=std::sin(w)/(2.*q);
        const double inv=1./(1.+alpha);
        return {static_cast<float>(alpha*inv),0.f,
                static_cast<float>(-alpha*inv),
                static_cast<float>(-2.*std::cos(w)*inv),
                static_cast<float>((1.-alpha)*inv)};
    }
    Coeff centerCoeff_{},punchCoeff_{};
    Filter vocal_{},vocalSecond_{};
    std::array<Filter,2> punch_{};
    std::array<std::array<Filter,2>,2> vocalDirect_{};
    std::array<float,2> sub_{},earlyAir_{},directAir_{};
    float subAlpha_=0.f,airAlpha_=0.f,slew_=0.f,stage_=0.f;
};
} // namespace armix::sonku::p97
