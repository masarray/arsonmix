// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Feed-forward peak compressor for the authoritative native PCM graph.
// Stereo-linked detector, immediate sample-through, no lookahead, no
// oversampling, no buffers, no heap, no locks and zero added PDC latency.
// The entire Settings packet is prevalidated on CONTROL; AUDIO only copies
// the packet at a block boundary and slews parameters to avoid zipper noise.
namespace armix::builtin_fx {
struct CompressorSettings final {
    float thresholdDb=-18.0f;
    float ratio=3.0f;
    float attackMs=10.0f;
    float releaseMs=120.0f;
    float kneeDb=6.0f;
    float makeupDb=0.0f;
    float mix=1.0f;
};

class ChannelCompressor final {
public:
    static constexpr int addedLatencySamples=0;
    struct PreparedEdit final {
        CompressorSettings settings{};
        double sampleRate=0.0;
        float attackCoefficient=0.0f;
        float releaseCoefficient=0.0f;
        float makeupGain=1.0f;
        float ratioStrength=0.0f; // CONTROL precomputed 1 - 1/ratio
        float halfInverseKnee=0.0f; // CONTROL precomputed 0.5/knee dB
        float kneeStartLinear=1.0f; // control precomputed detector gate
        bool valid=false;
    };
    static bool validSettings(const CompressorSettings& s) noexcept {
        return std::isfinite(s.thresholdDb)&&s.thresholdDb>=-60.f&&s.thresholdDb<=0.f &&
          std::isfinite(s.ratio)&&s.ratio>=1.f&&s.ratio<=20.f &&
          std::isfinite(s.attackMs)&&s.attackMs>=0.1f&&s.attackMs<=100.f &&
          std::isfinite(s.releaseMs)&&s.releaseMs>=10.f&&s.releaseMs<=2000.f &&
          std::isfinite(s.kneeDb)&&s.kneeDb>=0.f&&s.kneeDb<=24.f &&
          std::isfinite(s.makeupDb)&&s.makeupDb>=-12.f&&s.makeupDb<=24.f &&
          std::isfinite(s.mix)&&s.mix>=0.f&&s.mix<=1.f;
    }
    static bool prepareEdit(const CompressorSettings& settings,double sampleRate,
                            PreparedEdit& output) noexcept {
        if(!validSettings(settings)||!std::isfinite(sampleRate)||
           sampleRate<8000.||sampleRate>384000.)return false;
        PreparedEdit next{};
        next.settings=settings;
        next.sampleRate=sampleRate;
        next.attackCoefficient=static_cast<float>(
            std::exp(-1000.0/(static_cast<double>(settings.attackMs)*sampleRate)));
        next.releaseCoefficient=static_cast<float>(
            std::exp(-1000.0/(static_cast<double>(settings.releaseMs)*sampleRate)));
        // Compute output makeup off realtime, then slew the LINEAR gain.
        next.makeupGain=static_cast<float>(
            std::exp(static_cast<double>(settings.makeupDb)*0.11512925464970229));
        // The settled compressor does not need scalar division for each
        // compressed PCM sample. The original arithmetic is retained during
        // the short parameter slew for sample-for-sample sonic parity.
        next.ratioStrength=1.f-1.f/settings.ratio;
        // Below this guard the DSP uses the hard-knee branch. Never
        // divide by a subnormal, or reject an otherwise valid tiny knee.
        next.halfInverseKnee=settings.kneeDb>=0.0001f?
            0.5f/settings.kneeDb:0.f;
        next.kneeStartLinear=static_cast<float>(std::exp(
            static_cast<double>(settings.thresholdDb-settings.kneeDb*.5f)*
            0.11512925464970229));
        next.valid=std::isfinite(next.makeupGain)&&
                   std::isfinite(next.ratioStrength)&&
                   std::isfinite(next.halfInverseKnee)&&
                   std::isfinite(next.kneeStartLinear)&&next.kneeStartLinear>0.f;
        output=next;
        return true;
    }
    bool prepare(double sampleRate) noexcept {
        PreparedEdit packet{};
        if(!prepareEdit(target_.settings,sampleRate,packet))return false;
        sampleRate_=sampleRate;
        fadeSamples_=std::max(1,static_cast<int>(sampleRate*0.008));
        current_=target_=packet;
        kneeGuardLinear_=packet.kneeStartLinear;
        envelope_=0.f;
        gainReductionDb_=0.f;inputPeak_=outputPeak_=0.f;
        rampRemaining_=0;
        ready_=true;
        return true;
    }
    bool resetUnpublished(const CompressorSettings& settings) noexcept {
        PreparedEdit edit{};
        if(!ready_||!prepareEdit(settings,sampleRate_,edit))return false;
        current_=target_=edit;
        kneeGuardLinear_=edit.kneeStartLinear;
        envelope_=0.f;
        gainReductionDb_=0.f;inputPeak_=outputPeak_=0.f;
        rampRemaining_=0;
        return true;
    }
    bool applyPrepared(const PreparedEdit& edit) noexcept {
        if(!ready_||!edit.valid||edit.sampleRate!=sampleRate_||
           !validSettings(edit.settings)||!std::isfinite(edit.attackCoefficient)||
           !std::isfinite(edit.releaseCoefficient)||
           !std::isfinite(edit.makeupGain)||edit.makeupGain<=0.f||
           !std::isfinite(edit.ratioStrength)||edit.ratioStrength<0.f||
           edit.ratioStrength>1.f||
           !std::isfinite(edit.halfInverseKnee)||edit.halfInverseKnee<0.f||
           !std::isfinite(edit.kneeStartLinear)||edit.kneeStartLinear<=0.f||
           edit.attackCoefficient<0.f||edit.attackCoefficient>=1.f||
           edit.releaseCoefficient<0.f||edit.releaseCoefficient>=1.f)return false;
        // During a parameter ramp the minimum of all endpoints is a safe
        // conservative bound. Never skip legitimate soft-knee compression.
        kneeGuardLinear_=std::min(kneeGuardLinear_,edit.kneeStartLinear);
        target_=edit;
        rampRemaining_=fadeSamples_;
        return true;
    }
    void processMono(float* samples,std::size_t frames) noexcept {
        processStereo(samples,samples,frames);
    }
    void processStereo(float* left,float* right,std::size_t frames) noexcept {
        if(!ready_||!left||!right||frames==0)return;
        const bool mono=left==right;
        float maxReduction=0.f,maxInput=0.f,maxOutput=0.f;
        // First consume the actual 8-ms parameter slew, if any. Only the
        // remaining frames use the settled kernel: no per-sample ramp check,
        // coefficient reload, or legacy scalar division in steady state.
        // Never shift the ramp at a buffer boundary: each sample still
        // advances exactly once BEFORE detection and gain calculation.
        const auto slew=std::min(frames,
            static_cast<std::size_t>(std::max(0,rampRemaining_)));
        if(slew)renderRange<true>(left,right,mono,0,slew,
                                  maxInput,maxOutput,maxReduction);
        if(slew<frames)renderRange<false>(left,right,mono,slew,frames,
                                          maxInput,maxOutput,maxReduction);
        inputPeak_=maxInput;
        outputPeak_=maxOutput;
        gainReductionDb_=maxReduction;
    }
    [[nodiscard]] float gainReductionDb() const noexcept {return gainReductionDb_;}
    [[nodiscard]] float inputPeak() const noexcept {return inputPeak_;}
    [[nodiscard]] float outputPeak() const noexcept {return outputPeak_;}
    [[nodiscard]] bool isReady() const noexcept {return ready_;}
    [[nodiscard]] double sampleRate() const noexcept {return sampleRate_;}
    [[nodiscard]] CompressorSettings settingsForOfflineTests() const noexcept {
        return target_.settings;
    }
private:
    PreparedEdit current_{},target_{};
    double sampleRate_=0.;
    float envelope_=0.f,gainReductionDb_=0.f,kneeGuardLinear_=1.f;
    float inputPeak_=0.f,outputPeak_=0.f;
    int rampRemaining_=0,fadeSamples_=1;
    bool ready_=false;
    // Two compile-time kernels, not two engines or DSP authorities.
    // Settled parameters are cached once per contiguous range. The ramp
    // kernel retains the old per-sample arithmetic, including the special
    // final sample where advance() reaches the prepared target.
    template<bool Slewing>
    void renderRange(float* left,float* right,bool mono,
                     std::size_t begin,std::size_t end,
                     float& maxInput,float& maxOutput,
                     float& maxReduction) noexcept {
        // These copies are exclusively used by the steady-state specialization;
        // controls are immutable until an explicit realtime applyPrepared().
        const auto settledSettings=current_.settings;
        const float settledAttack=current_.attackCoefficient;
        const float settledRelease=current_.releaseCoefficient;
        const float settledMakeup=current_.makeupGain;
        const float settledStrength=current_.ratioStrength;
        const float settledKneeScale=current_.halfInverseKnee;
        const float settledGate=kneeGuardLinear_;
        for(std::size_t i=begin;i<end;++i) {
            if constexpr (Slewing)advance();
            const auto& s=Slewing?current_.settings:settledSettings;
            const float dryL=std::isfinite(left[i])?left[i]:0.f;
            const float dryR=mono?dryL:(std::isfinite(right[i])?right[i]:0.f);
            const float peak=std::max(std::abs(dryL),std::abs(dryR));
            maxInput=std::max(maxInput,peak);
            const float attack=Slewing?current_.attackCoefficient:settledAttack;
            const float release=Slewing?current_.releaseCoefficient:settledRelease;
            const float coefficient=peak>envelope_?attack:release;
            envelope_=coefficient*envelope_+(1.f-coefficient)*peak;
            if(envelope_<1.0e-25f)envelope_=0.f;
            float reduction=0.f;
            const float gate=Slewing?kneeGuardLinear_:settledGate;
            if(s.ratio>1.f&&envelope_>gate) {
                const float above=20.f*std::log10(envelope_)-s.thresholdDb;
                // The final ramp sample has already committed the prepared
                // packet, and therefore uses its settled scalar as before.
                const bool midSlew=Slewing&&rampRemaining_>0;
                const float strength=midSlew?1.f-1.f/s.ratio:
                    (Slewing?current_.ratioStrength:settledStrength);
                if(s.kneeDb<0.0001f)reduction=std::max(0.f,above)*strength;
                else if(above>=s.kneeDb*0.5f)reduction=above*strength;
                else if(above> -s.kneeDb*0.5f) {
                    const float bend=above+s.kneeDb*0.5f;
                    const float bendScale=midSlew?0.5f/s.kneeDb:
                        (Slewing?current_.halfInverseKnee:settledKneeScale);
                    reduction=bend*bend*bendScale*strength;
                }
            }
            // A fully dry mix still advances the detector and publishes GR.
            const float attenuation=reduction>0.f&&s.mix>0.f?
                std::exp(reduction*(-0.11512925464970229f)):1.f;
            const float makeup=Slewing?current_.makeupGain:settledMakeup;
            const float wetGain=attenuation*makeup;
            const float outputGain=(1.f-s.mix)+s.mix*wetGain;
            left[i]=dryL*outputGain;
            if(!mono)right[i]=dryR*outputGain;
            maxOutput=std::max(maxOutput,peak*std::abs(outputGain));
            maxReduction=std::max(maxReduction,reduction);
        }
    }
    void advance() noexcept {
        if(rampRemaining_<=0)return;
        const float f=1.f/static_cast<float>(rampRemaining_);
        auto slide=[f](float& a,float b) noexcept {a+=(b-a)*f;};
        auto& a=current_.settings;const auto& b=target_.settings;
        slide(a.thresholdDb,b.thresholdDb);slide(a.ratio,b.ratio);
        slide(a.attackMs,b.attackMs);slide(a.releaseMs,b.releaseMs);
        slide(a.kneeDb,b.kneeDb);slide(a.makeupDb,b.makeupDb);
        slide(a.mix,b.mix);
        slide(current_.attackCoefficient,target_.attackCoefficient);
        slide(current_.releaseCoefficient,target_.releaseCoefficient);
        slide(current_.makeupGain,target_.makeupGain);
        if(--rampRemaining_==0){
            current_=target_;
            kneeGuardLinear_=target_.kneeStartLinear;
        }
    }
};
} // namespace armix::builtin_fx
