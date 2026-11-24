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
#include "HarmonyPeriodContinuity.h"
#include "HarmonyLeadTune.h"
#include "HarmonyVoicePlanner.h"
#include "HarmonyBackingHeadroom.h"

// Deterministic, allocation-free vocal harmony DSP. No JUCE or GUI dependency.
// Grain shifting is a functioning prototype, not formant-accurate synthesis.
namespace armix::harmony {
struct Settings {
    int key=0, scale=0, voices=3; // C=0..B=11; 0=major, 1=natural minor; 2..5 voices
    int voiceMode=0; // 0=pitched harmony (2-5), 1=true unison doubler (one wet take)
    bool correction=false;
    // Manual is always the trusted fallback. Music follows another accepted
    // ChannelId only after stable chroma confidence (never a PCM route).
    int guideMode=0; // 0=Manual Key, 1=Music Track
    std::uint64_t guideChannelId=0;
    float wet=0.45f, space=0.60f, color=0.50f, gainDb=0.f;
};
inline bool valid(const Settings& s) noexcept {
    return s.key>=0&&s.key<12&&s.scale>=0&&s.scale<=1&&
      s.voiceMode>=0&&s.voiceMode<=1&&
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
// Shared, delayed-source articulation guard. A broadband/high-band
// energy cue is NOT a phoneme classifier. It only attenuates new backing
// excitation during strong high-frequency consonant-like evidence.
// The lead, pitch tracker, epoch clocks and existing room tails stay intact.
class BackingArticulation final {
public:
    void prepare(double rate) noexcept {
        const double pole=std::exp(-2.*3.141592653589793*2200./rate);
        lowAlpha_=static_cast<float>(1.-pole);
        highNorm_=static_cast<float>((1.+pole)/(2.*pole));
        energyAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.003)));
        attack_=static_cast<float>(1.-std::exp(-1./(rate*.002)));
        release_=static_cast<float>(1.-std::exp(-1./(rate*.018)));
        low_=total_=high_=attenuation_=0.f;
    }
    float process(float x) noexcept {
        low_+=lowAlpha_*(x-low_);
        const float h=(x-low_)*highNorm_;
        total_+=energyAlpha_*(x*x-total_);
        high_+=energyAlpha_*(h*h-high_);
        // Avoid a division on normal vowels and silence. A high-frequency
        // dominant region receives up to 45% less NEW wet excitation.
        const float excess=high_-.42f*total_;
        const float target=total_>1.e-5f&&excess>0.f?
            .45f*std::min(1.f,excess/(.23f*total_)):0.f;
        attenuation_+=(target-attenuation_)*(target>attenuation_?attack_:release_);
        return 1.f-attenuation_;
    }
private:
    float lowAlpha_=0.f,highNorm_=1.f,energyAlpha_=0.f,attack_=0.f,release_=0.f;
    float low_=0.f,total_=0.f,high_=0.f,attenuation_=0.f;
};
// Bridge a discontinuous PSOLA residual to an always-advancing
// granular carrier. A short valid run is required before re-entry.
// No held absolute vocal sample, extra PCM history or output delay.
class BackingRenderSeam final {
public:
    struct Counts {std::uint64_t losses=0,acquisitions=0;};
    void prepare(double rate) noexcept {
        ramp_=std::max(1,static_cast<int>(std::lround(rate*.003)));
        warm_=std::max(1,static_cast<int>(std::lround(rate*.001)));
        tangentSamples_=std::max(4,static_cast<int>(std::lround(rate*.0005)));
        const float first=1.f-1.f/tangentSamples_;
        tangentNorm_=1.f/(first*first);
        peakDecay_=static_cast<float>(std::exp(-1./(rate*.010)));
        counts_={};reset();
    }
    // Preserve diagnostic counts across ordinary phrase resets.
    void reset() noexcept {
        run_=0;pos_=ramp_;active_=false;
        last_=start_=previous_=peak_=tangent_=0.f;
    }
    Counts countsForOfflineTests() const noexcept {return counts_;}
    float process(float carrier,float candidate,bool ready,float blend,
                  bool slopeAware=true) noexcept {
        if(ready){if(run_<warm_)++run_;}else run_=0;
        const bool active=run_>=warm_;
        if(active!=active_){
            start_=last_;pos_=0;active_=active;
            if(active)++counts_.acquisitions;else ++counts_.losses;
            // Match outgoing residual slope with a compact 0.5ms tangent.
            // Bound excursion by the recent envelope, not a global limiter.
            const float bound=peak_/(.20f*tangentSamples_);
            tangent_=slopeAware?std::clamp(last_-previous_,-bound,bound):0.f;
        }
        const float target=active_?blend*(candidate-carrier):0.f;
        previous_=last_;
        if(pos_<ramp_){
            const int n=pos_++;
            const float u=static_cast<float>(n)/ramp_;
            const float w=u*u*(3.f-2.f*u);
            last_=start_*(1.f-w)+target*w;
            if(slopeAware&&n+1<tangentSamples_&&tangent_!=0.f){
                const float k=static_cast<float>(n+1);
                const float z=1.f-k/tangentSamples_;
                last_+=tangent_*k*z*z*tangentNorm_;
            }
        }else last_=target;
        if(slopeAware)peak_=std::max(std::abs(last_),peak_*peakDecay_);
        return carrier+last_;
    }
private:
    int ramp_=1,warm_=1,run_=0,pos_=1,tangentSamples_=4;
    bool active_=false;
    float last_=0.f,start_=0.f,previous_=0.f,peak_=0.f,tangent_=0.f;
    float tangentNorm_=1.f,peakDecay_=0.f;
    Counts counts_{};
};
// Two independent fractional delay heads. Integrate delay velocity instead
// of multiplying a moving pitch ratio by grain age (which adds a spurious
// pitch term during a glide). Retarget only a zero-weight head. If F0 is
// trusted, choose a whole-period offset from the still-audible head to
// avoid destructive interference in the fallback crossfade.
// All state is bounded; no audio allocation, lookahead or new PCM history.
class PhaseCoherentCarrier final {
public:
    struct Delays {float a=0.f,b=0.f;};
    void reset() noexcept {initialized_=false;lastTime_=0;previous_=0.f;delays_={};}
    Delays process(std::uint64_t time,float phase,float ratio,float base,
                   int grain,float period,bool trusted,float maxDelay) noexcept {
        const float velocity=1.f-ratio;
        const float half=phase<.5f?phase+.5f:phase-.5f;
        // A full grain at worst permitted ratio cannot leave history.
        const float lo=2.f+1.01f*grain;
        const float hi=maxDelay-.51f*grain;
        const auto nominal=[&](float p){return std::clamp(base+velocity*p*grain,lo,hi);};
        if(!initialized_||time!=lastTime_+1){
            delays_={nominal(phase),nominal(half)};
            initialized_=true;
            // Align at initial acquisition too. Neither prior head is live.
            delays_.b=aligned(delays_.b,delays_.a,period,trusted,lo,hi);
        }else{
            delays_.a+=velocity;delays_.b+=velocity;
            // Phase wrap corresponds to the cubic window's zero endpoint.
            // At most one head resets per sample for all prepared grains.
            if(phase<previous_)
                delays_.a=aligned(nominal(phase),delays_.b,period,trusted,lo,hi);
            else if(previous_<.5f&&phase>=.5f)
                delays_.b=aligned(nominal(half),delays_.a,period,trusted,lo,hi);
        }
        previous_=phase;lastTime_=time;
        return delays_;
    }
private:
    static float aligned(float nominal,float other,float period,bool trusted,
                         float lo,float hi) noexcept {
        if(!trusted||!std::isfinite(period)||period<8.f||period>3200.f)return nominal;
        const float candidate=other+std::round((nominal-other)/period)*period;
        // Do not clamp a phase-aligned point into an unrelated phase.
        return candidate>=lo&&candidate<=hi?candidate:nominal;
    }
    bool initialized_=false;
    std::uint64_t lastTime_=0;
    float previous_=0.f;
    Delays delays_{};
};
// Slow note center removes sustained tuning error while keeping a bounded
// fraction of the microphone's faster pitch contour. Updated once per
// accepted pitch-analysis hop, not per sample. No lookahead or audio buffer.
class BackingPitchContour final {
public:
    void prepare(double hopSeconds) noexcept {
        alpha_=static_cast<float>(1.-std::exp(-hopSeconds/.120));
        reset();
    }
    void reset() noexcept {note_=-1;center_=0.f;}
    float correctionMidi(int note,float midi) noexcept {
        if(note_!=note){note_=note;center_=midi;}
        else center_+=alpha_*(midi-center_);
        // 65% of the fast excursion, max 35 cents. Static tuning and
        // accepted note changes retain the established target center.
        return midi-std::clamp(.65f*(midi-center_),-.35f,.35f);
    }
private:
    int note_=-1;
    float center_=0.f,alpha_=1.f;
};
class Engine final {
public:
    static constexpr int maxVoices=5;
    // Opt-in offline evidence only. All fields count sample calls, except
    // seam transitions which are recorded separately by BackingRenderSeam.
    struct RendererTrace {
        enum Field {calls,notCarrying,causalBlocked,drained,safeUnready,ready,partial,weakConfidence,epochRecovered,recoverySites,size};
        std::array<std::uint64_t,size> values{};
        RendererTrace delta(const RendererTrace& previous) const noexcept {
            RendererTrace out;
            for(std::size_t i=0;i<values.size();++i)out.values[i]=values[i]-previous.values[i];
            return out;
        }
    };
    static_assert(singers.size()==maxVoices);
    static constexpr int maxPitchLagsPerSlice=8; // fewer burst MACs per sample
    // Maximum 192 kHz grain is 3072 frames: 2*grain latency + 3*grain
    // history = 15360, so 16384 frames retain the full rate/ratio budget.
    // Halves the three large audio rings per instance without RT allocation.
    static constexpr std::size_t capacity=16384;
    // The legacy option is for offline A/B only; the native processor always
    // prepares a fixed live latency, immutable for its published graph lease.
    bool prepare(double rate,bool legacyLatencyForOfflineTests=false) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        rate_=rate;
        grain_=std::clamp(static_cast<int>(std::round(rate*0.016)),256,3072);
        legacyLatency_=legacyLatencyForOfflineTests;
        latency_=legacyLatency_?2*grain_:static_cast<int>(std::lround(rate*.024));
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
        doublerBlend_=targetDoubler_=doubleFiltered_=0.f;
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
        leadRendering_=false;leadSeam_.prepare(rate);voicePlanner_.reset();
        pitchSyncBlend_.fill(0.f);pitchSyncFiltered_.fill(0.f);
        pitchSyncValid_.fill(0.f);
        for(auto& seam:backingSeams_)seam.prepare(rate);
        continuityReferenceEnabled_=true;supportReferenceEnabled_=true;
        naturalReferenceEnabled_=true;backingContour_.prepare(detectEvery_/rate);
        carrierReferenceEnabled_=true;slopeReferenceEnabled_=true;drainReferenceEnabled_=true;epochRecoveryEnabled_=true;cubicReferenceEnabled_=false;
        traceEnabled_=false;rendererTrace_={};
        for(auto& carrier:carriers_)carrier.reset();
        periodContinuity_.reset();
        sourcePeriodSamples_=0.f;sourceConfidence_=0.f;sampleClock_=0;
        uncertainPitchHops_=0;weakSyncHops_=0;pitchSyncStable_=false;
        pitchSyncReferenceEnabled_=true;
        stageDepthReferenceEnabled_=true;
        roomFocusReferenceEnabled_=true;
        presenceReferenceEnabled_=true;unityReferenceEnabled_=true;
        expressionReferenceEnabled_=true;identityReferenceEnabled_=true;
        resonanceReferenceEnabled_=true;articulationReferenceEnabled_=true;
        fastCorrelation_=true;lowerFocusReferenceEnabled_=true;
        articulation_.prepare(rate);
        headroom_.prepare(rate);headroomEnabled_=true;
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
        chordRoot_=chordQuality_=-1;chordTransitionPending_=false;guidedVoicing_=false;
        voiced_=false;voicedGain_=0.f;renderersReleased_=true;
        settings_=Settings{};
        smoothingAlpha_=static_cast<float>(1.-std::exp(-1./(rate*0.005))); // 5 ms control ramp
        // Preserve the established 48k envelope while keeping durations
        // constant at 32-192k. Exponents computed off the callback.
        // Sustain a noisy, vibrato-heavy sung vowel without 2ms square-like
        // envelope edges; real silence is still detected immediately.
        voicedGateAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.008)));
        // A released pitch estimate must not gate the wet singer with the
        // same 8ms attack constant. 18ms release covers transient uncertainty
        // without sustaining unvoiced backing indefinitely. Dry lead is
        // unaffected; this is a release envelope, not a voice generator.
        voicedReleaseAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.018)));
        ratioAlpha_=static_cast<float>(1.-std::pow(1.-0.00045,48000./rate));
        processed_=false;settings_=Settings{};
        return setSettings(settings_);
    }
    // Prepared on the CONTROL thread. Immutable edit data is copied through
    // the existing SPSC; applying it on the callback needs only fixed copies.
    struct PreparedSettings {
        Settings settings{};
        float gain=1.f,backingGain=1.f;
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
    // Whole-stem compensation, not a lead duck or per-voice AGC. The
    // Static anchors fitted to matched dry-subtracted native PCM from
    // H1 CI run 45343447827 (44.1/48/96k, 220Hz dual-mono vowel fixture).
    // Cap at 2.5 (+7.96dB) instead of chasing exact RMS at deep Space.
    // Interpolate monotonically; do not boost quiet/consonant frames via AGC.
    // Leave factory Wet=.45 and below on the previous presence curve.
    static float backingUnityGain(float wet,float space,int mode,int voices) noexcept {
        constexpr std::array<std::array<float,3>,5> anchors{{
            {{1.83f,2.35f,2.49f}}, // Duo
            {{1.95f,2.38f,2.40f}},
            {{1.85f,2.42f,2.50f}},
            {{2.04f,2.50f,2.50f}},
            {{1.66f,2.50f,2.50f}}  // Doubler
        }};
        const auto& v=anchors[static_cast<std::size_t>(
            mode==1?4:std::clamp(voices,2,maxVoices)-2)];
        const float w=std::clamp((wet-.45f)/.55f,0.f,1.f);
        const float blend=w*w*(3.f-2.f*w);
        const float s=std::clamp(space,0.f,1.f);
        const float full=s<=.6f?v[0]+(v[1]-v[0])*(s/.6f):
                                  v[1]+(v[2]-v[1])*((s-.6f)/.4f);
        return 1.f+blend*(full-1.f);
    }
    // Preserve Duo's exact gains. Larger ensembles distribute the same
    // normalized power across five distinct positions; the HIGH singer
    // is a primary part, not another diffuse support layer.
    static float singerPresence(std::size_t v,int voices) noexcept {
        constexpr std::array<float,5> arranged{{1.30f,.80f,2.02f,1.22f,1.22f}};
        return voices<=2?singers[v].presence:arranged[v];
    }
    static float singerPan(std::size_t v,int voices) noexcept {
        constexpr std::array<float,5> arranged{{-.94f,-.17f,.95f,-.49f,.43f}};
        return voices<=2?singers[v].pan:arranged[v];
    }
    static bool prepareSettings(const Settings& next,PreparedSettings& out,
                                bool enhancedPresence=true,
                                bool unityPresence=true) noexcept {
        if(!valid(next))return false;
        PreparedSettings prepared;
        prepared.settings=next;
        prepared.gain=std::pow(10.f,next.gainDb/20.f);
        prepared.backingGain=enhancedPresence&&unityPresence?
            backingUnityGain(next.wet,next.space,next.voiceMode,next.voices):1.f;
        constexpr float pi=3.14159265358979323846f;
        if(next.voiceMode==1){
            // Real unison double, NOT 2 pitched harmony voices. One
            // independently windowed take follows source F0 at unity ratio.
            // A tiny prepared left-back offset plus the existing shared
            // room/detune/arrival give a second-take character.
            const float pan=-.40f*next.space;
            const float angle=(pan+1.f)*pi*.25f;
            const float amount=next.wet*(.77f+.38f*next.wet)*.74f;
            prepared.mixL[0]=amount*std::cos(angle);
            prepared.mixR[0]=amount*std::sin(angle);
            out=prepared;
            return true;
        }
        float profileEnergy=0.f;
        for(std::size_t v=0;v<singers.size();++v)
            if(singerActive(singers[v].role,next.voices)){
                const float presence=singerPresence(v,next.voices);
                profileEnergy+=presence*presence;
            }
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
            const float angle=(singerPan(v,next.voices)*stageWidth+1.f)*pi*0.25f;
            const float level=amplitude*singerPresence(v,next.voices);
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
            chordRoot_=chordQuality_=-1;chordTransitionPending_=true;guidedVoicing_=false;
        }
        if(settings_.correction&&!next.settings.correction)
            leadTune_.release();
        if((next.settings.voices>=3)!=(settings_.voices>=3)){
            // Mode boundary changes the arranger, even on a held note.
            tonalTransitionPending_=true;
        }
        if(next.settings.voiceMode!=settings_.voiceMode){
            // Re-evaluate note targets on the next accepted analysis,
            // without stopping the dry singer or reinitializing the room.
            tonalTransitionPending_=true;
            if(next.settings.voiceMode==1)targetRatio_.fill(1.f);
        }
        settings_=next.settings;
        targetWet_=next.settings.wet;targetColor_=next.settings.color;
        targetSpace_=next.settings.space;
        targetGain_=next.gain;targetBackingGain_=next.backingGain;
        targetEnsembleFocus_=next.settings.voiceMode==0&&next.settings.voices>=3?1.f:0.f;
        targetCorrection_=next.settings.correction?1.f:0.f;
        targetDoubler_=next.settings.voiceMode==1?1.f:0.f;
        for(int v=0;v<maxVoices;++v){
            targetMixL_[static_cast<std::size_t>(v)]=next.mixL[v];
            targetMixR_[static_cast<std::size_t>(v)]=next.mixR[v];
        }
        if(!processed_){
            wet_=targetWet_;color_=targetColor_;space_=targetSpace_;
            gain_=targetGain_;backingGain_=targetBackingGain_;
            ensembleFocus_=targetEnsembleFocus_;doublerBlend_=targetDoubler_;
            mixL_=targetMixL_;mixR_=targetMixR_;
        }
        return true;
    }
    // Control-thread lifecycle and standalone test entry only. The realtime
    // FX mailbox invokes applyPreparedSettings(), never this trig/pow path.
    bool setSettings(const Settings& next) noexcept {
        PreparedSettings prepared;
        return prepareSettings(next,prepared,presenceReferenceEnabled_,unityReferenceEnabled_)&&
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
    void setGuideChord(int root,int quality) noexcept {
        if(settings_.guideMode!=1||root<0||root>=12||quality<0||quality>1)
            root=quality=-1;
        if(root==chordRoot_&&quality==chordQuality_)return;
        chordRoot_=root;chordQuality_=quality;
        // Only backing arrangement changes; do not invalidate lead F0,
        // Tune, voiced gate or common-tone envelopes.
        if(settings_.voiceMode==0&&settings_.voices>=3)
            chordTransitionPending_=true;
    }
    bool guidedVoicingForOfflineTests() const noexcept{return guidedVoicing_;}
    bool setResonanceForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        resonanceReferenceEnabled_=enabled;return true;
    }
    // Exact H14 reference for matched acoustic regression only.
    // Exact H15 carrier reference; offline before the first PCM only.
    bool setRendererTraceForOfflineTests(bool enabled) noexcept {
        if(processed_)return false;
        traceEnabled_=enabled;rendererTrace_={};return true;
    }
    RendererTrace rendererTraceForOfflineTests(int voice) const noexcept {
        return voice>=0&&voice<maxVoices?rendererTrace_[static_cast<std::size_t>(voice)]:RendererTrace{};
    }
    // H22 experimental offline candidate: do not change the field
    // build's timbre until matched human-vocal listening qualifies it.
    bool setHeadroomForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        headroomEnabled_=enabled;return true;
    }
    const BackingHeadroom& headroomForOfflineTests()const noexcept{return headroom_;}
    bool setCubicForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        cubicReferenceEnabled_=enabled;return true;
    }
    bool setEpochRecoveryForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        epochRecoveryEnabled_=enabled;return true;
    }
    bool setDrainForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        drainReferenceEnabled_=enabled;return true;
    }
    bool setSlopeForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        slopeReferenceEnabled_=enabled;return true;
    }
    BackingRenderSeam::Counts seamCountsForOfflineTests(int voice) const noexcept {
        return voice>=0&&voice<maxVoices?
            backingSeams_[static_cast<std::size_t>(voice)].countsForOfflineTests():
            BackingRenderSeam::Counts{};
    }
    bool setCarrierForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        carrierReferenceEnabled_=enabled;return true;
    }
    bool setNaturalForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        naturalReferenceEnabled_=enabled;return true;
    }
    bool setSupportForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        supportReferenceEnabled_=enabled;return true;
    }
    bool setContinuityForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        continuityReferenceEnabled_=enabled;return true;
    }
    bool setLowerFocusForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        lowerFocusReferenceEnabled_=enabled;return true;
    }
    bool setArticulationForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        articulationReferenceEnabled_=enabled;return true;
    }
    // Test-only matched H9 accumulation order; not a persisted mode.
    bool setFastCorrelationForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        fastCorrelation_=enabled;return true;
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
    // Offline-only: exact previous presence curve, independent of legacy
    // pre-presence reference and the spatial/pitch A/B switches.
    bool setBackingUnityForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        unityReferenceEnabled_=enabled;
        return setSettings(settings_);
    }
    // Exact H3 expression baseline; only before any PCM, never a GUI setting.
    bool setIndependentExpressionForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        expressionReferenceEnabled_=enabled;return true;
    }
    // H4 exact-reference switch, test harness only (before PCM).
    bool setSingerIdentityForOfflineReference(bool enabled) noexcept {
        if(processed_)return false;
        identityReferenceEnabled_=enabled;return true;
    }
    int singerTargetForOfflineTests(int v) const noexcept {
        return voicePlanner_.target(static_cast<std::size_t>(v));
    }
    std::uint64_t singerAgeForOfflineTests(int v) const noexcept {
        return ensemble_.noteAgeForOfflineTests(v);
    }
    int observedMidiNote() const noexcept{return currentNote_;}
    bool voiced() const noexcept{return voiced_;}
    int peakPitchLagWork() const noexcept{return peakPitchLagWork_;}
    // Audio-owner telemetry; standalone CTest reads after process() returns.
    int completedPitchAnalyses() const noexcept{return completedPitchAnalyses_;}
    // Offline native fixture telemetry only: caller reads outside process().
    // No atomic copy or GUI poll in the realtime audio callback.
    float sourcePeriodForOfflineTests() const noexcept {
        return sourcePeriodSamples_;
    }
    float wetGateForOfflineTests() const noexcept {return voicedGain_;}
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
            backingGain_+=a*(targetBackingGain_-backingGain_);
            ensembleFocus_+=a*(targetEnsembleFocus_-ensembleFocus_);
            doublerBlend_+=a*(targetDoubler_-doublerBlend_);
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
            const float gateAlpha=voiced_?voicedGateAlpha_:voicedReleaseAlpha_;
            voicedGain_+=((voiced_?1.f:0.f)-voicedGain_)*gateAlpha;
            if(voiced_)renderersReleased_=false;
            // A note can lose F0 at an unvoiced consonant while its last
            // synthesized 18ms envelope remains audible. Do not erase
            // that singer's grain/period until the wet gain is effectively
            // silent: doing so switches to an unrelated granular phase
            // mid-release and sounds like a rapid micro-stutter.
            if(!voiced_&&!renderersReleased_&&voicedGain_<.001f){
                pitchSync_.reset();
                pitchSyncBlend_.fill(0.f);
                pitchSyncValid_.fill(0.f);
                pitchSyncFiltered_.fill(0.f);
                for(auto& seam:backingSeams_)seam.reset();
                periodContinuity_.reset();
                sourcePeriodSamples_=0.f;
                sourceConfidence_=0.f;
                renderersReleased_=true;
            }
            // Compute the device-rate-correct filter coefficient once per
            // active sample, not once for each backing shifter.
            const float filterCoef=(voicedGain_>0.00001f||doublerBlend_>0.00001f)?
                toneColor_.coefficient(color_):0.f;
            float leadL=dryL,leadR=dryR;
            renderLead(leadL,leadR);
            float outL=0.f,outR=0.f;
            // Direct sound and reflections from backing singers only.
            // Preserve the exact previous spatial gains in offline reference.
            const auto stageGain=BackingStageSpatial::gains(space_);
            // Wider ensembles need a little more direct-to-room ratio:
            // the full-Wet five-voice stem fell just below the H1 gate at
            // deep Space despite the bounded 2.5 bus gain. Restore up to
            // +1.58dB direct (not diffuse tail) without raising that cap.
            // Smooth the arranger boundary, and keep Duo/Doubler exact.
            const float backingDirect=stageDepthReferenceEnabled_?
                stageGain.direct*(1.f+.20f*space_*ensembleFocus_+
                                  .28f*space_*doublerBlend_):1.f-.38f*space_;
            const float earlyReturn=stageDepthReferenceEnabled_?
                stageGain.early:.28f*space_;
            const float independent=expressionReferenceEnabled_?ensembleFocus_:0.f;
            const float identity=identityReferenceEnabled_?ensembleFocus_:0.f;
            // Observe the same delayed source timeline as dry lead, not
            // the undelayed microphone (which would duck early by 24ms).
            // One detector for the ensemble; never modify dry or existing tails.
            const float articulation=articulationReferenceEnabled_?
                1.f+ensembleFocus_*(articulation_.process(
                    mono_[(write_+capacity-static_cast<std::size_t>(latency_))%capacity])-1.f):
                1.f;
            float roomSendL=0.f,roomSendR=0.f;
            for(int v=0;v<maxVoices;++v){
                const auto ix=static_cast<std::size_t>(v);
                const bool audible=voicedGain_>0.00001f&&
                    std::abs(mixL_[ix])+std::abs(mixR_[ix])>=1.e-5f;
                float voice=0.f;
                // Unison need not estimate or quantize pitch. Reuse the
                // same mono history, fixed PDC and singer stage. Fade the
                // mode boundary; only harmony needs the voiced gate/PSOLA.
                if(audible&&(v!=0||doublerBlend_<.99999f)){
                    voice=shifted(v,ensemble_.timbreCoefficient(v,filterCoef),
                                  ensemble_.arrivalSamples(v),
                                  ensemble_.pitchRatio(v,independent),independent)*voicedGain_;
                }
                if(v==0&&doublerBlend_>0.00001f){
                    const float sample=tap(mono_,latency_+ensemble_.doublerOffset());
                    doubleFiltered_+=ensemble_.timbreCoefficient(0,filterCoef)*
                        (sample-doubleFiltered_);
                    voice=voice*(1.f-doublerBlend_)+doubleFiltered_*doublerBlend_;
                }
                // Keep accepted Duo/Doubler exact; H5 only affects the
                // larger ensemble. Renderer readiness controls the guard,
                // not detector confidence alone. Process zero input too
                // so a released role cannot retain stale filter state.
                if(identity>0.f)
                    voice=ensemble_.shapeSinger(v,voice,identity,ratio_[ix],
                        1.f-pitchSyncBlend_[ix]*pitchSyncValid_[ix]);
                else
                    ensemble_.shapeSinger(v,0.f,0.f,1.f,0.f);
                voice=ensemble_.shadeBacking(v,voice*articulation,space_);
                const float reflection=ensemble_.exchangeReflection(v,voice);
                const float second=ensemble_.secondReflection(v);
                const float directL=voice*mixL_[ix],directR=voice*mixR_[ix];
                const auto placed=stage_.place(v,voice,reflection,second,
                                               mixL_[ix],mixR_[ix],space_);
                if(stageDepthReferenceEnabled_){
                    // Preserve HIGH audibility while lifting low register
                    // anchors in the direct field rather than muddying room.
                    const float roleFocus=lowerFocusReferenceEnabled_?
                        ((v==0||v==2)?.12f:v==3?.16f:0.f):0.f;
                    const float directPresence=BackingStageSpatial::
                        directionalPresence(v,space_)*(1.f+ensembleFocus_*roleFocus);
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
            // Compensate direct + early + late together, retaining DRR and
            // the original room excitation; lead remains independent.
            float protection=1.f;
            if(headroomEnabled_)
                protection=headroom_.process(inputL,inputR,leadL*gain_,leadR*gain_,
                    outL*backingGain_*gain_,outR*backingGain_*gain_,gain_,wet_);
            // Keep the previous multiply/add order when protection is unity.
            const float backing=backingGain_*protection;
            l[i]=std::clamp((leadL+outL*backing)*gain_,-4.f,4.f);
            r[i]=std::clamp((leadR+outR*backing)*gain_,-4.f,4.f);
            // Expression follows stable notes; confidence hysteresis is
            // handled by the tracker, not by vibrato resetting on one hop.
            ensemble_.advance(voiced_&&noteHold_>=2,voiced_);
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
            if(leadRendering_){leadShifter_.reset();leadSeam_.reset();}
            leadRendering_=false;leadBlend_=0.f;leadRatio_=1.f;
            return;
        }
        leadRendering_=true;
        leadRatio_+=ratioAlpha_*(target-leadRatio_);
        PitchSynchronousVoice<capacity,1>::StereoOutput tuned{};
        // At deep bass or a transient ratio, a short lookback must never
        // search a truncated future-side epoch. The existing residual seam
        // returns to the aligned dry lead; no dynamic host latency change.
        if(legacyLatency_||PitchSynchronousVoice<capacity,1>::completeEpochWindow(
                leadPeriod_,leadRatio_,static_cast<float>(latency_)))
            tuned=leadShifter_.processStereo(0,sampleClock_,write_,mono_,
                left_,right_,leadPeriod_,leadRatio_,static_cast<float>(latency_));
        leadBlend_+=smoothingAlpha_*((tuning&&tuned.ready?1.f:0.f)-leadBlend_);
        // Do not bypass the blend on a missing epoch. The seam bridges
        // only the correction residual; the live dry waveform continues.
        leadSeam_.process(left,right,tuned.left,tuned.right,
                          tuned.ready,leadBlend_);
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
                  float fineRatio=1.f,float independent=0.f) noexcept {
        float& currentRatio=ratio_[static_cast<std::size_t>(voice)];
        currentRatio+=(targetRatio_[static_cast<std::size_t>(voice)]-currentRatio)*
            ensemble_.ratioSmoothing(voice,ratioAlpha_,independent);
        const float effectiveRatio=std::clamp(currentRatio*fineRatio,.49f,2.01f);
        const float p=phase_[static_cast<std::size_t>(voice)];
        const float pB=p<0.5f?p+0.5f:p-0.5f;
        // Retain 16ms grain geometry and singer arrival offsets, but align
        // both the granular and PSOLA paths to the same declared lookback.
        float delayA=latency_+arrival+(1.f-effectiveRatio)*p*grain_;
        float delayB=latency_+arrival+(1.f-effectiveRatio)*pB*grain_;
        if(carrierReferenceEnabled_){
            const auto heads=carriers_[static_cast<std::size_t>(voice)].process(
                sampleClock_,p,effectiveRatio,static_cast<float>(latency_)+arrival,grain_,
                sourcePeriodSamples_,voiced_&&pitchSyncStable_&&sourceConfidence_>=.86f,
                static_cast<float>(capacity-3));
            delayA=heads.a;delayB=heads.b;
        }
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
        // Detector confidence controls the 30ms render BLEND, not the
        // renderer's clock. Continue advancing source epochs during weak
        // voiced evidence and the final wet tail; otherwise skipped samples
        // force a fresh epoch schedule when confidence returns and produce
        // an audible PSOLA/granular phase discontinuity.
        const bool carryEpochs=pitchSyncReferenceEnabled_&&
            sourcePeriodSamples_>=8.f&&(voiced_||voicedGain_>.01f);
        PitchSynchronousVoice<capacity,maxVoices>::Output sync{};
        // Conservative, independent vocal-tract proxy. Preserve Duo,
        // Doubler, lead Tune and granular fallback; suppress the shift
        // during weak periodic evidence and use the existing ensemble ramp.
        constexpr std::array<float,maxVoices> resonance{{.98f,1.035f,.94f,.95f,1.02f}};
        const float trust=std::clamp((sourceConfidence_-.79f)*10.f,0.f,1.f);
        const float scale=resonanceReferenceEnabled_?
            1.f+(resonance[ix]-1.f)*ensembleFocus_*trust:1.f;
        const bool windowSafe=carryEpochs&&(legacyLatency_||
            PitchSynchronousVoice<capacity,maxVoices>::completeEpochWindow(
                sourcePeriodSamples_,effectiveRatio,static_cast<float>(latency_)+arrival));
        // Admission needs causal lookback. Rendering an already validated
        // grain does not: its entire source window was proven at admission.
        const bool drain=drainReferenceEnabled_&&continuityReferenceEnabled_&&supportReferenceEnabled_;
        if(carryEpochs&&(windowSafe||drain))
            sync=pitchSync_.process(voice,sampleClock_,write_,mono_,
                sourcePeriodSamples_,effectiveRatio,
                static_cast<float>(latency_)+arrival,scale,continuityReferenceEnabled_,
                supportReferenceEnabled_,naturalReferenceEnabled_,windowSafe,epochRecoveryEnabled_,true,cubicReferenceEnabled_);
        if(traceEnabled_){
            auto& v=rendererTrace_[ix].values;
            ++v[RendererTrace::calls];
            if(!carryEpochs)++v[RendererTrace::notCarrying];
            else if(!windowSafe){
                ++v[RendererTrace::causalBlocked];
                if(sync.ready)++v[RendererTrace::drained];
            }else if(!sync.ready)++v[RendererTrace::safeUnready];
            if(sync.ready){
                ++v[RendererTrace::ready];
                if(sync.coverage<1.f)++v[RendererTrace::partial];
            }
            if(carryEpochs&&!confident)++v[RendererTrace::weakConfidence];
            if(sync.recovered)++v[RendererTrace::epochRecovered];
            v[RendererTrace::recoverySites]+=sync.recoverySites;
        }
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
        y=continuityReferenceEnabled_?backingSeams_[ix].process(f,colorState,sync.ready,
            blend*(supportReferenceEnabled_?sync.coverage:1.f),slopeReferenceEnabled_):
            f*(1.f-blend)+windowedSync*blend;
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
            // Convert frozen PCM to double only once per analysis window.
            // Independent accumulators break the serial FP dependency and
            // permit packed multiply/add without fast-math or ISA dispatch.
            // Same full 160 samples, lag coverage, normalization and budget.
            const double* x=pitchWindow_.data();
            const double* y=x+lag;
            double xy=0.;
            if(fastCorrelation_){
                double s0=0.,s1=0.,s2=0.,s3=0.;
                static_assert(span%4==0);
                for(int j=0;j<span;j+=4){
                    s0+=x[j]*y[j];s1+=x[j+1]*y[j+1];
                    s2+=x[j+2]*y[j+2];s3+=x[j+3]*y[j+3];
                }
                xy=(s0+s1)+(s2+s3);
            }else{
                for(int j=0;j<span;++j)xy+=x[j]*y[j];
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
        // This invalidates the detector immediately, but the AUDIO-owned
        // render tail retains sourcePeriodSamples_ and live overlap-add
        // grains until its wet envelope crosses the silent threshold.
        // A new note arriving during the release can reuse valid epochs.
        leadTune_.release();voicePlanner_.reset();backingContour_.reset();
    }
    // Keep the last known voiced note through at most five energetic but
    // ambiguous analysis windows (~33ms at 48k); true silence immediately
    // releases. Avoid ~150Hz note-gating during loud vowels and long vibrato.
    // Six inconclusive windows still release, never an indefinite noise hold.
    void uncertainPitchEvidence() noexcept {
        if(voiced_&&noteHold_>=2&&uncertainPitchHops_<5){
            ++uncertainPitchHops_;
            // Harmony tolerates brief ambiguous vowels, but lead tuning
            // must not apply stale correction through a consonant.
            if(uncertainPitchHops_>=2)leadTune_.release();
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
        // In DOUBLE the source's own melody is authoritative, not the key
        // wheel's diatonic quantization. Harmony keeps the existing scale.
        int note=settings_.voiceMode==1?
            static_cast<int>(std::lround(midi)):
            noteAt(degree,activeKey,activeScale);
        if(!tonalTransitionPending_ &&
           !pitchChangeHasMargin(currentNote_,note,midi)){
            note=currentNote_;
        }
        if(note!=candidateNote_){candidateNote_=note;noteHold_=1;}
        // Saturate the hold counter: a multi-hour sustained note must not
        // overflow a signed int in the realtime thread.
        else if(noteHold_<4)++noteHold_;
        // Two consistent windows for ordinary transitions; a sudden octave
        // (or wider interval) needs four independent analysis windows.
        // This avoids re-planning the entire backing choir for two noisy
        // high-harmonic windows in an otherwise sustained singer phrase.
        // do not generate provisional harmony on a single noisy transient.
        const float confidence=std::clamp(
            correlations[static_cast<std::size_t>(bestLag)],0.f,1.f);
        const int requiredNoteHops=currentNote_>=0&&
            std::abs(note-currentNote_)>=9?4:2;
        if(noteHold_<requiredNoteHops){
            if(!voiced_||note!=currentNote_)leadTune_.release();
            return;
        }
        const float newPeriod=static_cast<float>(rate_/hz);
        if(note!=currentNote_||tonalTransitionPending_||chordTransitionPending_){
            std::array<int,maxVoices> old{};
            for(std::size_t v=0;v<old.size();++v)old[v]=voicePlanner_.target(v);
            guidedVoicing_=false;
            if(settings_.voiceMode==0)
                guidedVoicing_=voicePlanner_.planGuided(note,activeKey,
                    activeScale==0?major:minor,settings_.voices,chordRoot_,chordQuality_);
            else voicePlanner_.reset();
            unsigned changed=0;
            for(std::size_t v=0;v<old.size();++v)
                if(!voiced_||old[v]!=voicePlanner_.target(v))changed|=1u<<v;
            ensemble_.beginNote(changed);
        }
        currentNote_=note;voiced_=true;tonalTransitionPending_=false;
        chordTransitionPending_=false;
        // Retain overlapping grains across even a real musical leap.
        // A brief octave readout caused by a loud second harmonic cannot
        // reset all five backing renderers and create a micro-dropout.
        sourcePeriodSamples_=periodContinuity_.observe(newPeriod);
        sourceConfidence_=confidence;
        if(settings_.correction){
            // Reuse the accepted F0 continuity policy, not an independent
            // raw-lag tracker. Keep the old valid period during release.
            leadPeriod_=sourcePeriodSamples_;
            if(periodContinuity_.transitioning()||
               std::abs(newPeriod-leadPeriod_)>.20f*leadPeriod_)
                leadTune_.release();
            else leadTune_.observe(midi,note,confidence);
        }
        // Hysteresis prevents marginal correlation confidence causing
        // rapid PSOLA/granular alternation. Silence disables it immediately.
        if(confidence>=.86f){
            pitchSyncStable_=true;weakSyncHops_=0;
        }else if(confidence<.79f){
            if(++weakSyncHops_>=3)pitchSyncStable_=false;
        }else weakSyncHops_=0;
        // Integer intervals come from the prepared LUT. The single
        // fractional ratio tracks the current microphone pitch.
        if(settings_.voiceMode==1){
            // Preserve the exact sung melody with only EnsembleSpace's
            // small individual cents/vibrato, no scale transposition.
            targetRatio_.fill(1.f);
        }else{
            const float contourMidi=backingContour_.correctionMidi(note,midi);
            const float baseRatio=std::exp2((note-(naturalReferenceEnabled_&&
                settings_.voices>=3?contourMidi:midi))/12.f);
            for(std::size_t v=0;v<singers.size();++v){
                const int target=voicePlanner_.target(v);
                targetRatio_[v]=target<0?1.f:std::clamp(
                    baseRatio*pitchIntervals_.ratio(target-note),.50f,2.f);
            }
        }
    }
    bool legacyLatency_=false;
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
    // Fixed 391 doubles, +1564 bytes vs the H9 float snapshot. The
    // decimated input itself is unchanged; no added audio delay.
    std::array<double,160+231> pitchWindow_{};
    bool fastCorrelation_=true,lowerFocusReferenceEnabled_=true;
    std::array<float,231> pitchCorrelations_{};
    int currentNote_=-1,candidateNote_=-1,noteHold_=0;
    int guideKey_=-1,guideScale_=-1;
    int chordRoot_=-1,chordQuality_=-1;
    bool chordTransitionPending_=false,guidedVoicing_=false;
    bool tonalTransitionPending_=false; // retune note, never mute voiced singer
    bool voiced_=false;float voicedGain_=0.f;
    bool renderersReleased_=true,processed_=false;
    float smoothingAlpha_=1.f,voicedGateAlpha_=0.002f;
    float voicedReleaseAlpha_=0.001f,ratioAlpha_=0.00045f;
    float wet_=0.f,targetWet_=0.f,color_=0.f,targetColor_=0.f;
    float sourcePeriodSamples_=0.f,sourceConfidence_=0.f,pitchSyncAlpha_=0.f;
    float pitchSyncValidAlpha_=0.f;
    int uncertainPitchHops_=0,weakSyncHops_=0;
    bool pitchSyncStable_=false;
    std::uint64_t sampleClock_=0;
    bool pitchSyncReferenceEnabled_=true,stageDepthReferenceEnabled_=true;
    bool roomFocusReferenceEnabled_=true;
    bool presenceReferenceEnabled_=true,unityReferenceEnabled_=true;
    bool expressionReferenceEnabled_=true,identityReferenceEnabled_=true;
    bool resonanceReferenceEnabled_=true,articulationReferenceEnabled_=true;
    BackingArticulation articulation_{};
    BackingHeadroom headroom_{};
    bool headroomEnabled_=true;
    std::array<BackingRenderSeam,maxVoices> backingSeams_{};
    bool continuityReferenceEnabled_=true,supportReferenceEnabled_=true;
    bool naturalReferenceEnabled_=true,carrierReferenceEnabled_=true,slopeReferenceEnabled_=true;
    bool drainReferenceEnabled_=true,traceEnabled_=false,epochRecoveryEnabled_=true,cubicReferenceEnabled_=false;
    std::array<RendererTrace,maxVoices> rendererTrace_{};
    BackingPitchContour backingContour_{};
    std::array<PhaseCoherentCarrier,maxVoices> carriers_{};
    std::array<float,maxVoices> pitchSyncBlend_{},pitchSyncFiltered_{},pitchSyncValid_{};
    float space_=0.f,targetSpace_=0.f;
    float gain_=1.f,targetGain_=1.f,targetCorrection_=0.f;
    float backingGain_=1.f,targetBackingGain_=1.f;
    float ensembleFocus_=0.f,targetEnsembleFocus_=0.f;
    float doublerBlend_=0.f,targetDoubler_=0.f,doubleFiltered_=0.f;
    std::array<float,maxVoices> mixL_{},mixR_{},targetMixL_{},targetMixR_{};
    float decimationLowpass_=0.f,decimationAlpha_=0.f;
    StereoPhaseSafeFold vocalFold_{};
    RateStableColor toneColor_{};
    PreparedPitchIntervals pitchIntervals_{};
    EnsembleSpace ensemble_{};
    BackingDepthRoom depthRoom_{};
    BackingStageSpatial stage_{};
    BackingRoomFocus roomFocus_{};
    PeriodContinuity periodContinuity_{};
    PitchSynchronousVoice<capacity,maxVoices> pitchSync_{};
    PitchSynchronousVoice<capacity,1> leadShifter_{};
    LeadTune leadTune_{};
    LeadRenderSeam leadSeam_{};
    VoicePlanner voicePlanner_{};
    bool leadRendering_=false;
    float leadRatio_=1.f,leadBlend_=0.f,leadPeriod_=0.f;
    Settings settings_{};
    std::array<float,capacity> left_{},right_{},mono_{};
    std::array<float,2048> decimated_{};
    std::array<float,maxVoices> filtered_{},phase_{},ratio_{},targetRatio_{};
};
} // namespace armix::harmony
