// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// ArSon Vocal Throw. One audio-thread owner; preparation allocates on CONTROL.
// The microphone's dry PCM is never buffered or delayed. A rolling history
// freezes the last word/phrase for tempo-spaced stereo echoes. All samples and
// triggers run on the existing native ChannelId PCM callback, never WebView.
namespace armix::builtin_fx {
struct VocalThrowSettings final {
    int mode=2;                // 0 manual, 1 automatic, 2 hybrid
    float bpm=120.f;          // explicit/host or tap BPM (not claimed auto-detected)
    int division=0;           // 0 quarter, 1 eighth, 2 dotted eighth, 3 half
    int repeats=3;
    float captureMs=460.f;
    float silenceMs=220.f;
    float thresholdDb=-35.f;
    float wet=.42f;
    float decay=.55f;         // successive repeat amplitude multiplier
    float ducking=.7f;        // suppress echo while lead vocalist is active
};
class VocalThrow final {
public:
    static constexpr int addedLatencySamples=0;
    static constexpr int maxRepeats=6;
    static constexpr float maxCaptureMs=1500.f;
    struct PreparedEdit {
        VocalThrowSettings settings{};
        double sampleRate=0.;
        std::uint32_t intervalSamples=0, captureSamples=0, silenceSamples=0;
        float gateLinear=0.f;
        bool valid=false;
    };
    static bool validSettings(const VocalThrowSettings& s) noexcept {
        return s.mode>=0&&s.mode<=2&&s.division>=0&&s.division<=3&&
               s.repeats>=1&&s.repeats<=maxRepeats&&
               std::isfinite(s.bpm)&&s.bpm>=60.f&&s.bpm<=200.f&&
               std::isfinite(s.captureMs)&&s.captureMs>=80.f&&s.captureMs<=maxCaptureMs&&
               std::isfinite(s.silenceMs)&&s.silenceMs>=80.f&&s.silenceMs<=900.f&&
               std::isfinite(s.thresholdDb)&&s.thresholdDb>=-65.f&&s.thresholdDb<=-15.f&&
               std::isfinite(s.wet)&&s.wet>=0.f&&s.wet<=1.f&&
               std::isfinite(s.decay)&&s.decay>=.15f&&s.decay<=.9f&&
               std::isfinite(s.ducking)&&s.ducking>=0.f&&s.ducking<=1.f;
    }
    static bool prepareEdit(const VocalThrowSettings& s,double sampleRate,PreparedEdit& out) noexcept {
        if(!validSettings(s)||!std::isfinite(sampleRate)||
           sampleRate<8000.||sampleRate>192000.)return false;
        constexpr std::array<double,4> beats{{1.,.5,.75,2.}};
        PreparedEdit next{};
        next.settings=s;next.sampleRate=sampleRate;
        next.intervalSamples=std::max(1u,static_cast<std::uint32_t>(
            std::llround(60.*sampleRate*beats[static_cast<std::size_t>(s.division)]/s.bpm)));
        next.captureSamples=std::max(1u,static_cast<std::uint32_t>(
            std::llround(sampleRate*s.captureMs/1000.)));
        next.silenceSamples=std::max(1u,static_cast<std::uint32_t>(
            std::llround(sampleRate*s.silenceMs/1000.)));
        next.gateLinear=static_cast<float>(std::pow(10.,s.thresholdDb/20.));
        next.valid=next.gateLinear>0.f;
        out=next;return true;
    }
    // CONTROL thread only, before publishing the insert. Maximum memory is
    // bounded by the selected device rate and independent of song duration.
    bool prepare(double rate) {
        PreparedEdit packet{};
        if(!prepareEdit(VocalThrowSettings{},rate,packet))return false;
        const auto historyFrames=static_cast<std::size_t>(std::ceil(rate*2.5))+4;
        const auto freezeFrames=static_cast<std::size_t>(std::ceil(rate*maxCaptureMs/1000.))+4;
        if(historyFrames>500000||freezeFrames>300000)return false;
        historyL_.assign(historyFrames,0.f);
        historyR_.assign(historyFrames,0.f);
        // Two independent tails: new phrases never overwrite an active echo.
        for(auto& voice:voices_) {
            voice.freezeL.assign(freezeFrames,0.f);
            voice.freezeR.assign(freezeFrames,0.f);
        }
        rate_=rate;ready_=true;settings_=packet;reset();
        return true;
    }
    // CONTROL, before publication. No vector resizing during state restore.
    bool resetUnpublished(const VocalThrowSettings& settings) noexcept {
        PreparedEdit next{};
        if(!ready_||!prepareEdit(settings,rate_,next))return false;
        settings_=next;reset();return true;
    }
    // AUDIO: trivially copied, completely validated, no heap/locks.
    bool applyPrepared(const PreparedEdit& edit) noexcept {
        if(!ready_||!edit.valid||edit.sampleRate!=rate_||
           !validSettings(edit.settings)||edit.intervalSamples==0||
           edit.captureSamples==0||edit.captureSamples>voices_[0].freezeL.size()||
           edit.silenceSamples==0)return false;
        settings_=edit;return true;
    }
    // AUDIO: triggers the most recent word/phrase, preferring the latest
    // vocal end if the singer stopped within the last 700 ms.
    bool throwNow() noexcept {
        if(!ready_||recorded_==0||lastSoundEnd_==0||clock_<lastSoundEnd_)return false;
        const auto recent=static_cast<std::uint64_t>(rate_*.7);
        if(clock_-lastSoundEnd_>recent)return false; // reject stale/silent cues
        const bool ended=quiet_>0;
        const bool accepted=freezeAt(ended?lastSoundEnd_:clock_);
        if(accepted)manualInCurrentPhrase_=true;
        return accepted;
    }
    void processStereo(float* left,float* right,std::size_t frames) noexcept {
        if(!ready_||!left||!right)return;
        const bool mono=left==right;
        const std::uint32_t maxQuiet=static_cast<std::uint32_t>(rate_);
        const std::uint32_t phraseGap=static_cast<std::uint32_t>(rate_*.06);
        const std::uint32_t minVoice=static_cast<std::uint32_t>(rate_*.08);
        for(std::size_t i=0;i<frames;++i) {
            const float dryL=std::isfinite(left[i])?left[i]:0.f;
            const float dryR=mono?dryL:(std::isfinite(right[i])?right[i]:0.f);
            historyL_[writeIndex_]=dryL;historyR_[writeIndex_]=dryR;
            if(++writeIndex_==historyL_.size())writeIndex_=0;
            ++clock_;recorded_=std::min(recorded_+1,historyL_.size());
            const float peak=std::max(std::abs(dryL),std::abs(dryR));
            const float coeff=peak>envelope_?envAttack_:envRelease_;
            envelope_=coeff*envelope_+(1.f-coeff)*peak;
            if(envelope_<1.0e-20f)envelope_=0.f;
            const bool singing=envelope_>=settings_.gateLinear;
            if(singing) {
                if(quiet_>phraseGap) {
                    voiced_=0;
                    manualInCurrentPhrase_=false; // genuinely new sung phrase
                }
                quiet_=0;
                voiced_=std::min(voiced_+1,maxQuiet);
                lastSoundEnd_=clock_;
                if(voiced_>=minVoice)armed_=true;
            } else {
                if(quiet_<maxQuiet)++quiet_; // never wrap during multi-hour sessions
                if(armed_ && quiet_>=settings_.silenceSamples) {
                    if(settings_.settings.mode!=0 && !manualInCurrentPhrase_
                        && clock_>=nextAutoEligible_) {
                        if(freezeAt(lastSoundEnd_)) {
                            nextAutoEligible_=clock_+
                                static_cast<std::uint64_t>(rate_*.4);
                        }
                    }
                    armed_=false;
                }
            }
            float echoL=0.f,echoR=0.f;
            for(auto& voice:voices_) {
                if(voice.copying)copyFrozenChunk(voice);
                if(!voice.playing)continue;
                // Fixed six-tap limit, constant time; voice owns its capture
                // and scheduling packet even when BPM changes mid-tail.
                for(int tap=0;tap<voice.active.settings.repeats;++tap) {
                    const auto slot=static_cast<std::size_t>(tap);
                    const std::uint64_t start=voice.repeatStarts[slot];
                    if(clock_<=start)continue;
                    const std::uint64_t offset=clock_-start-1;
                    if(offset>=voice.freezeCount)continue;
                    float ramp=1.f;
                    if(voice.edgeSamples>0) {
                        ramp=std::min(ramp,static_cast<float>(offset)/
                                                   static_cast<float>(voice.edgeSamples));
                        ramp=std::min(ramp,static_cast<float>(voice.freezeCount-offset-1)/
                                                   static_cast<float>(voice.edgeSamples));
                    }
                    const auto p=static_cast<std::size_t>(offset);
                    const float gain=voice.repeatGains[slot]*std::max(0.f,ramp);
                    echoL+=voice.freezeL[p]*gain;echoR+=voice.freezeR[p]*gain;
                }
                const auto end=voice.repeatStarts[
                    static_cast<std::size_t>(voice.active.settings.repeats-1)]
                    +voice.freezeCount;
                if(clock_>end)voice.playing=false;
            }
            // Slew wet level and ducking independently to avoid zipper
            // noise on rapid GUI edits and the next vocal entrance.
            const float duckTarget=singing?1.f-settings_.settings.ducking:1.f;
            duck_=duck_*duckCoeff_+duckTarget*(1.f-duckCoeff_);
            wetSmoothed_=wetSmoothed_*duckCoeff_+
                settings_.settings.wet*(1.f-duckCoeff_);
            const float echoGain=wetSmoothed_*duck_;
            left[i]=dryL+echoL*echoGain;
            if(!mono)right[i]=dryR+echoR*echoGain;
        }
    }
    [[nodiscard]] bool ready() const noexcept{return ready_;}
    [[nodiscard]] VocalThrowSettings settingsForTests() const noexcept{return settings_.settings;}
    [[nodiscard]] bool echoActive() const noexcept{
        for(const auto& voice:voices_)if(voice.playing)return true;
        return false;
    }
    [[nodiscard]] unsigned throwCount() const noexcept{return throwCount_;}
    [[nodiscard]] double sampleRate() const noexcept{return rate_;}
private:
    struct TailVoice final {
        std::vector<float> freezeL,freezeR;
        std::array<std::uint64_t,maxRepeats> repeatStarts{};
        std::array<float,maxRepeats> repeatGains{};
        PreparedEdit active{};
        std::uint64_t copyEnd=0,freezeCount=0;
        std::size_t copyIndex=0,sourceIndex=0;
        std::uint32_t edgeSamples=0;
        bool copying=false,playing=false;
    };
    bool freezeAt(std::uint64_t end) noexcept {
        if(end==0||end>clock_||historyL_.empty())return false;
        // Bounded polyphony: do not cut or overwrite an existing phrase.
        // A full pool rejects a cue (and reports this via native ACK).
        TailVoice* slot=nullptr;
        for(auto& voice:voices_) {
            if(!voice.playing&&!voice.copying){slot=&voice;break;}
        }
        if(!slot)return false;
        const auto available=std::min<std::uint64_t>(end,recorded_);
        const auto count=std::min<std::uint64_t>(available,settings_.captureSamples);
        if(count<static_cast<std::uint64_t>(rate_*.025))return false;
        slot->copyEnd=end;
        slot->freezeCount=count;
        slot->copyIndex=0;
        slot->sourceIndex=static_cast<std::size_t>((end-count)%historyL_.size());
        slot->edgeSamples=static_cast<std::uint32_t>(rate_*.004);
        slot->active=settings_;
        slot->copying=true;
        ++throwCount_;
        return true;
    }
    void copyFrozenChunk(TailVoice& voice) noexcept {
        // A fixed 24-frame budget each sample. Contiguous bulk copying
        // replaces per-frame division/modulo in the former implementation.
        const std::size_t left=static_cast<std::size_t>(voice.freezeCount)-voice.copyIndex;
        const std::size_t n=std::min<std::size_t>({24,left,
                                        historyL_.size()-voice.sourceIndex});
        std::copy_n(historyL_.data()+voice.sourceIndex,n,
                    voice.freezeL.data()+voice.copyIndex);
        std::copy_n(historyR_.data()+voice.sourceIndex,n,
                    voice.freezeR.data()+voice.copyIndex);
        voice.copyIndex+=n;
        voice.sourceIndex+=n;
        if(voice.sourceIndex==historyL_.size())voice.sourceIndex=0;
        if(voice.copyIndex!=voice.freezeCount)return;
        voice.copying=false;
        const auto first=std::max(clock_,voice.copyEnd+voice.active.intervalSamples);
        float amplitude=1.f;
        for(int tap=0;tap<voice.active.settings.repeats;++tap) {
            const auto slot=static_cast<std::size_t>(tap);
            voice.repeatStarts[slot]=first+
                static_cast<std::uint64_t>(tap)*voice.active.intervalSamples;
            voice.repeatGains[slot]=amplitude;
            amplitude*=voice.active.settings.decay;
        }
        voice.playing=true;
    }
    void reset() noexcept {
        clock_=lastSoundEnd_=nextAutoEligible_=0;
        recorded_=writeIndex_=0;
        voiced_=quiet_=0;
        armed_=manualInCurrentPhrase_=false;
        envelope_=0.f;duck_=1.f;throwCount_=0;
        wetSmoothed_=settings_.settings.wet;
        if(rate_>0) {
            envAttack_=static_cast<float>(std::exp(-1./(rate_*.004)));
            envRelease_=static_cast<float>(std::exp(-1./(rate_*.018)));
            duckCoeff_=static_cast<float>(std::exp(-1./(rate_*.008)));
        }
        for(auto& voice:voices_) {
            voice.copyEnd=voice.freezeCount=0;
            voice.copyIndex=voice.sourceIndex=0;
            voice.edgeSamples=0;
            voice.copying=voice.playing=false;
            voice.repeatStarts.fill(0);voice.repeatGains.fill(0.f);
        }
        std::fill(historyL_.begin(),historyL_.end(),0.f);
        std::fill(historyR_.begin(),historyR_.end(),0.f);
    }
    PreparedEdit settings_{};
    std::vector<float> historyL_,historyR_;
    std::array<TailVoice,2> voices_{};
    std::uint64_t clock_=0,lastSoundEnd_=0,nextAutoEligible_=0;
    std::size_t recorded_=0,writeIndex_=0;
    std::uint32_t voiced_=0,quiet_=0;
    float envelope_=0.f,envAttack_=0.f,envRelease_=0.f,duckCoeff_=0.f;
    float duck_=1.f,wetSmoothed_=.42f;
    double rate_=0.;
    unsigned throwCount_=0;
    bool armed_=false,manualInCurrentPhrase_=false,ready_=false;
};
} // namespace armix::builtin_fx
