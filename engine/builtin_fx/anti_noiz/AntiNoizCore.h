// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <complex>
#include <numbers>
#include "AntiNoizFft.h"
#include "AntiNoizContext.h"
#include "AntiNoizGain.h"
#include "AntiNoizProfile.h"
#include "AntiNoizAutoGain.h"

namespace armix::builtin_fx::anti_noiz {
// Independent bounded WOLA reference, not a trained context classifier.
// All methods belong to the single DSP owner; prepare is off the audio thread.
class Core {
public:
    enum class Mode { AutoProtect, Voice, Singing, Music };
    // Host-provided source intent, not inferred from hardware or classified PCM.
    enum class SourceRole { Unknown, SpeechMic, SingingMic, Music, Mixed };
    struct Settings {
        Mode mode=Mode::AutoProtect;
        float cleanDb=14.f;
        float silenceDb=32.f;
        float musicProtect=1.f;
        bool enabled=true;
    };
    // Opaque validated packet: only Core can construct usable coefficients.
    // The control producer prepares it; the audio owner applies it atomically
    // at the existing command boundary without transcendental math.
    class PreparedSettings {
        friend class Core;
        Settings settings_{};
        float clean_=1,singing_=1,silence_=1,auto_=1;
        bool valid_=false;
    };
    static PreparedSettings prepareSettings(Settings s) noexcept {
        PreparedSettings p;
        if(!std::isfinite(s.cleanDb)||!std::isfinite(s.silenceDb)||s.cleanDb<0||s.cleanDb>24||
           s.silenceDb<0||s.silenceDb>48||!std::isfinite(s.musicProtect)||s.musicProtect<0||s.musicProtect>1||static_cast<unsigned>(s.mode)>3U)return p;
        p.settings_=s;
        p.clean_=gainDb(s.cleanDb);
        p.singing_=gainDb(std::min(12.f-6.f*s.musicProtect,s.cleanDb));
        p.silence_=gainDb(s.silenceDb);
        p.auto_=gainDb(std::min(12.f-4.f*s.musicProtect,s.cleanDb));
        p.valid_=true;return p;
    }
    static constexpr int maxN=FftPlan::maxN, maxBins=maxN/2+1;
    static Mode resolveMode(Mode requested,SourceRole source) noexcept {
        if(requested!=Mode::AutoProtect)return requested;
        switch(source){
            case SourceRole::SpeechMic:return Mode::Voice;
            case SourceRole::SingingMic:return Mode::Singing;
            case SourceRole::Music:return Mode::Music;
            default:return Mode::AutoProtect;
        }
    }
    Mode effectiveMode() const noexcept {return effectiveMode_;}
    SourceRole sourceRole() const noexcept {return sourceRole_;}
    // Source identity/intent is bound to the graph epoch; reprepare off RT
    // invalidates the noise profile. Never infer intent from a device name.
    bool prepare(double rate,int channels,SourceRole role=SourceRole::Unknown) noexcept {
        ready_=false;
        if(!std::isfinite(rate)||rate<44100.||rate>96000.||channels<1||channels>2||static_cast<unsigned>(role)>4U) return false;
        sourceRole_=role;effectiveMode_=resolveMode(settings_.mode,sourceRole_);
        rate_=rate; channels_=channels; n_=rate>48000.?512:256; hop_=n_/2;
        // Both supported FFT sizes are powers of two. The same ring index
        // can be formed without integer division in each WOLA sample loop.
        ringMask_=n_-1;
        for(int i=0;i<n_;++i) {
            win_[i]=std::sqrt(0.5f-0.5f*std::cos(2.f*std::numbers::pi_v<float>*float(i)/float(n_)));
        }
        fft_.prepare(n_);
        prepareTiming();
        context_.prepare(rate_,hop_);
        voiceGain_.prepare(rate_,hop_);
        for(auto& a:in_)a.fill(0);
        for(auto& a:out_)a.fill(0);
        profile_.prepare(rate_,hop_,channels_,hop_+1);
        mask_.fill(1); pos_=ticks_=0; expansion_=1;
        applyGains(prepareSettings(settings_));
        ready_=true; return true;
    }
    // Offline/direct-owner convenience API. Live Control uses prepared packets.
    bool setSettings(Settings s) noexcept {return applySettings(prepareSettings(s));}
    bool applySettings(const PreparedSettings& p) noexcept {
        if(!p.valid_)return false;
        const auto s=p.settings_;
        const auto next=resolveMode(s.mode,sourceRole_);
        if(!s.enabled||next==Mode::Music)profile_.cancel();
        // Extended Voice-only PSD authority must not leak into musical modes
        // or survive an explicit disable. Keep the learned anchor and modest
        // idle tracking. Existing overlap and gain smoothing remain intact.
        if(effectiveMode_==Mode::Voice&&(next!=Mode::Voice||(!s.enabled&&settings_.enabled)))
            profile_.limitToIdleBounds();
        if(next!=effectiveMode_||s.enabled!=settings_.enabled){voiceGain_.reset();context_.reset();profile_.track(power_,false);}
        effectiveMode_=next;
        settings_=s; applyGains(p); return true;
    }
    bool beginLearn() noexcept {
        if(!ready_||!profile_.begin())return false;
        context_.reset(); voiceGain_.reset(); return true;
    }
    // Quiescent/offline owner only: restart the signal timeline, keep trusted PSD.
    void resetStreamKeepProfile() noexcept {
        for(auto& a:in_)a.fill(0);
        for(auto& a:out_)a.fill(0);
        mask_.fill(1); expansion_=1;
        pos_=ticks_=0;
        profile_.cancel(); context_.reset(); voiceGain_.reset();
    }
    Settings settings() const noexcept {return settings_;}
    float spectralGain() const noexcept {return *std::min_element(mask_.begin(),mask_.begin()+hop_+1);}
    float residualGain() const noexcept {return expansion_;}
    bool ready() const noexcept {return ready_;}
    bool hasProfile() const noexcept {return profile_.valid();}
    bool learning() const noexcept {return profile_.learning();}
    int latencySamples() const noexcept {return ready_?n_:0;}
    // In-place mono/stereo, arbitrary partitions. Invalid layout leaves audio unchanged.
    void process(float* left,float* right,int samples) noexcept {
        if(!ready_||!left||(channels_==2&&(!right||left==right))||samples<=0)return;
        for(int i=0;i<samples;++i) {
            float* const p[2]={left,right};
            for(int c=0;c<channels_;++c) {
                const float x=p[c][i];
                in_[c][pos_]=std::isfinite(x)?std::clamp(x,-64.f,64.f):0.f;
                p[c][i]=out_[c][pos_];out_[c][pos_]=0.f;
            }
            if(++pos_==n_)pos_=0;
            if(++ticks_==hop_){ticks_=0; frame();}
        }
    }
    // Native host lifecycle only. Keep the qualified stationary DSP path,
    // coefficient preparation and all Core layout unchanged.
    void cancelNativeLearn() noexcept {
        if(profile_.learning()){profile_.cancel();context_.reset();voiceGain_.reset();}
    }
    bool lastNativeLearnQualified() const noexcept {return profile_.lastAttemptQualified();}
    void processNativeDry(float* left,float* right,int samples) noexcept {
        // AUDIO owns this transient override; no settings ACK, no pow,
        // no second delay. Existing disabled WOLA path advances the timeline.
        const bool enabled=settings_.enabled;
        settings_.enabled=false;
        process(left,right,samples);
        settings_.enabled=enabled;
    }
private:
    using Complex=std::complex<float>;
    static constexpr float eps=1e-16f;
    static float gainDb(float db) noexcept {return std::pow(10.f,-db/20.f);}
    void prepareTiming() noexcept {
        // Preserve the 48 kHz reference behavior, not hop-count-dependent timing.
        const double hops=double(hop_)*48000./(rate_*128.);
        maskClose_=float(1.-std::pow(1.-0.18,hops));
        expandClose_=float(1.-std::pow(1.-0.035,hops));
    }
    void applyGains(const PreparedSettings& p) noexcept {
        cleanFloor_=p.clean_;singingFloor_=p.singing_;
        silenceFloor_=p.silence_;autoFloor_=p.auto_;
    }
    void renderUnity() noexcept {
        // Algebraically equivalent unity WOLA, with the same delay and overlap.
        for(int c=0;c<channels_;++c)for(int i=0;i<n_;++i) {
            const int j=(pos_+i)&ringMask_;
            out_[c][j]+=in_[c][j]*win_[i]*win_[i];
        }
        mask_.fill(1); expansion_=1;
    }
    void frame() noexcept {
        if(!profile_.learning()&&(!profile_.valid()||!settings_.enabled||effectiveMode_==Mode::Music)) {
            context_.reset();profile_.track(power_,false);renderUnity(); return;
        }
        const int bins=hop_+1;
        for(int c=0;c<channels_;++c) {
            for(int i=0;i<n_;++i)work_[c][i]={in_[c][(pos_+i)&ringMask_]*win_[i],0};
            fft_.transform(work_[c],false);
            for(int k=0;k<bins;++k) {
                power_[c][k]=std::norm(work_[c][k]);
            }
        }
        const auto result=profile_.observe(power_);
        if(result!=NoiseProfile<maxBins>::LearnResult::waiting){context_.reset();voiceGain_.reset();}
        const bool protect=profile_.learning()||!profile_.valid()||!settings_.enabled||effectiveMode_==Mode::Music;
        const bool idle=context_.update(power_,profile_.anchor(),channels_,bins,!protect);
        // Only explicit Voice or source-aware SpeechMic permits independently
        // qualified coherent drift. Unknown/Mixed and musical policies keep
        // the original confirmed-idle tracking; anchor-based guards unchanged.
        profile_.track(power_,idle,!protect&&effectiveMode_==Mode::Voice,
                       context_.broadbandRatio(),context_.subbandPeakRatio());
        const bool selective=!protect&&!idle&&effectiveMode_==Mode::AutoProtect;
        const bool activeVoice=!protect&&!idle&&effectiveMode_==Mode::Voice;
        const float ratio=context_.broadbandRatio();
        const float floor=protect?1.f:idle||effectiveMode_==Mode::Voice?cleanFloor_:
            effectiveMode_==Mode::Singing?singingFloor_:AutoGain::activeFloor(autoFloor_,ratio,context_.subbandPeakRatio());
        // Uncertainty reserve is prepared once per frame. The band-local
        // budget avoids giving dominant learned noise a global dry path.
        const float reserve=activeVoice?AutoGain::detailReserve(ratio,.75f):
            selective?AutoGain::detailReserve(ratio,.25f+.55f*settings_.musicProtect):0.f;
        if(protect||idle)voiceGain_.reset();
        // Analysis remains live, but when both gains are exactly unity no
        // inverse FFT is needed. Preserve the context and profile histories.
        if(floor==1.f&&(!idle||silenceFloor_==1.f)){renderUnity();return;}
        for(int k=0;k<bins;++k) {
            float evidence=0;
            for(int c=0;c<channels_;++c)
                evidence=std::max(evidence,power_[c][k]/std::max(profile_.powers()[c][k],eps));
            // Voice-only uncertainty conditioning for noise-dominant bands.
            // Reuse the existing temporal neighborhood, immutable anchor and
            // stereo union; no extra FFT, delay, allocation or user parameter.
            if(!protect&&effectiveMode_==Mode::Voice&&evidence>1.f) {
                const float relative=context_.relativeNoise(profile_.anchor(),channels_,k);
                if(relative>1.5f)evidence=VoiceGain<maxBins>::conditionedEvidence(
                    evidence,context_.temporalRatio(profile_.anchor(),channels_,bins,k),relative);
            }
            // Only explicit Voice opts into the stronger causal estimator.
            const float gain=selective?AutoGain::target(floor,context_.temporalRatio(profile_.anchor(),channels_,bins,k),evidence):
                activeVoice?voiceGain_.estimate(k,evidence):
                std::sqrt(std::max(0.f,1.f-1.f/std::max(1.f,evidence)));
            const float localFloor=reserve>0.f?AutoGain::detailFloor(floor,reserve,
                context_.relativeNoise(profile_.anchor(),channels_,k)):floor;
            target_[k]=std::max(localFloor,gain);
        }
        for(int k=0;k<bins;++k) {
            const float smooth=0.25f*target_[std::max(0,k-1)]+0.5f*target_[k]+0.25f*target_[std::min(bins-1,k+1)];
            // A protected band must not be pulled down by neighboring bins.
            const float g=std::max(selective||reserve>0.f?target_[k]:floor,smooth);
            mask_[k]=g>=mask_[k]?g:mask_[k]+maskClose_*(g-mask_[k]);
        }
        const float expTarget=idle?silenceFloor_:1.f;
        // Immediate opening in the next overlap; slow closure. No hard gate.
        expansion_=expTarget>=expansion_?expTarget:expansion_+expandClose_*(expTarget-expansion_);
        for(int c=0;c<channels_;++c) {
            for(int k=0;k<bins;++k) {
                const float g=mask_[k]*expansion_;
                work_[c][k]*=g;
                if(k>0&&k<hop_)work_[c][n_-k]*=g;
            }
            fft_.transform(work_[c],true);
            for(int i=0;i<n_;++i)out_[c][(pos_+i)&ringMask_]+=work_[c][i].real()*win_[i];
        }
    }
    Settings settings_{};
    SourceRole sourceRole_=SourceRole::Unknown;
    Mode effectiveMode_=Mode::AutoProtect;
    double rate_=48000.;
    int channels_=0,n_=256,hop_=128,ringMask_=255,pos_=0,ticks_=0;
    bool ready_=false;
    float expansion_=1;
    std::array<float,maxN> win_{};
    FftPlan fft_{};
    ContextGuard<maxBins> context_{};
    VoiceGain<maxBins> voiceGain_{};
    float maskClose_=0.18f,expandClose_=0.035f;
    float cleanFloor_=1,singingFloor_=1,silenceFloor_=1,autoFloor_=1;
    std::array<std::array<float,maxN>,2> in_{},out_{};
    std::array<std::array<Complex,maxN>,2> work_{};
    NoiseProfile<maxBins> profile_{};
    std::array<std::array<float,maxBins>,2> power_{};
    std::array<float,maxBins> mask_{},target_{};
};
} // namespace armix::builtin_fx::anti_noiz
