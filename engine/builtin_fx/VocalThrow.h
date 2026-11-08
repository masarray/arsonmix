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
        freezeL_.assign(freezeFrames,0.f);
        freezeR_.assign(freezeFrames,0.f);
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
           edit.captureSamples==0||edit.captureSamples>freezeL_.size()||
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
        return freezeAt(ended?lastSoundEnd_:clock_);
    }
    void processStereo(float* left,float* right,std::size_t frames) noexcept {
        if(!ready_||!left||!right)return;
        const bool mono=left==right;
        for(std::size_t i=0;i<frames;++i) {
            const float dryL=std::isfinite(left[i])?left[i]:0.f;
            const float dryR=mono?dryL:(std::isfinite(right[i])?right[i]:0.f);
            const auto location=static_cast<std::size_t>(clock_%historyL_.size());
            historyL_[location]=dryL;historyR_[location]=dryR;
            ++clock_;recorded_=std::min(recorded_+1,historyL_.size());
            const float peak=std::max(std::abs(dryL),std::abs(dryR));
            // Fast attack, slow release. Singing detector is independent of
            // output echo, so feedback cannot self-trigger another phrase.
            const float coeff=peak>envelope_?envAttack_:envRelease_;
            envelope_=coeff*envelope_+(1.f-coeff)*peak;
            if(envelope_<1.0e-20f)envelope_=0.f;
            const bool singing=envelope_>=settings_.gateLinear;
            if(singing) {
                if(quiet_>static_cast<std::uint32_t>(rate_*.06))voiced_=0;
                quiet_=0;
                voiced_=std::min(voiced_+1,static_cast<std::uint32_t>(rate_));
                lastSoundEnd_=clock_;
                if(voiced_>=static_cast<std::uint32_t>(rate_*.08))armed_=true;
            } else {
                ++quiet_;
                if(armed_ && quiet_>=settings_.silenceSamples) {
                    if(settings_.settings.mode!=0 && clock_>=nextAutoEligible_) {
                        const bool captured=freezeAt(lastSoundEnd_);
                        if(captured)nextAutoEligible_=clock_+
                            static_cast<std::uint64_t>(rate_*.4);
                    }
                    armed_=false;
                }
            }
            if(copyPending_)copyFrozenChunk(); // fixed real-time budget
            float echoL=0.f,echoR=0.f;
            if(playing_) {
                // A new manual throw replaces the old tail atomically.
                // Each voice has a fixed offset in the captured buffer;
                // multiple phrases may overlap at short note divisions.
                for(int tap=0;tap<active_.settings.repeats;++tap) {
                    const std::uint64_t start=repeatStarts_[static_cast<std::size_t>(tap)];
                    if(clock_<=start)continue;
                    const std::uint64_t offset=clock_-start-1;
                    if(offset>=freezeCount_)continue;
                    float ramp=1.f;
                    const auto edge=static_cast<std::uint64_t>(rate_*.004);
                    if(edge>0) {
                        ramp=std::min(ramp,static_cast<float>(offset)/static_cast<float>(edge));
                        ramp=std::min(ramp,static_cast<float>(freezeCount_-offset-1)/
                                         static_cast<float>(edge));
                    }
                    const auto p=static_cast<std::size_t>(offset);
                    const float gain=repeatGains_[static_cast<std::size_t>(tap)]*std::max(0.f,ramp);
                    echoL+=freezeL_[p]*gain;echoR+=freezeR_[p]*gain;
                }
                const auto end=repeatStarts_[static_cast<std::size_t>(active_.settings.repeats-1)]
                                  +freezeCount_;
                if(clock_>end)playing_=false;
            }
            const float target=singing?1.f-settings_.settings.ducking:1.f;
            // Fixed 8-ms one-pole gain smoothing: no clicks on duck transitions.
            duck_=duck_*duckCoeff_+target*(1.f-duckCoeff_);
            left[i]=dryL+echoL*settings_.settings.wet*duck_;
            if(!mono)right[i]=dryR+echoR*settings_.settings.wet*duck_;
        }
    }
    [[nodiscard]] bool ready() const noexcept{return ready_;}
    [[nodiscard]] VocalThrowSettings settingsForTests() const noexcept{return settings_.settings;}
    [[nodiscard]] bool echoActive() const noexcept{return playing_;}
    [[nodiscard]] unsigned throwCount() const noexcept{return throwCount_;}
    [[nodiscard]] double sampleRate() const noexcept{return rate_;}
private:
    bool freezeAt(std::uint64_t end) noexcept {
        if(end==0||end>clock_||historyL_.empty())return false;
        const auto available=std::min<std::uint64_t>(end,recorded_);
        const auto count=std::min<std::uint64_t>(available,settings_.captureSamples);
        if(count<static_cast<std::uint64_t>(rate_*.025))return false;
        // No large memcpy in a 64/128-sample ASIO callback. Copy only a
        // fixed number of stereo frames per processed sample before playback.
        copyFrom_=end-count;copyEnd_=end;copyIndex_=0;
        freezeCount_=count;active_=settings_;
        playing_=false;copyPending_=true;++throwCount_;
        return true;
    }
    void copyFrozenChunk() noexcept {
        if(!copyPending_)return;
        for(int n=0;n<24 && copyIndex_<freezeCount_;++n) {
            const auto source=static_cast<std::size_t>(
                (copyFrom_+copyIndex_)%historyL_.size());
            const auto dest=static_cast<std::size_t>(copyIndex_++);
            freezeL_[dest]=historyL_[source];freezeR_[dest]=historyR_[source];
        }
        if(copyIndex_!=freezeCount_)return;
        copyPending_=false;
        // The phrase-end interval is authoritative. If capture completion
        // is later than the first repeat, begin on the next safe sample.
        const auto first=std::max(clock_,copyEnd_+active_.intervalSamples);
        float amplitude=1.f;
        for(int tap=0;tap<active_.settings.repeats;++tap) {
            const auto slot=static_cast<std::size_t>(tap);
            repeatStarts_[slot]=first+
                static_cast<std::uint64_t>(tap)*active_.intervalSamples;
            repeatGains_[slot]=amplitude;amplitude*=active_.settings.decay;
        }
        playing_=true;
    }
    void reset() noexcept {
        clock_=lastSoundEnd_=nextAutoEligible_=0;
        recorded_=freezeCount_=0;copyFrom_=copyEnd_=copyIndex_=0;
        voiced_=quiet_=0;armed_=playing_=copyPending_=false;
        envelope_=0.f;duck_=1.f;throwCount_=0;
        if(rate_>0) {
            envAttack_=static_cast<float>(std::exp(-1./(rate_*.004)));
            envRelease_=static_cast<float>(std::exp(-1./(rate_*.018)));
            duckCoeff_=static_cast<float>(std::exp(-1./(rate_*.008)));
        }
        repeatStarts_.fill(0);repeatGains_.fill(0.f);
        std::fill(historyL_.begin(),historyL_.end(),0.f);
        std::fill(historyR_.begin(),historyR_.end(),0.f);
    }
    PreparedEdit settings_{},active_{};
    std::vector<float> historyL_,historyR_,freezeL_,freezeR_;
    std::array<std::uint64_t,maxRepeats> repeatStarts_{};
    std::array<float,maxRepeats> repeatGains_{};
    std::uint64_t clock_=0,lastSoundEnd_=0,nextAutoEligible_=0;
    std::size_t recorded_=0;
    std::uint64_t freezeCount_=0,copyFrom_=0,copyEnd_=0,copyIndex_=0;
    std::uint32_t voiced_=0,quiet_=0;
    float envelope_=0.f,envAttack_=0.f,envRelease_=0.f,duckCoeff_=0.f,duck_=1.f;
    double rate_=0.;
    unsigned throwCount_=0;
    bool armed_=false,playing_=false,copyPending_=false,ready_=false;
};
} // namespace armix::builtin_fx
