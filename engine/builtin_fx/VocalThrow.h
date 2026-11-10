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
    // AUDIO: incoming transport interval is already computed on CONTROL.
    // A zero interval explicitly falls back to accepted song/TAP BPM. Each
    // frozen tail keeps its interval when a backing track changes tempo.
    void setMusicIntervalSamples(std::uint32_t samples) noexcept {
        musicIntervalSamples_=samples;
        gridValid_=false; // BPM-only mode never claims a measured beat grid.
    }
    // AUDIO: the JUCE insert reads the one accepted ChannelId block clock
    // and control-prepared music onset anchor. Onsets are observed in 10ms
    // windows, so beat-phase is approximate (not sample-perfect detection).
    // The local->graph sample mapping is taken before every FX processing
    // block; it works even when the insert was added after playback began.
    void setMusicBeatGrid(std::uint32_t intervalSamples,
                          std::uint64_t anchorFrame,
                          std::uint64_t blockStartFrame,
                          bool locked) noexcept {
        musicIntervalSamples_=locked?intervalSamples:0;
        gridValid_=locked&&intervalSamples>0;
        gridAnchorFrame_=anchorFrame;
        gridBlockStartFrame_=blockStartFrame;
        gridLocalStartFrame_=clock_;
    }
    // AUDIO ONLY. A click DURING a syllable is a musical intent, not an
    // immediate splice. The singer finishes naturally; we freeze at the
    // verified end boundary. A second click cancels the queued intent.
    // An already completed phrase (last 700ms) is fired immediately.
    bool throwNow() noexcept {
        if(!ready_)return false;
        if(cueArmed_) {
            cueArmed_=cueSawVoice_=false; // deliberate performer cancellation
            return true;
        }
        const auto recent=static_cast<std::uint64_t>(rate_*.7);
        const auto gap=static_cast<std::uint32_t>(rate_*.06);
        const bool finished=lastSoundEnd_>0&&quiet_>=gap&&
            clock_>=lastSoundEnd_&&clock_-lastSoundEnd_<=recent;
        if(finished) {
            // AUTO/HYBRID has already consumed this phrase.
            if(lastSoundEnd_==autoTriggeredPhraseEnd_)return false;
            const bool fired=freezeAt(lastSoundEnd_);
            if(fired) {
                manualInCurrentPhrase_=true;
                armed_=false;
            }
            return fired;
        }
        // Even before the first microphone frame, arm ONE future phrase.
        // Never retain the cue past four seconds or a device/bypass reset.
        cueArmed_=true;
        cueSawVoice_=armed_ && lastSoundEnd_>0;
        cueDeadline_=clock_+static_cast<std::uint64_t>(rate_*4.);
        return true;
    }
    [[nodiscard]] bool cueArmed() const noexcept{return cueArmed_;}
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
            // Update ambient floor only when the vocal envelope is quiet.
            // Gate hysteresis avoids chattering at syllable tails; never
            // change the lead vocal dry PCM or demand calibration from user.
            const float gate=std::max(settings_.gateLinear,noiseFloor_*3.0f);
            const bool singing=envelope_>=gate*(quiet_==0?.75f:1.f);
            if(singing) {
                if(quiet_>phraseGap || lastWordStart_==0) {
                    lastWordStart_=clock_;
                    if(quiet_>phraseGap) {
                        voiced_=0;
                        manualInCurrentPhrase_=false;
                        autoTriggeredPhraseEnd_=0;
                    }
                }
                quiet_=0;
                voiced_=std::min(voiced_+1,maxQuiet);
                lastSoundEnd_=clock_;
                if(voiced_>=minVoice) {
                    armed_=true;
                    if(cueArmed_)cueSawVoice_=true;
                }
            } else {
                if(quiet_<maxQuiet)++quiet_; // never wrap during multi-hour sessions
                noiseFloor_=noiseFloor_*.9998f+peak*.0002f;
                if(armed_ && quiet_>=settings_.silenceSamples) {
                    // Manual queued cue gets priority over AUTO/HYBRID.
                    // Freeze the completed phrase, not the middle of a word.
                    if(cueArmed_ && cueSawVoice_) {
                        if(freezeAt(lastSoundEnd_))
                            manualInCurrentPhrase_=true;
                        cueArmed_=cueSawVoice_=false;
                    } else if(settings_.settings.mode!=0 && !manualInCurrentPhrase_
                        && clock_>=nextAutoEligible_) {
                        if(freezeAt(lastSoundEnd_)) {
                            autoTriggeredPhraseEnd_=lastSoundEnd_;
                            nextAutoEligible_=clock_+
                                static_cast<std::uint64_t>(rate_*.4);
                        }
                    }
                    armed_=false;
                }
            }
            if(cueArmed_ && clock_>=cueDeadline_)
                cueArmed_=cueSawVoice_=false; // bounded stale-cue expiry
            float echoL=0.f,echoR=0.f,activeTapGain=0.f;
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
                    // Ping-pong is wet-only. A true stereo source is folded
                    // to a stable wet mid, then positioned alternately L/R;
                    // the dry original is preserved sample-for-sample.
                    const float mid=.5f*(voice.freezeL[p]+voice.freezeR[p]);
                    auto& lowpass=voice.tapTone[slot];
                    const float alpha=.58f/(1.f+.22f*static_cast<float>(tap));
                    lowpass+=alpha*(mid-lowpass);
                    const float tone=(.75f-.07f*static_cast<float>(tap))*mid+
                        (.25f+.07f*static_cast<float>(tap))*lowpass;
                    // Tiny asymmetric early reflection = spatial depth;
                    // bounded reads only from the already frozen phrase,
                    // never a second realtime delay allocation or HRTF.
                    const auto reflectionOffset=voice.reflectionSamples;
                    const float reflection=p>=reflectionOffset?
                        .5f*(voice.freezeL[p-reflectionOffset]+
                            voice.freezeR[p-reflectionOffset]):0.f;
                    const float direct=tone*gain;
                    const float room=reflection*gain*.115f;
                    if((tap&1)==0) {
                        echoL+=direct*.93f;
                        echoR+=direct*.23f+room;
                    } else {
                        echoR+=direct*.93f;
                        echoL+=direct*.23f+room;
                    }
                    activeTapGain+=gain;
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
            // Concert-safe overlap headroom: many closely spaced taps,
            // including two phrases, must not multiply the wet peak by 6x.
            // No attenuation for ordinary nonoverlapping delays.
            const float echoGain=(wetSmoothed_*duck_)/
                std::max(1.f,activeTapGain*wetSmoothed_);
            left[i]=dryL+echoL*echoGain;
            if(!mono)right[i]=dryR+echoR*echoGain;
        }
    }
    // AUDIO only: discard historical vocalist PCM and pending echo tails
    // when the FX finishes bypassing or is re-enabled. Do not clear vector
    // storage here: 192kHz history could cost ~480,000 writes in one ASIO
    // callback. recorded_=0 fences stale samples until truly overwritten.
    // Retain the accepted settings, sample clock and completed cue counter.
    void invalidateCaptureHistory() noexcept {
        recorded_=0;
        lastSoundEnd_=nextAutoEligible_=autoTriggeredPhraseEnd_=0;
        lastWordStart_=cueDeadline_=0;
        voiced_=quiet_=0;
        armed_=manualInCurrentPhrase_=cueArmed_=cueSawVoice_=false;
        noiseFloor_=0.f;envelope_=0.f;
        for(auto& voice:voices_) {
            voice.copying=false;voice.playing=false;
            voice.freezeCount=0;voice.copyIndex=0;
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
        std::array<float,maxRepeats> tapTone{}; // voice-owned HF damping
        PreparedEdit active{};
        std::uint64_t copyEnd=0,freezeCount=0;
        std::size_t copyIndex=0,sourceIndex=0;
        std::uint32_t edgeSamples=0,reflectionSamples=0;
        std::uint64_t gridAnchorFrame=0,gridCueFrame=0,gridCueLocal=0;
        bool gridValid=false;
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
        auto count=std::min<std::uint64_t>(available,settings_.captureSamples);
        // Prefer the actual last voiced onset (including 28ms consonant
        // preroll) over an arbitrary fixed-size slice of the previous word.
        const auto pre=static_cast<std::uint64_t>(rate_*.028);
        const auto onset=lastWordStart_>pre?lastWordStart_-pre:0;
        if(onset>0&&onset<end)count=std::min(count,end-onset);
        if(count<static_cast<std::uint64_t>(rate_*.025))return false;
        slot->copyEnd=end;
        slot->freezeCount=count;
        slot->copyIndex=0;
        slot->sourceIndex=static_cast<std::size_t>((end-count)%historyL_.size());
        slot->edgeSamples=static_cast<std::uint32_t>(rate_*.004);
        slot->reflectionSamples=static_cast<std::uint32_t>(rate_*.011);
        slot->tapTone.fill(0.f);
        slot->active=settings_;
        if(musicIntervalSamples_)
            slot->active.intervalSamples=musicIntervalSamples_;
        // Freeze the same accepted beat grid as the captured phrase.
        // Later tempo changes, stale epochs or an unplugged MUSIC WIRE do
        // not re-time an already accepted echo tail.
        slot->gridValid=gridValid_;
        slot->gridAnchorFrame=gridAnchorFrame_;
        slot->gridCueFrame=gridBlockStartFrame_+
            (gridValid_?clock_-gridLocalStartFrame_:0);
        slot->gridCueLocal=clock_;
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
        std::uint64_t first=std::max(
            clock_,voice.copyEnd+voice.active.intervalSamples);
        if(voice.gridValid && voice.active.intervalSamples &&
           first>=voice.gridCueLocal) {
            const auto interval=voice.active.intervalSamples;
            const auto globalFirst=voice.gridCueFrame+
                (first-voice.gridCueLocal);
            // Quantize only the echo onset, never the singer's dry PCM.
            // Use unsigned arithmetic and bound grid correction to one
            // subdivision, protecting against a stale/future anchor.
            if(globalFirst>=voice.gridAnchorFrame) {
                const auto remainder=
                    (globalFirst-voice.gridAnchorFrame)%interval;
                // Never wait nearly another beat just to satisfy a stale
                // onset lattice. Small phase correction is musical; a full
                // extra delay is not. Upper bound = one quarter subdivision.
                if(remainder && interval-remainder<=interval/4)
                    first+=interval-remainder;
            } else if(voice.gridAnchorFrame-globalFirst<=interval) {
                first+=voice.gridAnchorFrame-globalFirst;
            }
        }
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
        clock_=lastSoundEnd_=nextAutoEligible_=autoTriggeredPhraseEnd_=0;
        lastWordStart_=cueDeadline_=0;
        recorded_=writeIndex_=0;
        voiced_=quiet_=0;
        armed_=manualInCurrentPhrase_=cueArmed_=cueSawVoice_=false;
        noiseFloor_=0.f;envelope_=0.f;duck_=1.f;throwCount_=0;
        wetSmoothed_=settings_.settings.wet;
        if(rate_>0) {
            envAttack_=static_cast<float>(std::exp(-1./(rate_*.004)));
            envRelease_=static_cast<float>(std::exp(-1./(rate_*.018)));
            duckCoeff_=static_cast<float>(std::exp(-1./(rate_*.008)));
        }
        for(auto& voice:voices_) {
            voice.copyEnd=voice.freezeCount=0;
            voice.copyIndex=voice.sourceIndex=0;
            voice.edgeSamples=voice.reflectionSamples=0;
            voice.tapTone.fill(0.f);
            voice.copying=voice.playing=false;
            voice.gridValid=false;
            voice.gridAnchorFrame=voice.gridCueFrame=voice.gridCueLocal=0;
            voice.repeatStarts.fill(0);voice.repeatGains.fill(0.f);
        }
        std::fill(historyL_.begin(),historyL_.end(),0.f);
        std::fill(historyR_.begin(),historyR_.end(),0.f);
    }
    PreparedEdit settings_{};
    std::vector<float> historyL_,historyR_;
    std::array<TailVoice,2> voices_{};
    std::uint64_t clock_=0,lastSoundEnd_=0,nextAutoEligible_=0,autoTriggeredPhraseEnd_=0;
    std::uint64_t lastWordStart_=0,cueDeadline_=0;
    std::size_t recorded_=0,writeIndex_=0;
    std::uint32_t voiced_=0,quiet_=0;
    float envelope_=0.f,envAttack_=0.f,envRelease_=0.f,duckCoeff_=0.f;
    float noiseFloor_=0.f;
    float duck_=1.f,wetSmoothed_=.42f;
    double rate_=0.;
    std::uint32_t musicIntervalSamples_=0;
    std::uint64_t gridAnchorFrame_=0,gridBlockStartFrame_=0,gridLocalStartFrame_=0;
    bool gridValid_=false;
    unsigned throwCount_=0;
    bool armed_=false,manualInCurrentPhrase_=false,ready_=false;
    bool cueArmed_=false,cueSawVoice_=false;
};
} // namespace armix::builtin_fx
