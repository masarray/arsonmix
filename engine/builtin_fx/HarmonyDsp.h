// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "HarmonyStereoFold.h"
#include "HarmonyPreparedTone.h"

// Deterministic, allocation-free vocal harmony DSP. No JUCE or GUI dependency.
// Grain shifting is a functioning prototype, not formant-accurate synthesis.
namespace armix::harmony {
struct Settings {
    int key=0, scale=0, voices=3; // C=0..B=11; 0=major, 1=natural minor; 2..5 voices
    bool correction=false;
    // Manual is always the trusted fallback. Music follows another accepted
    // ChannelId only after stable chroma confidence (never a PCM route).
    int guideMode=0; // 0=Manual Key, 1=Music Track
    std::uint64_t guideChannelId=0;
    float wet=0.45f, space=0.60f, color=0.50f, gainDb=0.f;
};
inline bool valid(const Settings& s) noexcept {
    return s.key>=0&&s.key<12&&s.scale>=0&&s.scale<=1&&
      s.guideMode>=0&&s.guideMode<=1&&
      (s.guideMode==0?s.guideChannelId==0:s.guideChannelId>0)&&
      s.voices>=2&&s.voices<=5&&std::isfinite(s.wet)&&s.wet>=0&&s.wet<=1&&
      std::isfinite(s.space)&&s.space>=0&&s.space<=1&&
      std::isfinite(s.color)&&s.color>=0&&s.color<=1&&
      std::isfinite(s.gainDb)&&s.gainDb>=-24&&s.gainDb<=12;
}
inline constexpr std::array<int,7> major{{0,2,4,5,7,9,11}};
inline constexpr std::array<int,7> minor{{0,2,3,5,7,8,10}};
inline int floorDiv(int a,int b) noexcept { const int q=a/b,r=a%b;return r<0?q-1:q; }
inline int noteAt(int degree,int key,int scale) noexcept {
    const auto& notes=scale==0?major:minor;
    const int oct=floorDiv(degree,7),step=degree-oct*7;
    return key+12*oct+notes[static_cast<std::size_t>(step)];
}
inline int nearestDegree(float midi,int key,int scale) noexcept {
    const int approx=static_cast<int>(std::floor(midi/12.f))*7;
    int best=approx; float error=1.e9f;
    for(int d=approx-21;d<=approx+21;++d) {
        const float e=std::abs(static_cast<float>(noteAt(d,key,scale))-midi);
        if(e<error){error=e;best=d;}
    }
    return best;
}
// Complementary overlap-add seam with zero slope at every grain boundary.
// A triangular seam has a slope discontinuity that can color sustained vowels.
// Precondition: phase is owned by Engine and always in [0,1].
// Avoid alternating adjacent scale notes when sung vibrato hovers near a
// semitone boundary. Only move after the candidate wins by a small margin.
inline bool pitchChangeHasMargin(int held,int candidate,float midi) noexcept {
    return held<0||held==candidate||
      std::abs(midi-static_cast<float>(candidate))+0.20f<
      std::abs(midi-static_cast<float>(held));
}
inline float smoothGrainBlend(float phase) noexcept {
    const float u=1.f-std::abs(2.f*phase-1.f);
    return u*u*(3.f-2.f*u);
}
class Engine final {
public:
    static constexpr int maxVoices=5;
    static constexpr int maxPitchLagsPerSlice=8; // fewer burst MACs per sample
    // Maximum 192 kHz grain is 3072 frames: 2*grain latency + 3*grain
    // history = 15360, so 16384 frames retain the full rate/ratio budget.
    // Halves the three large audio rings per instance without RT allocation.
    static constexpr std::size_t capacity=16384;
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        rate_=rate;
        grain_=std::clamp(static_cast<int>(std::round(rate*0.016)),256,3072);
        latency_=2*grain_;
        decimation_=std::max(1,static_cast<int>(std::round(rate/12000.)));
        detectEvery_=std::max(128,static_cast<int>(rate/150.));
        // 32 fixed time slices per analysis hop, but only 8 lags/slice.
        // Even 32k input completes every supported 65-1200 Hz candidate
        // before the next frozen PCM window. Distributes RT work on ASIO 64.
        pitchSliceEvery_=std::max(1,detectEvery_/32);
        // Support bass through soprano fundamentals; keep Nyquist/finiteness guards.
        // Earlier 850 Hz cap failed to track high soprano notes (e.g. C6).
        lowLag_=std::max(8,static_cast<int>(rate/(decimation_*1200.)));
        highLag_=std::min(230,static_cast<int>(rate/(decimation_*65.)));
        if(lowLag_>=highLag_||latency_+3*grain_>=static_cast<int>(capacity))return false;
        left_.fill(0.f);right_.fill(0.f);mono_.fill(0.f);decimated_.fill(0.f);
        filtered_.fill(0.f);phase_.fill(0.f);ratio_.fill(1.f);targetRatio_.fill(1.f);
        // Low-pass before downsampling: raw 12 kHz decimation aliases vocal sibilants
        // into the periodicity detector and destabilizes the live note readback.
        decimationLowpass_=0.f;
        if(!vocalFold_.prepare(rate)||!toneColor_.prepare(rate))return false;
        pitchIntervals_.prepare();
        const float cutoff=1800.f;
        decimationAlpha_=static_cast<float>(1.-std::exp(-2.*3.141592653589793*cutoff/rate));
        write_=0;downWrite_=0;downSeen_=0;downCounter_=0;
        detectCounter_=0;pitchSliceCounter_=0;pitchNextLag_=0;
        pitchActive_=false;pitchBest_=-1.f;pitchBestLag_=0;
        peakPitchLagWork_=0;completedPitchAnalyses_=0;abandonedPitchAnalyses_=0;
        noteHold_=0;currentNote_=-1;candidateNote_=-1;
        guideKey_=-1;guideScale_=-1;
        voiced_=false;voicedGain_=0.f;settings_=Settings{};
        smoothingAlpha_=static_cast<float>(1.-std::exp(-1./(rate*0.005))); // 5 ms control ramp
        // Preserve the established 48k envelope while keeping durations
        // constant at 32-192k. Exponents computed off the callback.
        voicedGateAlpha_=static_cast<float>(1.-std::pow(1.-0.002,48000./rate));
        ratioAlpha_=static_cast<float>(1.-std::pow(1.-0.00045,48000./rate));
        processed_=false;settings_=Settings{};
        return setSettings(settings_);
    }
    // Prepared on the CONTROL thread. Immutable edit data is copied through
    // the existing SPSC; applying it on the callback needs only fixed copies.
    struct PreparedSettings {
        Settings settings{};
        float gain=1.f;
        float mixL[maxVoices]{},mixR[maxVoices]{};
    };
    static bool prepareSettings(const Settings& next,PreparedSettings& out) noexcept {
        if(!valid(next))return false;
        PreparedSettings prepared;
        prepared.settings=next;
        prepared.gain=std::pow(10.f,next.gainDb/20.f);
        constexpr float pi=3.14159265358979323846f;
        const float amplitude=next.wet*0.65f/std::sqrt(static_cast<float>(next.voices));
        for(int v=0;v<next.voices;++v){
            const float pan=next.space*(2.f*static_cast<float>(v)/(next.voices-1)-1.f);
            const float angle=(pan+1.f)*pi*0.25f;
            prepared.mixL[v]=amplitude*std::cos(angle);
            prepared.mixR[v]=amplitude*std::sin(angle);
        }
        out=prepared;return true;
    }
    bool applyPreparedSettings(const PreparedSettings& next) noexcept {
        // Caller publishes only prepareSettings()-validated packets.
        // A scale/key change must not inherit the previous tonal quantizer
        // candidate or remain held on a now-out-of-scale note.
        if(next.settings.key!=settings_.key||next.settings.scale!=settings_.scale||
           next.settings.guideMode!=settings_.guideMode||
           next.settings.guideChannelId!=settings_.guideChannelId){
            candidateNote_=-1;currentNote_=-1;noteHold_=0;voiced_=false;
        }
        if(next.settings.guideMode!=settings_.guideMode||
           next.settings.guideChannelId!=settings_.guideChannelId){
            guideKey_=-1;guideScale_=-1;
        }
        settings_=next.settings;
        targetWet_=next.settings.wet;targetColor_=next.settings.color;
        targetGain_=next.gain;
        targetCorrection_=next.settings.correction?1.f:0.f;
        for(int v=0;v<maxVoices;++v){
            targetMixL_[static_cast<std::size_t>(v)]=next.mixL[v];
            targetMixR_[static_cast<std::size_t>(v)]=next.mixR[v];
        }
        if(!processed_){
            wet_=targetWet_;color_=targetColor_;gain_=targetGain_;
            correctionMix_=targetCorrection_;
            mixL_=targetMixL_;mixR_=targetMixR_;
        }
        return true;
    }
    // Control-thread lifecycle and standalone test entry only. The realtime
    // FX mailbox invokes applyPreparedSettings(), never this trig/pow path.
    bool setSettings(const Settings& next) noexcept {
        PreparedSettings prepared;
        return prepareSettings(next,prepared)&&applyPreparedSettings(prepared);
    }
    // Called only on the existing audio callback from a separately selected
    // source channel. Invalid/ambiguous music always restores Manual Key.
    void setGuideKey(int key,int scale) noexcept {
        if(settings_.guideMode!=1){key=-1;scale=-1;}
        if(key<0||key>=12||scale<0||scale>1){key=-1;scale=-1;}
        const int previousKey=effectiveKey(),previousScale=effectiveScale();
        guideKey_=key;guideScale_=scale;
        if(previousKey!=effectiveKey()||previousScale!=effectiveScale()){
            currentNote_=-1;candidateNote_=-1;noteHold_=0;voiced_=false;
        }
    }
    int effectiveKey() const noexcept {
        return settings_.guideMode==1&&guideKey_>=0?guideKey_:settings_.key;
    }
    int effectiveScale() const noexcept {
        return settings_.guideMode==1&&guideScale_>=0?guideScale_:settings_.scale;
    }
    const Settings& settings() const noexcept{return settings_;}
    int latencySamples() const noexcept{return latency_;}
    int observedMidiNote() const noexcept{return currentNote_;}
    bool voiced() const noexcept{return voiced_;}
    int peakPitchLagWork() const noexcept{return peakPitchLagWork_;}
    // Audio-owner telemetry; standalone CTest reads after process() returns.
    int completedPitchAnalyses() const noexcept{return completedPitchAnalyses_;}
    int abandonedPitchAnalyses() const noexcept{return abandonedPitchAnalyses_;}
    void process(float* l,float* r,int frames) noexcept {
        if(!l||!r||frames<=0||rate_<=0.)return;
        processed_=true;
        for(int i=0;i<frames;++i) {
            // The audio thread never calls trig or rebuilds panning geometry.
            // A 5 ms one-pole ramp suppresses automation/preset zipper clicks.
            const float a=smoothingAlpha_;
            wet_+=a*(targetWet_-wet_);
            color_+=a*(targetColor_-color_);
            gain_+=a*(targetGain_-gain_);
            correctionMix_+=a*(targetCorrection_-correctionMix_);
            for(int v=0;v<maxVoices;++v){
                const auto ix=static_cast<std::size_t>(v);
                mixL_[ix]+=a*(targetMixL_[ix]-mixL_[ix]);
                mixR_[ix]+=a*(targetMixR_[ix]-mixR_[ix]);
            }
            // Bound pathological IEEE PCM before adding it to grain/dry
            // histories; output is already limited to +/-4 in this engine.
            // Regular [-4,+4] sample parity and stereo polarity are exact.
            const float inputL=std::clamp(std::isfinite(l[i])?l[i]:0.f,-4.f,4.f);
            const float inputR=std::clamp(std::isfinite(r[i])?r[i]:0.f,-4.f,4.f);
            const float mono=vocalFold_.process(inputL,inputR);
            left_[write_]=inputL;right_[write_]=inputR;mono_[write_]=mono;
            decimationLowpass_+=decimationAlpha_*(mono-decimationLowpass_);
            if(++downCounter_>=decimation_){
                downCounter_=0;
                decimated_[downWrite_]=decimationLowpass_;
                downWrite_=(downWrite_+1)%decimated_.size();
                ++downSeen_;
            }
            if(++detectCounter_>=detectEvery_){
                detectCounter_=0;beginPitchAnalysis();
            }
            // Correlation search is spread across the existing sample
            // callback: never evaluate all 175+ candidate lags at once.
            // Every 64-sample ASIO period sees a bounded number of slices.
            if(pitchActive_ && ++pitchSliceCounter_>=pitchSliceEvery_){
                pitchSliceCounter_=0;continuePitchAnalysis();
            }
            const float dryL=tap(left_,static_cast<float>(latency_));
            const float dryR=tap(right_,static_cast<float>(latency_));
            voicedGain_+=((voiced_?1.f:0.f)-voicedGain_)*voicedGateAlpha_;
            // Compute the device-rate-correct filter coefficient once per
            // active sample, not once for each of up to six shifters.
            const float filterCoef=voicedGain_>0.00001f?
                toneColor_.coefficient(color_):0.f;
            float lead=0.f;
            if(correctionMix_>0.00001f&&voicedGain_>0.00001f)
                lead=shifted(maxVoices,filterCoef);
            const float leadBlend=correctionMix_*voicedGain_;
            float outL=(dryL*(1.f-leadBlend)+lead*leadBlend)*(1.f-wet_*0.65f*voicedGain_);
            float outR=(dryR*(1.f-leadBlend)+lead*leadBlend)*(1.f-wet_*0.65f*voicedGain_);
            if(voicedGain_>0.00001f){
                for(int v=0;v<maxVoices;++v){
                    const auto ix=static_cast<std::size_t>(v);
                    if(std::abs(mixL_[ix])+std::abs(mixR_[ix])<1.e-5f)continue;
                    const float y=shifted(v,filterCoef)*voicedGain_;
                    outL+=y*mixL_[ix];outR+=y*mixR_[ix];
                }
            }
            l[i]=std::clamp(outL*gain_,-4.f,4.f);
            r[i]=std::clamp(outR*gain_,-4.f,4.f);
            write_=(write_+1)%capacity;
        }
    }
private:
    float tap(const std::array<float,capacity>& a,float delay) const noexcept {
        const float d=std::clamp(delay,1.f,static_cast<float>(capacity-3));
        const int whole=static_cast<int>(d);
        const float f=d-whole;
        const auto a0=(write_+capacity-static_cast<std::size_t>(whole))%capacity;
        const auto a1=(a0+capacity-1)%capacity;
        return a[a0]*(1.f-f)+a[a1]*f;
    }
    float shifted(int voice,float filterCoef) noexcept {
        float& currentRatio=ratio_[static_cast<std::size_t>(voice)];
        currentRatio+=(targetRatio_[static_cast<std::size_t>(voice)]-currentRatio)*ratioAlpha_;
        const float p=phase_[static_cast<std::size_t>(voice)];
        const float pB=p<0.5f?p+0.5f:p-0.5f;
        const float delayA=2.f*grain_+(1.f-currentRatio)*p*grain_;
        const float delayB=2.f*grain_+(1.f-currentRatio)*pB*grain_;
        // Cubic smoothstep over the complementary two-grain windows.
        // Unlike the triangular seam, both endpoints have zero slope:
        // reduced grain-edge modulation without sin/cos in audio callback.
        const float w=smoothGrainBlend(p);
        float y=w*tap(mono_,delayA)+(1.f-w)*tap(mono_,delayB);
        float& f=filtered_[static_cast<std::size_t>(voice)];
        f+=filterCoef*(y-f);y=f;
        phase_[static_cast<std::size_t>(voice)]+=1.f/static_cast<float>(grain_);
        if(phase_[static_cast<std::size_t>(voice)]>=1.f)
            phase_[static_cast<std::size_t>(voice)]-=1.f;
        return y;
    }
    // Prepare one immutable, bounded pitch-analysis window every analysis hop.
    // The subsequent 8-lag correlation slices cannot race PCM history writes:
    // they only read the frozen fixed-size pitchWindow_.
    void beginPitchAnalysis() noexcept {
        if(pitchActive_)++abandonedPitchAnalyses_;
        pitchActive_=false;
        if(downSeen_<static_cast<std::size_t>(highLag_+170))return;
        constexpr int span=160;
        auto ix=(downWrite_+decimated_.size()-1)%decimated_.size();
        for(int j=0;j<span+highLag_;++j){
            pitchWindow_[static_cast<std::size_t>(j)]=decimated_[ix];
            ix=ix==0?decimated_.size()-1:ix-1;
        }
        pitchEnergy_=0.;
        for(int j=0;j<span;++j){
            const double x=pitchWindow_[static_cast<std::size_t>(j)];
            pitchEnergy_+=x*x;
        }
        if(pitchEnergy_/static_cast<double>(span)<1.e-6){
            voiced_=false;noteHold_=0;candidateNote_=-1;currentNote_=-1;
            return;
        }
        pitchCorrelations_.fill(0.f);
        pitchLagEnergy_=0.;
        for(int j=0;j<span;++j){
            const double y=pitchWindow_[static_cast<std::size_t>(j+lowLag_)];
            pitchLagEnergy_+=y*y;
        }
        pitchBest_=-1.f;pitchBestLag_=0;pitchNextLag_=lowLag_;
        pitchSliceCounter_=0;
        pitchActive_=true;
    }
    void continuePitchAnalysis() noexcept {
        if(!pitchActive_)return;
        constexpr int span=160;
        const int endLag=std::min(highLag_,pitchNextLag_+maxPitchLagsPerSlice-1);
        const int work=endLag-pitchNextLag_+1;
        peakPitchLagWork_=std::max(peakPitchLagWork_,work);
        for(int lag=pitchNextLag_;lag<=endLag;++lag){
            if(lag>lowLag_){
                const double leaving=pitchWindow_[static_cast<std::size_t>(lag-1)];
                const double entering=pitchWindow_[static_cast<std::size_t>(span+lag-1)];
                pitchLagEnergy_+=entering*entering-leaving*leaving;
            }
            double xy=0.;
            for(int j=0;j<span;++j){
                const double x=pitchWindow_[static_cast<std::size_t>(j)];
                const double y=pitchWindow_[static_cast<std::size_t>(j+lag)];
                xy+=x*y;
            }
            const float correlation=static_cast<float>(
                xy/std::sqrt(pitchEnergy_*std::max(0.,pitchLagEnergy_)+1.e-14));
            pitchCorrelations_[static_cast<std::size_t>(lag)]=correlation;
            if(correlation>pitchBest_){pitchBest_=correlation;pitchBestLag_=lag;}
        }
        pitchNextLag_=endLag+1;
        if(pitchNextLag_>highLag_){
            pitchActive_=false;
            ++completedPitchAnalyses_;
            finishPitchAnalysis();
        }
    }
    void finishPitchAnalysis() noexcept {
        const auto& correlations=pitchCorrelations_;
        float best=pitchBest_;
        int bestLag=pitchBestLag_;
        // Prefer shortest strong period rather than an octave-multiple peak.
        if(best>0.77f){
            for(int lag=lowLag_;lag<=highLag_;++lag){
                if(correlations[static_cast<std::size_t>(lag)]>=std::max(0.77f,best-0.035f)){
                    bestLag=lag;break;
                }
            }
        }
        if(bestLag==0||best<=0.77f){
            voiced_=false;noteHold_=0;candidateNote_=-1;currentNote_=-1;return;
        }
        // Three-point peak interpolation reduces integer-lag tuning steps,
        // especially at the high end of the supported singing range.
        float lag=static_cast<float>(bestLag);
        if(bestLag>lowLag_&&bestLag<highLag_){
            const float a=correlations[static_cast<std::size_t>(bestLag-1)];
            const float b=correlations[static_cast<std::size_t>(bestLag)];
            const float d=correlations[static_cast<std::size_t>(bestLag+1)];
            const float curvature=a-2.f*b+d;
            if(curvature < -1.e-6f)
                lag+=std::clamp(0.5f*(a-d)/curvature,-0.5f,0.5f);
        }
        const float hz=static_cast<float>(rate_)/(decimation_*lag);
        const float midi=69.f+12.f*std::log2(hz/440.f);
        if(!std::isfinite(midi)||midi<31.f||midi>96.f){
            voiced_=false;noteHold_=0;candidateNote_=-1;currentNote_=-1;return;
        }
        const int activeKey=effectiveKey(),activeScale=effectiveScale();
        int degree=nearestDegree(midi,activeKey,activeScale);
        int note=noteAt(degree,activeKey,activeScale);
        if(!pitchChangeHasMargin(currentNote_,note,midi)){
            note=currentNote_;
            degree=nearestDegree(static_cast<float>(note),activeKey,activeScale);
        }
        if(note!=candidateNote_){candidateNote_=note;noteHold_=1;}
        // Saturate the hold counter: a multi-hour sustained note must not
        // overflow a signed int in the realtime thread.
        else if(noteHold_<3)++noteHold_;
        // Two consistent windows for both phrase onset and melodic transitions;
        // do not generate provisional harmony on a single noisy transient.
        if(noteHold_<2)return;
        currentNote_=note;voiced_=true;
        // Fifth harmony voice is a lower sixth in key; keep each voice
        // independent and normalize its level against the total count.
        // One fractional-pitch exponent per analysis hop; each harmony
        // interval is an integer number of semitones in the chosen scale.
        // Five previous pow() calls move to the CONTROL prepared LUT.
        const float baseRatio=std::pow(2.f,(note-midi)/12.f);
        constexpr std::array<int,maxVoices> intervals{{2,4,-2,6,-4}};
        for(int v=0;v<maxVoices;++v){
            const int target=noteAt(degree+intervals[static_cast<std::size_t>(v)],
                                    activeKey,activeScale);
            targetRatio_[static_cast<std::size_t>(v)]=std::clamp(
                baseRatio*pitchIntervals_.ratio(target-note),0.50f,2.0f);
        }
        targetRatio_[maxVoices]=std::clamp(baseRatio,0.75f,1.34f);
    }
    double rate_=0.; int grain_=0,latency_=0,decimation_=4,detectEvery_=320;
    int lowLag_=12,highLag_=144;std::size_t write_=0,downWrite_=0,downSeen_=0;
    int downCounter_=0,detectCounter_=0;
    int pitchSliceEvery_=1,pitchSliceCounter_=0,pitchNextLag_=0;
    int pitchBestLag_=0,peakPitchLagWork_=0;
    int completedPitchAnalyses_=0,abandonedPitchAnalyses_=0;
    bool pitchActive_=false;
    float pitchBest_=-1.f;
    double pitchEnergy_=0.,pitchLagEnergy_=0.;
    // Fixed PCM snapshots; no allocation or variable work on the callback.
    std::array<float,160+231> pitchWindow_{};
    std::array<float,231> pitchCorrelations_{};
    int currentNote_=-1,candidateNote_=-1,noteHold_=0;
    int guideKey_=-1,guideScale_=-1;
    bool voiced_=false;float voicedGain_=0.f;
    bool processed_=false;
    float smoothingAlpha_=1.f,voicedGateAlpha_=0.002f,ratioAlpha_=0.00045f;
    float wet_=0.f,targetWet_=0.f,color_=0.f,targetColor_=0.f;
    float gain_=1.f,targetGain_=1.f,correctionMix_=0.f,targetCorrection_=0.f;
    std::array<float,maxVoices> mixL_{},mixR_{},targetMixL_{},targetMixR_{};
    float decimationLowpass_=0.f,decimationAlpha_=0.f;
    StereoPhaseSafeFold vocalFold_{};
    RateStableColor toneColor_{};
    PreparedPitchIntervals pitchIntervals_{};
    Settings settings_{};
    std::array<float,capacity> left_{},right_{},mono_{};
    std::array<float,2048> decimated_{};
    std::array<float,maxVoices+1> filtered_{},phase_{},ratio_{},targetRatio_{};
};
} // namespace armix::harmony
