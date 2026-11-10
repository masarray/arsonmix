// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "HarmonyStereoFold.h"
#include "HarmonyPreparedTone.h"
#include "HarmonyEnsembleSpace.h"
#include "HarmonyDepthRoom.h"
#include "HarmonyRoomFocus.h"
#include "HarmonyStageSpatial.h"
#include "HarmonyPitchSynchronous.h"
#include "HarmonyLeadTune.h"
#include "HarmonyVoicePlanner.h"

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
    static_assert(singers.size()==maxVoices);
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
        // Independent grain clocks avoid five identical backing-singer seams.
        for(int v=0;v<maxVoices;++v)
            phase_[static_cast<std::size_t>(v)]=.157f*static_cast<float>(v);
        // Low-pass before downsampling: raw 12 kHz decimation aliases vocal sibilants
        // into the periodicity detector and destabilizes the live note readback.
        decimationLowpass_=0.f;
        if(!vocalFold_.prepare(rate)||!toneColor_.prepare(rate)||
           !ensemble_.prepare(rate)||!depthRoom_.prepare(rate)||
            !stage_.prepare(rate)||!roomFocus_.prepare(rate))return false;
        pitchIntervals_.prepare();
        pitchSync_.reset();
        leadShifter_.reset();
        leadTune_.prepare(detectEvery_/rate);
        leadRatio_=1.f;leadBlend_=0.f;leadPeriod_=0.f;
        leadRendering_=false;voicePlanner_.reset();
        pitchSyncBlend_.fill(0.f);pitchSyncFiltered_.fill(0.f);
        pitchSyncValid_.fill(0.f);
        sourcePeriodSamples_=0.f;sourceConfidence_=0.f;sampleClock_=0;
        uncertainPitchHops_=0;weakSyncHops_=0;pitchSyncStable_=false;
        pitchSyncReferenceEnabled_=true;
        stageDepthReferenceEnabled_=true;
        roomFocusReferenceEnabled_=true;
        presenceReferenceEnabled_=true;
        // Both renderer choice and missing-epoch recovery are time-domain
        // crossfades. Never hard-switch their independent source phases.
        pitchSyncAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.030)));
        pitchSyncValidAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.005)));
        const float cutoff=1800.f;
        decimationAlpha_=static_cast<float>(1.-std::exp(-2.*3.141592653589793*cutoff/rate));
        write_=0;downWrite_=0;downSeen_=0;downCounter_=0;
        detectCounter_=0;pitchSliceCounter_=0;pitchNextLag_=0;
        pitchActive_=false;pitchBest_=-1.f;pitchBestLag_=0;
        peakPitchLagWork_=0;completedPitchAnalyses_=0;abandonedPitchAnalyses_=0;
        noteHold_=0;currentNote_=-1;candidateNote_=-1;
        guideKey_=-1;guideScale_=-1;tonalTransitionPending_=false;
        voiced_=false;voicedGain_=0.f;settings_=Settings{};
        smoothingAlpha_=static_cast<float>(1.-std::exp(-1./(rate*0.005))); // 5 ms control ramp
        // Preserve the established 48k envelope while keeping durations
        // constant at 32-192k. Exponents computed off the callback.
        // Sustain a noisy, vibrato-heavy sung vowel without 2ms square-like
        // envelope edges; real silence is still detected immediately.
        voicedGateAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.008)));
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
    // Wet-only musical presence, prepared OFF the audio callback.
    // 4-voice full Wet rises ~5 dB while dry lead and stage remain unchanged.
    static float normalizedBackingWetAmplitude(float wet,int voices,
                                               bool enhanced=true) noexcept {
        if(!std::isfinite(wet)||wet<=0.f||voices<2||voices>maxVoices)return 0.f;
        const float w=std::clamp(wet,0.f,1.f);
        const float presence=enhanced?(.77f+.38f*w):.65f;
        return w*presence/std::sqrt(static_cast<float>(voices));
    }
    static bool prepareSettings(const Settings& next,PreparedSettings& out,
                                bool enhancedPresence=true) noexcept {
        if(!valid(next))return false;
        PreparedSettings prepared;
        prepared.settings=next;
        prepared.gain=std::pow(10.f,next.gainDb/20.f);
        constexpr float pi=3.14159265358979323846f;
        float profileEnergy=0.f;
        for(const auto& singer:singers)
            if(singerActive(singer.role,next.voices))
                profileEnergy+=singer.presence*singer.presence;
        const float amplitude=normalizedBackingWetAmplitude(
            next.wet,next.voices,enhancedPresence)*
            std::sqrt(static_cast<float>(next.voices)/profileEnergy);
        for(std::size_t v=0;v<singers.size();++v){
            const auto& singer=singers[v];
            if(!singerActive(singer.role,next.voices))continue;
            // Space maps to perceptual stereo width: a 60% setting now
            // already gives recognizably separated LOW/HIGH singers.
            // The sqrt curve and constant-power gains are CONTROL-only;
            // Space=0 remains a single mono backing center.
            const float stageWidth=std::sqrt(next.space);
            const float angle=(singer.pan*stageWidth+1.f)*pi*0.25f;
            const float level=amplitude*singer.presence;
            prepared.mixL[v]=level*std::cos(angle);
            prepared.mixR[v]=level*std::sin(angle);
        }
        out=prepared;return true;
    }
    bool applyPreparedSettings(const PreparedSettings& next) noexcept {
        // Caller publishes only prepareSettings()-validated packets.
        // Discard only the TONAL candidate on a key/source edit. The singer's
        // current voiced envelope, source period and active grains continue.
        // Next completed pitch windows retune to the new scale, bypassing
        // old-key note hysteresis exactly until a new note is confirmed.
        if(next.settings.key!=settings_.key||next.settings.scale!=settings_.scale||
           next.settings.guideMode!=settings_.guideMode||
           next.settings.guideChannelId!=settings_.guideChannelId){
            candidateNote_=-1;noteHold_=0;tonalTransitionPending_=true;
            leadTune_.release();
        }
        if(next.settings.guideMode!=settings_.guideMode||
           next.settings.guideChannelId!=settings_.guideChannelId){
            guideKey_=-1;guideScale_=-1;
        }
        if(settings_.correction&&!next.settings.correction)
            leadTune_.release();
        settings_=next.settings;
        targetWet_=next.settings.wet;targetColor_=next.settings.color;
        targetSpace_=next.settings.space;
        targetGain_=next.gain;
        targetCorrection_=next.settings.correction?1.f:0.f;
        for(int v=0;v<maxVoices;++v){
            targetMixL_[static_cast<std::size_t>(v)]=next.mixL[v];
            targetMixR_[static_cast<std::size_t>(v)]=next.mixR[v];
        }
        if(!processed_){
            wet_=targetWet_;color_=targetColor_;space_=targetSpace_;
            gain_=targetGain_;
            mixL_=targetMixL_;mixR_=targetMixR_;
        }
        return true;
    }
    // Control-thread lifecycle and standalone test entry only. The realtime
    // FX mailbox invokes applyPreparedSettings(), never this trig/pow path.
    bool setSettings(const Settings& next) noexcept {
        PreparedSettings prepared;
        return prepareSettings(next,prepared,presenceReferenceEnabled_)&&
               applyPreparedSettings(prepared);
    }
    // Called only on the existing audio callback from a separately selected
    // source channel. Invalid/ambiguous music always restores Manual Key.
    void setGuideKey(int key,int scale) noexcept {
        if(settings_.guideMode!=1){key=-1;scale=-1;}
        if(key<0||key>=12||scale<0||scale>1){key=-1;scale=-1;}
        const int previousKey=effectiveKey(),previousScale=effectiveScale();
        guideKey_=key;guideScale_=scale;
        if(previousKey!=effectiveKey()||previousScale!=effectiveScale()){
            // A guide-key revision is not vocal silence. Preserve the
            // continuous sound and retune at the next stable pitch evidence.
            candidateNote_=-1;noteHold_=0;tonalTransitionPending_=true;
            leadTune_.release();
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
    // Offline A/B harness only: retain the exact legacy granular reference
    // without introducing a user-facing mode or changing serialized FX state.
    // Production instances always default to the pitch-synchronous candidate.
    void setPitchSynchronousCandidateForOfflineReference(bool enabled) noexcept {
        pitchSyncReferenceEnabled_=enabled;
    }
    // Offline A/B only. Default live audio always uses the new stage; no
    // ordinary GUI control, device transaction or saved setting is added.
    void setStageDepthForOfflineReference(bool enabled) noexcept {
        stageDepthReferenceEnabled_=enabled;
    }
    void setRoomFocusForOfflineReference(bool enabled) noexcept {
        roomFocusReferenceEnabled_=enabled;
    }
    // Offline-only before process(): same native pitch, stage, room and wet,
    // but old vs new normalized voice gain, for controlled vocal A/B.
    bool setBackingPresenceForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        presenceReferenceEnabled_=enabled;
        return setSettings(settings_);
    }
    int observedMidiNote() const noexcept{return currentNote_;}
    bool voiced() const noexcept{return voiced_;}
    int peakPitchLagWork() const noexcept{return peakPitchLagWork_;}
    // Audio-owner telemetry; standalone CTest reads after process() returns.
    int completedPitchAnalyses() const noexcept{return completedPitchAnalyses_;}
    int abandonedPitchAnalyses() const noexcept{return abandonedPitchAnalyses_;}
    // Wet changes are NEVER a hidden sidechain duck on the real lead. Only
    // explicitly enabled Pitch Tune may substitute corrected lead samples.
    static float leadPath(float original,float corrected,float correctionMix) noexcept {
        return original*(1.f-correctionMix)+corrected*correctionMix;
    }
    void process(float* l,float* r,int frames) noexcept {
        if(!l||!r||frames<=0||rate_<=0.)return;
        processed_=true;
        for(int i=0;i<frames;++i) {
            // The audio thread never calls trig or rebuilds panning geometry.
            // A 5 ms one-pole ramp suppresses automation/preset zipper clicks.
            const float a=smoothingAlpha_;
            wet_+=a*(targetWet_-wet_);
            color_+=a*(targetColor_-color_);
            space_+=a*(targetSpace_-space_);
            gain_+=a*(targetGain_-gain_);
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
            // active sample, not once for each backing shifter.
            const float filterCoef=voicedGain_>0.00001f?
                toneColor_.coefficient(color_):0.f;
            float outL=dryL,outR=dryR;
            renderLead(outL,outR);
            // Direct sound and reflections from backing singers only.
            // Preserve the exact previous spatial gains in offline reference.
            const auto stageGain=BackingStageSpatial::gains(space_);
            const float backingDirect=stageDepthReferenceEnabled_?
                stageGain.direct:1.f-.38f*space_;
            const float earlyReturn=stageDepthReferenceEnabled_?
                stageGain.early:.28f*space_;
            float roomSendL=0.f,roomSendR=0.f;
            for(int v=0;v<maxVoices;++v){
                const auto ix=static_cast<std::size_t>(v);
                const bool audible=voicedGain_>0.00001f&&
                    std::abs(mixL_[ix])+std::abs(mixR_[ix])>=1.e-5f;
                float voice=0.f;
                if(audible){
                    voice=shifted(v,ensemble_.timbreCoefficient(v,filterCoef),
                                  ensemble_.arrivalSamples(v),
                                  ensemble_.pitchRatio(v))*voicedGain_;
                }
                voice=ensemble_.shadeBacking(v,voice,space_);
                const float reflection=ensemble_.exchangeReflection(v,voice);
                const float second=ensemble_.secondReflection(v);
                const float directL=voice*mixL_[ix],directR=voice*mixR_[ix];
                const auto placed=stage_.place(v,voice,reflection,second,
                                               mixL_[ix],mixR_[ix],space_);
                if(stageDepthReferenceEnabled_){
                    const float directPresence=BackingStageSpatial::
                        directionalPresence(v,space_);
                    outL+=placed.directL*backingDirect*directPresence+
                          placed.earlyL*earlyReturn;
                    outR+=placed.directR*backingDirect*directPresence+
                          placed.earlyR*earlyReturn;
                    roomSendL+=placed.roomL;roomSendR+=placed.roomR;
                }else{
                    outL+=directL*backingDirect+
                          reflection*mixR_[ix]*earlyReturn;
                    outR+=directR*backingDirect+
                          reflection*mixL_[ix]*earlyReturn;
                    roomSendL+=directL;roomSendR+=directR;
                }
            }
            // One shared backing-only diffuse field. Both its input and
            // audible return ramp with Space. Wet=0 gates its old tail on
            // a smooth <=5 ms envelope while the room ring still decays.
            // Room-only low-cut and musical tail bloom; lead remains untouched.
            const float sendL=roomSendL*space_,sendR=roomSendR*space_;
            const auto cleaned=roomFocus_.filterSend(sendL,sendR);
            const auto room=depthRoom_.process(
                roomFocusReferenceEnabled_?cleaned.left:sendL,
                roomFocusReferenceEnabled_?cleaned.right:sendR);
            const float roomGate=std::min(1.f,wet_*4.f);
            const float lateReturn=(stageDepthReferenceEnabled_?
                stageGain.late:.85f*space_)*roomGate;
            const float focus=roomFocus_.returnGain(dryL,dryR,space_);
            const float roomReturn=lateReturn*(roomFocusReferenceEnabled_?focus:1.f);
            outL+=room.left*roomReturn;outR+=room.right*roomReturn;
            l[i]=std::clamp(outL*gain_,-4.f,4.f);
            r[i]=std::clamp(outR*gain_,-4.f,4.f);
            // Expression follows stable notes; confidence hysteresis is
            // handled by the tracker, not by vibrato resetting on one hop.
            ensemble_.advance(voiced_&&noteHold_>=2);
            stage_.advance();
            write_=(write_+1)%capacity;
            ++sampleClock_;
        }
    }
private:
    void renderLead(float& left,float& right) noexcept {
        const float target=leadTune_.ratio();
        const bool tuning=targetCorrection_>0.f&&std::abs(target-1.f)>.00015f;
        if(!tuning&&leadBlend_<.00001f){
            if(leadRendering_)leadShifter_.reset();
            leadRendering_=false;leadBlend_=0.f;leadRatio_=1.f;
            return;
        }
        leadRendering_=true;
        leadRatio_+=ratioAlpha_*(target-leadRatio_);
        const auto tuned=leadShifter_.processStereo(0,sampleClock_,write_,mono_,
            left_,right_,leadPeriod_,leadRatio_,static_cast<float>(latency_));
        leadBlend_+=smoothingAlpha_*((tuning&&tuned.ready?1.f:0.f)-leadBlend_);
        if(tuned.ready){
            left=leadPath(left,tuned.left,leadBlend_);
            right=leadPath(right,tuned.right,leadBlend_);
        }
    }
    float tap(const std::array<float,capacity>& a,float delay) const noexcept {
        const float d=std::clamp(delay,1.f,static_cast<float>(capacity-3));
        const int whole=static_cast<int>(d);
        const float f=d-whole;
        const auto a0=(write_+capacity-static_cast<std::size_t>(whole))%capacity;
        const auto a1=(a0+capacity-1)%capacity;
        return a[a0]*(1.f-f)+a[a1]*f;
    }
    float shifted(int voice,float filterCoef,float arrival=0.f,
                  float fineRatio=1.f) noexcept {
        float& currentRatio=ratio_[static_cast<std::size_t>(voice)];
        currentRatio+=(targetRatio_[static_cast<std::size_t>(voice)]-currentRatio)*ratioAlpha_;
        const float effectiveRatio=std::clamp(currentRatio*fineRatio,.49f,2.01f);
        const float p=phase_[static_cast<std::size_t>(voice)];
        const float pB=p<0.5f?p+0.5f:p-0.5f;
        const float delayA=2.f*grain_+arrival+(1.f-effectiveRatio)*p*grain_;
        const float delayB=2.f*grain_+arrival+(1.f-effectiveRatio)*pB*grain_;
        // Cubic smoothstep over the complementary two-grain windows.
        // Unlike the triangular seam, both endpoints have zero slope:
        // reduced grain-edge modulation without sin/cos in audio callback.
        const float w=smoothGrainBlend(p);
        float y=w*tap(mono_,delayA)+(1.f-w)*tap(mono_,delayB);
        const auto ix=static_cast<std::size_t>(voice);
        float& f=filtered_[ix];
        f+=filterCoef*(y-f);
        // Voiced, periodic sources can use causal pitch-synchronous
        // overlap-add. The exact source period is measured, not inferred
        // from the harmony target. Uncertain/nonperiodic sounds retain the
        // previous validated granular shifter with a bounded 12ms crossfade.
        // Avoid 150Hz switching between two synthesis algorithms.
        const bool confident=pitchSyncReferenceEnabled_&&voiced_&&pitchSyncStable_&&
                             sourcePeriodSamples_>=8.f;
        PitchSynchronousVoice<capacity,maxVoices>::Output sync{};
        if(confident)
            sync=pitchSync_.process(voice,sampleClock_,write_,mono_,
                sourcePeriodSamples_,effectiveRatio,
                static_cast<float>(latency_)+arrival);
        float& colorState=pitchSyncFiltered_[ix];
        // Invalid PSOLA epochs must never inject zero into the high-voice
        // output while a crossfade is still active.
        if(sync.ready)colorState+=filterCoef*(sync.sample-colorState);
        // A single missing source epoch must not hard-switch an already
        // voiced PSOLA singer to a phase-unrelated granular sample. Recover
        // gradually over 5ms, even as the overall algorithm blends over 30ms.
        float& valid=pitchSyncValid_[ix];
        valid+=pitchSyncValidAlpha_*((sync.ready?1.f:0.f)-valid);
        float& blend=pitchSyncBlend_[ix];
        blend+=pitchSyncAlpha_*((confident?1.f:0.f)-blend);
        const float windowedSync=colorState*valid+f*(1.f-valid);
        y=f*(1.f-blend)+windowedSync*blend;
        phase_[ix]+=1.f/static_cast<float>(grain_);
        if(phase_[ix]>=1.f)phase_[ix]-=1.f;
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
            releasePitch();
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
    void releasePitch() noexcept {
        sourceConfidence_=0.f;voiced_=false;noteHold_=0;
        candidateNote_=-1;currentNote_=-1;
        uncertainPitchHops_=0;weakSyncHops_=0;pitchSyncStable_=false;
        sourcePeriodSamples_=0.f;
        leadTune_.release();voicePlanner_.reset();
    }
    // Keep the last known voiced note through at most five energetic but
    // ambiguous analysis windows (~33ms at 48k); true silence immediately
    // releases. Avoid ~150Hz note-gating during loud vowels and long vibrato.
    // Six inconclusive windows still release, never an indefinite noise hold.
    void uncertainPitchEvidence() noexcept {
        if(voiced_&&noteHold_>=2&&uncertainPitchHops_<5){
            ++uncertainPitchHops_;
            return;
        }
        releasePitch();
    }
    void finishPitchAnalysis() noexcept {
        const auto& correlations=pitchCorrelations_;
        float best=pitchBest_;
        int bestLag=pitchBestLag_;
        // Prefer shortest strong period rather than an octave-multiple peak.
        if(best>0.77f){
            for(int lag=lowLag_;lag<=highLag_;++lag){
                const float peak=correlations[static_cast<std::size_t>(lag)];
                if(peak>=std::max(0.77f,best-0.035f)&&
                   (lag==lowLag_||peak>=correlations[static_cast<std::size_t>(lag-1)])&&
                   (lag==highLag_||peak>=correlations[static_cast<std::size_t>(lag+1)])){
                    bestLag=lag;break;
                }
            }
        }
        if(bestLag==0||best<=0.77f){
            uncertainPitchEvidence();
            return;
        }
        uncertainPitchHops_=0;
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
            releasePitch();
            return;
        }
        const int activeKey=effectiveKey(),activeScale=effectiveScale();
        const int degree=nearestDegree(midi,activeKey,activeScale);
        int note=noteAt(degree,activeKey,activeScale);
        if(!tonalTransitionPending_ &&
           !pitchChangeHasMargin(currentNote_,note,midi)){
            note=currentNote_;
        }
        if(note!=candidateNote_){candidateNote_=note;noteHold_=1;}
        // Saturate the hold counter: a multi-hour sustained note must not
        // overflow a signed int in the realtime thread.
        else if(noteHold_<3)++noteHold_;
        // Two consistent windows for both phrase onset and melodic transitions;
        // do not generate provisional harmony on a single noisy transient.
        const float confidence=std::clamp(
            correlations[static_cast<std::size_t>(bestLag)],0.f,1.f);
        if(noteHold_<2){
            leadTune_.release();
            return;
        }
        const float newPeriod=static_cast<float>(rate_/hz);
        if(settings_.correction){
            leadPeriod_=newPeriod;
            leadTune_.observe(midi,note,confidence);
        }
        if(note!=currentNote_||tonalTransitionPending_){
            voicePlanner_.plan(note,activeKey,activeScale==0?major:minor);
            ensemble_.beginNote();
        }
        currentNote_=note;voiced_=true;tonalTransitionPending_=false;
        if(sourcePeriodSamples_>0.f&&
           std::abs(newPeriod-sourcePeriodSamples_)>.25f*sourcePeriodSamples_){
            // A confirmed octave-size transition is a new periodicity.
            // Re-seed grains but let the 5/30ms fallback crossfades mask
            // this discontinuity instead of resetting output gains to zero.
            pitchSync_.reset();
            pitchSyncStable_=false;weakSyncHops_=0;
            sourcePeriodSamples_=newPeriod;
        }else if(sourcePeriodSamples_<=0.f){
            sourcePeriodSamples_=newPeriod;
        }else{
            // Six-millisecond F0 windows fluctuate on long singer vibrato.
            // Slew the source period at note-rate, not as discrete PSOLA
            // grain-window jumps. A 5% bounded hop still tracks a real
            // melodic transition promptly; target intervals remain separate.
            const float bounded=std::clamp(newPeriod,
                sourcePeriodSamples_*.95f,sourcePeriodSamples_*1.05f);
            sourcePeriodSamples_+=.30f*(bounded-sourcePeriodSamples_);
        }
        sourceConfidence_=confidence;
        // Hysteresis prevents marginal correlation confidence causing
        // rapid PSOLA/granular alternation. Silence disables it immediately.
        if(confidence>=.86f){
            pitchSyncStable_=true;weakSyncHops_=0;
        }else if(confidence<.79f){
            if(++weakSyncHops_>=3)pitchSyncStable_=false;
        }else weakSyncHops_=0;
        // Integer intervals come from the prepared LUT. The single
        // fractional ratio tracks the current microphone pitch.
        const float baseRatio=std::exp2((note-midi)/12.f);
        for(std::size_t v=0;v<singers.size();++v){
            const int target=voicePlanner_.target(v);
            targetRatio_[v]=target<0?1.f:std::clamp(
                baseRatio*pitchIntervals_.ratio(target-note),.50f,2.f);
        }
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
    bool tonalTransitionPending_=false; // retune note, never mute voiced singer
    bool voiced_=false;float voicedGain_=0.f;
    bool processed_=false;
    float smoothingAlpha_=1.f,voicedGateAlpha_=0.002f,ratioAlpha_=0.00045f;
    float wet_=0.f,targetWet_=0.f,color_=0.f,targetColor_=0.f;
    float sourcePeriodSamples_=0.f,sourceConfidence_=0.f,pitchSyncAlpha_=0.f;
    float pitchSyncValidAlpha_=0.f;
    int uncertainPitchHops_=0,weakSyncHops_=0;
    bool pitchSyncStable_=false;
    std::uint64_t sampleClock_=0;
    bool pitchSyncReferenceEnabled_=true,stageDepthReferenceEnabled_=true;
    bool roomFocusReferenceEnabled_=true;
    bool presenceReferenceEnabled_=true;
    std::array<float,maxVoices> pitchSyncBlend_{},pitchSyncFiltered_{},pitchSyncValid_{};
    float space_=0.f,targetSpace_=0.f;
    float gain_=1.f,targetGain_=1.f,targetCorrection_=0.f;
    std::array<float,maxVoices> mixL_{},mixR_{},targetMixL_{},targetMixR_{};
    float decimationLowpass_=0.f,decimationAlpha_=0.f;
    StereoPhaseSafeFold vocalFold_{};
    RateStableColor toneColor_{};
    PreparedPitchIntervals pitchIntervals_{};
    EnsembleSpace ensemble_{};
    BackingDepthRoom depthRoom_{};
    BackingStageSpatial stage_{};
    BackingRoomFocus roomFocus_{};
    PitchSynchronousVoice<capacity,maxVoices> pitchSync_{};
    PitchSynchronousVoice<capacity,1> leadShifter_{};
    LeadTune leadTune_{};
    VoicePlanner voicePlanner_{};
    bool leadRendering_=false;
    float leadRatio_=1.f,leadBlend_=0.f,leadPeriod_=0.f;
    Settings settings_{};
    std::array<float,capacity> left_{},right_{},mono_{};
    std::array<float,2048> decimated_{};
    std::array<float,maxVoices> filtered_{},phase_{},ratio_{},targetRatio_{};
};
} // namespace armix::harmony
