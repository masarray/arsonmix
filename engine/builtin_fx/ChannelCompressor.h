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
        for(std::size_t i=0;i<frames;++i) {
            advance();
            const float dryL=std::isfinite(left[i])?left[i]:0.f;
            const float dryR=mono?dryL:(std::isfinite(right[i])?right[i]:0.f);
            const float peak=std::max(std::abs(dryL),std::abs(dryR));
            maxInput=std::max(maxInput,peak);
            // One shared peak detector guarantees stable stereo placement.
            const float coefficient=peak>envelope_?
                current_.attackCoefficient:current_.releaseCoefficient;
            envelope_=coefficient*envelope_+(1.f-coefficient)*peak;
            if(envelope_<1.0e-25f)envelope_=0.f; // denormal guard
            const auto& s=current_.settings;
            float reduction=0.f;
            // Quiet samples never require a costly log/exp. The gate is
            // exact after each edit and conservative throughout its slew.
            if(s.ratio>1.f&&envelope_>kneeGuardLinear_) {
                const float above=20.f*std::log10(envelope_)-s.thresholdDb;
                // While a parameter moves, preserve the legacy effective
                // ratio exactly; when settled, use CONTROL-prepared scalars.
                const bool slewing=rampRemaining_>0;
                const float strength=slewing?1.f-1.f/s.ratio:
                    current_.ratioStrength;
                if(s.kneeDb<0.0001f)reduction=std::max(0.f,above)*strength;
                else if(above>=s.kneeDb*0.5f)reduction=above*strength;
                else if(above> -s.kneeDb*0.5f) {
                    const float bend=above+s.kneeDb*0.5f;
                    const float bendScale=slewing?0.5f/s.kneeDb:
                        current_.halfInverseKnee;
                    reduction=bend*bend*bendScale*strength;
                }
            }
            // Even at MIX=0 the detector and GR meter must advance, but
            // evaluating the unused wet attenuation is wasted CPU.
            const float attenuation=reduction>0.f&&s.mix>0.f?
                std::exp(reduction*(-0.11512925464970229f)):1.f;
            const float wetGain=attenuation*current_.makeupGain;
            const float outputGain=(1.f-s.mix)+s.mix*wetGain;
            left[i]=dryL*outputGain;
            if(!mono)right[i]=dryR*outputGain;
            maxOutput=std::max(maxOutput,peak*std::abs(outputGain));
            maxReduction=std::max(maxReduction,reduction);
        }
        // Pre/post PCM and GR measured in the original stereo DSP pass:
        // no extra audio scan, lookahead, allocations or log conversion.
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
