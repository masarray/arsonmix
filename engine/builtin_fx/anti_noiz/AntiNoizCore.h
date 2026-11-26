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
#include "AntiNoizTonal.h"
#include "AntiNoizWind.h"
#include "AntiNoizCoherence.h"
#include "AntiNoizBroadband.h"
#include "AntiNoizManualExpander.h"
#include "AntiNoizManualSpectral.h"

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
        bool spectralManual=false;
        float noiseOffsetDb=0;
        ManualExpander::Settings manual{};
        bool spectralEnabled=true;
        float artifactControl=.5f,vocalProtection=.5f;
    };
    // Opaque validated packet: only Core can construct usable coefficients.
    // The control producer prepares it; the audio owner applies it atomically
    // at the existing command boundary without transcendental math.
    class PreparedSettings {
        friend class Core;
        Settings settings_{};
        float clean_=1,singing_=1,silence_=1,auto_=1;
        ManualExpander::Prepared manual_{};
        float inverseNoiseScale_=1;
        bool valid_=false;
    };
    static PreparedSettings prepareSettings(Settings s) noexcept {
        PreparedSettings p;
        if(!std::isfinite(s.cleanDb)||!std::isfinite(s.silenceDb)||s.cleanDb<0||s.cleanDb>40||
           s.silenceDb<0||s.silenceDb>48||!std::isfinite(s.musicProtect)||s.musicProtect<0||s.musicProtect>1||static_cast<unsigned>(s.mode)>3U||
           !std::isfinite(s.noiseOffsetDb)||s.noiseOffsetDb< -20||s.noiseOffsetDb>20||!ManualExpander::valid(s.manual)||
           !std::isfinite(s.artifactControl)||s.artifactControl<0||s.artifactControl>1||
           !std::isfinite(s.vocalProtection)||s.vocalProtection<0||s.vocalProtection>1)return p;
        p.settings_=s;
        p.clean_=s.spectralEnabled?gainDb(s.cleanDb):1.f;
        p.singing_=s.spectralEnabled?gainDb(std::min(12.f-6.f*s.musicProtect,s.cleanDb)):1.f;
        p.silence_=gainDb(s.silenceDb);
        p.auto_=s.spectralEnabled?gainDb(std::min(12.f-4.f*s.musicProtect,s.cleanDb)):1.f;
        p.manual_=ManualExpander::prepareSettings(s.manual);
        p.inverseNoiseScale_=std::pow(10.f,-s.noiseOffsetDb/10.f);
        p.valid_=true;return p;
    }
    static bool validSettings(Settings s) noexcept {return prepareSettings(s).valid_;}
    static constexpr int maxN=FftPlan::maxN, maxBins=maxN/2+1;
    // Native observation only; fixed-size power bins, no PCM/FFT allocation.
    // Linear mean-square (one-sided window-energy normalized) per FFT bin.
    struct Spectrum {
        std::array<float,maxBins> input{},noise{};
        int bins=0;
        float binHz=0.f,progress=0.f;
        bool inputValid=false,noiseValid=false,candidate=false;
    };
    Spectrum spectrum() const noexcept {
        Spectrum out{};
        if(!ready_)return out;
        out.bins=hop_+1;out.binHz=float(rate_/n_);
        out.inputValid=powerValid_;
        const bool capturing=profile_.learning();
        const int captured=profile_.capturedFrames();
        out.candidate=capturing;
        out.noiseValid=capturing?captured>0:profile_.valid();
        if(capturing)out.progress=std::clamp(float(profile_.observedFrames())/float(std::max(1,profile_.targetFrames())),0.f,1.f);
        if(!out.inputValid&&!out.noiseValid)return out;
        const auto& noise=capturing?profile_.captured():profile_.anchor();
        const float normal=4.f/(float(n_)*float(n_)*float(channels_));
        const float average=capturing&&captured>0?1.f/float(captured):1.f;
        for(int k=0;k<out.bins;++k){
            const float scale=(k==0||k==hop_)?normal*.5f:normal;
            if(out.inputValid)out.input[k]=(power_[0][k]+(channels_==2?power_[1][k]:0.f))*scale;
            if(out.noiseValid)out.noise[k]=(noise[0][k]+(channels_==2?noise[1][k]:0.f))*scale*average;
        }
        return out;
    }
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
        manual_.prepare(rate_,channels_,n_);manualActive_=false;
        wind_.prepare(rate_,hop_,n_,channels_);
        coherence_.prepare(rate_,hop_,channels_);
        broadband_.prepare(rate_,hop_,n_,channels_);
        voiceGain_.prepare(rate_,hop_);
        tonal_.prepare(rate_,channels_);tonalIdle_=false;
        for(auto& a:in_)a.fill(0);
        for(auto& a:out_)a.fill(0);
        profile_.prepare(rate_,hop_,channels_,hop_+1);
        cancelAudition();mask_.fill(1); pos_=ticks_=0; expansion_=1;powerValid_=false;
        applyGains(prepareSettings(settings_));
        ready_=true; return true;
    }
    // Offline/direct-owner convenience API. Live Control uses prepared packets.
    bool setSettings(Settings s) noexcept {return applySettings(prepareSettings(s));}
    bool applySettings(const PreparedSettings& p) noexcept {
        if(!p.valid_)return false;
        cancelAudition();
        const auto s=p.settings_;
        const auto next=resolveMode(s.mode,sourceRole_);
        if(!s.enabled||next==Mode::Music)profile_.cancel();
        // Extended Voice-only PSD authority must not leak into musical modes
        // or survive an explicit disable. Keep the learned anchor and modest
        // idle tracking. Existing overlap and gain smoothing remain intact.
        if(effectiveMode_==Mode::Voice&&(next!=Mode::Voice||(!s.enabled&&settings_.enabled)))
            profile_.limitToIdleBounds();
        if(next!=effectiveMode_||s.enabled!=settings_.enabled||s.cleanDb==0.f||settings_.cleanDb==0.f||s.spectralEnabled!=settings_.spectralEnabled||s.spectralManual!=settings_.spectralManual){tonal_.resetSignal();tonalIdle_=false;}
        if(next!=effectiveMode_||s.enabled!=settings_.enabled){voiceGain_.reset();context_.reset();wind_.reset();coherence_.reset();broadband_.reset();profile_.track(power_,false);}
        if(s.spectralManual!=settings_.spectralManual||s.noiseOffsetDb!=settings_.noiseOffsetDb)voiceGain_.reset();
        if(next!=effectiveMode_||s.enabled!=settings_.enabled){manual_.reset();manualActive_=false;}
        effectiveMode_=next;
        settings_=s; applyGains(p); return true;
    }
    bool beginLearn() noexcept {
        if(!ready_||!profile_.begin())return false;
        cancelAudition();manual_.reset();manualActive_=false;
        context_.reset(); wind_.reset();coherence_.reset();broadband_.reset(); voiceGain_.reset(); tonal_.begin();tonalIdle_=false;powerValid_=false; return true;
    }
    // Audio-owned transient commands; preserve WOLA timeline on reset.
    bool clearProfile() noexcept {
        if(!ready_)return false;
        cancelAudition();profile_.clear();tonal_.clear();tonalIdle_=false;
        context_.reset();wind_.reset();coherence_.reset();broadband_.reset();voiceGain_.reset();powerValid_=false;
        return true;
    }
    bool audition() noexcept {
        if(auditionActive()){auditionRemaining_=0;return true;}
        if(!ready_||!settings_.enabled||!settings_.spectralEnabled||!settings_.spectralManual||settings_.cleanDb<=0||
           !profile_.valid()||profile_.learning()||effectiveMode_==Mode::Music||(!settings_.manual.enabled&&settings_.silenceDb!=0))return false;
        auditionRemaining_=int(rate_*2.);return true;
    }
    bool auditionActive() const noexcept {return auditionRemaining_>0||auditionMix_>0.f;}
    void cancelAudition() noexcept {auditionRemaining_=0;auditionMix_=0;}
    // Quiescent/offline owner only: restart the signal timeline, keep trusted PSD.
    void resetStreamKeepProfile() noexcept {
        for(auto& a:in_)a.fill(0);
        for(auto& a:out_)a.fill(0);
        cancelAudition();mask_.fill(1); expansion_=1;manual_.reset();manualActive_=false;
        pos_=ticks_=0;powerValid_=false;
        profile_.cancel(); context_.reset(); wind_.reset();coherence_.reset();broadband_.reset(); voiceGain_.reset(); tonal_.resetSignal();tonalIdle_=false;
    }
    Settings settings() const noexcept {return settings_;}
    float spectralGain() const noexcept {return *std::min_element(mask_.begin(),mask_.begin()+hop_+1);}
    float residualGain() const noexcept {return manualActive_?manual_.gain():expansion_;}
    float detectorLevel() const noexcept {return manualActive_?manual_.detector():0.f;}
    bool ready() const noexcept {return ready_;}
    bool hasProfile() const noexcept {return profile_.valid();}
    bool learning() const noexcept {return profile_.learning();}
    int latencySamples() const noexcept {return ready_?n_:0;}
    // In-place mono/stereo, arbitrary partitions. Invalid layout leaves audio unchanged.
    void process(float* left,float* right,int samples) noexcept {
        if(!ready_||!left||(channels_==2&&(!right||left==right))||samples<=0)return;
        for(int i=0;i<samples;++i) {
            float* const p[2]={left,right};
            const bool preview=auditionActive();
            float dry[2]={};
            for(int c=0;c<channels_;++c) {
                const float x=p[c][i];
                if(preview)dry[c]=in_[c][pos_];
                in_[c][pos_]=std::isfinite(x)?std::clamp(x,-64.f,64.f):0.f;
                p[c][i]=out_[c][pos_];out_[c][pos_]=0.f;
            }
            if(profile_.learning())tonal_.observe(in_[0][pos_],channels_==2?in_[1][pos_]:0.f);
            else if(settings_.enabled&&!settings_.spectralManual&&effectiveMode_==Mode::Voice&&cleanFloor_<1.f&&tonal_.selected()>=0){
                tonal_.track(in_[0][pos_],channels_==2?in_[1][pos_]:0.f,tonalIdle_);
                // Apply to the already latency-aligned WOLA output.
                float unused=0.f;
                tonal_.apply(left[i],channels_==2?right[i]:unused,1.f-cleanFloor_);
            }
            if(preview)for(int c=0;c<channels_;++c)dry[c]-=p[c][i];
            const bool useManual=settings_.enabled&&settings_.manual.enabled&&effectiveMode_!=Mode::Music&&!profile_.learning();
            if(useManual){
                const float gain=manual_.step(in_[0][pos_],channels_==2?in_[1][pos_]:0.f);
                left[i]*=gain;if(channels_==2)right[i]*=gain;
            }else if(manualActive_)manual_.reset();
            manualActive_=useManual;
            if(preview){
                const float target=auditionRemaining_>0?1.f:0.f,step=200.f/float(rate_);
                auditionMix_=target>auditionMix_?std::min(target,auditionMix_+step):std::max(target,auditionMix_-step);
                for(int c=0;c<channels_;++c)p[c][i]+=auditionMix_*(dry[c]-p[c][i]);
                if(auditionRemaining_>0)--auditionRemaining_;
            }
            if(++pos_==n_)pos_=0;
            if(++ticks_==hop_){ticks_=0; frame();}
        }
    }
    // Native host lifecycle only. Keep the qualified stationary DSP path,
    // coefficient preparation and signal arithmetic unchanged.
    void cancelNativeLearn() noexcept {
        if(profile_.learning()){profile_.cancel();context_.reset();wind_.reset();coherence_.reset();broadband_.reset();voiceGain_.reset();tonal_.resetSignal();tonalIdle_=false;}
    }
    bool lastNativeLearnQualified() const noexcept {return profile_.lastAttemptQualified();}
    void processNativeDry(float* left,float* right,int samples) noexcept {
        // AUDIO owns this transient override; no settings ACK, no pow,
        // no second delay. Existing disabled WOLA path advances the timeline.
        const bool enabled=settings_.enabled;
        cancelAudition();settings_.enabled=false;
        tonal_.resetSignal();tonalIdle_=false;
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
        voiceExpandClose_=float(1.-std::exp(-double(hop_)/(rate_*.028)));
    }
    void applyGains(const PreparedSettings& p) noexcept {
        cleanFloor_=p.clean_;singingFloor_=p.singing_;
        silenceFloor_=p.silence_;autoFloor_=p.auto_;manual_.apply(p.manual_);inverseNoiseScale_=p.inverseNoiseScale_;
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
            powerValid_=false;context_.reset();wind_.reset();coherence_.reset();broadband_.reset();tonalIdle_=false;profile_.track(power_,false);renderUnity(); return;
        }
        const int bins=hop_+1;
        for(int c=0;c<channels_;++c) {
            for(int i=0;i<n_;++i)work_[c][i]={in_[c][(pos_+i)&ringMask_]*win_[i],0};
            fft_.transform(work_[c],false);
            for(int k=0;k<bins;++k) {
                power_[c][k]=std::norm(work_[c][k]);
            }
        }
        powerValid_=true;
        const auto result=profile_.observe(power_);
        if(result==NoiseProfile<maxBins>::LearnResult::accepted){tonal_.commit();wind_.configure(profile_.anchor());broadband_.configure(profile_.anchor());}
        if(result!=NoiseProfile<maxBins>::LearnResult::waiting){context_.reset();wind_.reset();coherence_.reset();broadband_.reset();voiceGain_.reset();}
        const bool protect=profile_.learning()||!profile_.valid()||!settings_.enabled||effectiveMode_==Mode::Music;
        // Only Voice with nonzero Silence may use the extra residual label.
        // Strict idle remains the sole authority for profile and spectral DSP.
        const auto* variability=!protect&&!settings_.manual.enabled&&effectiveMode_==Mode::Voice&&silenceFloor_<1.f?
            &profile_.variability():nullptr;
        const bool idle=context_.update(power_,profile_.anchor(),channels_,bins,!protect,variability);
        const bool windEnabled=!settings_.spectralManual&&!protect&&!idle&&effectiveMode_==Mode::Voice&&cleanFloor_<1.f&&wind_.eligible();
        wind_.update(power_,profile_.anchor(),windEnabled);
        // Phase coherence alone cannot distinguish a learned hum from
        // desired harmonics. Keep the qualified tonal path authoritative.
        const bool coherenceEnabled=windEnabled&&tonal_.selected()<0;
        coherence_.update(work_,coherenceEnabled);
        const bool broadEligible=!protect&&effectiveMode_==Mode::Voice&&broadband_.eligible()&&tonal_.selected()<0;
        const bool broadEnabled=!settings_.spectralManual&&broadEligible&&cleanFloor_<1.f;
        const bool broadResidual=broadEligible&&!settings_.manual.enabled&&silenceFloor_<1.f;
        broadband_.update(power_,profile_.anchor(),broadEnabled||broadResidual,broadResidual);
        tonalIdle_=idle&&!protect&&effectiveMode_==Mode::Voice&&cleanFloor_<1.f;
        const bool broadIdle=broadResidual&&broadband_.residualIdle();
        const float expTarget=settings_.manual.enabled?1.f:idle?silenceFloor_:broadIdle?std::max(silenceFloor_,.12589254f):context_.adaptiveIdle()?
            std::max(silenceFloor_,.25118864f):1.f; // at most 12 dB extra
        // Only explicit Voice or source-aware SpeechMic permits independently
        // qualified coherent drift. Unknown/Mixed and musical policies keep
        // the original confirmed-idle tracking; anchor-based guards unchanged.
        profile_.track(power_,idle&&!settings_.spectralManual,!settings_.spectralManual&&!protect&&effectiveMode_==Mode::Voice,
                       context_.broadbandRatio(),context_.subbandPeakRatio());
        const bool selective=!settings_.spectralManual&&!protect&&!idle&&effectiveMode_==Mode::AutoProtect;
        const bool activeVoice=!protect&&!idle&&effectiveMode_==Mode::Voice;
        const float ratio=context_.broadbandRatio();
        const float floor=protect||!settings_.spectralEnabled?1.f:settings_.spectralManual||idle||effectiveMode_==Mode::Voice?cleanFloor_:
            effectiveMode_==Mode::Singing?singingFloor_:AutoGain::activeFloor(autoFloor_,ratio,context_.subbandPeakRatio());
        // Uncertainty reserve is prepared once per frame. The band-local
        // budget avoids giving dominant learned noise a global dry path.
        const float reserve=settings_.spectralManual?0.f:activeVoice?AutoGain::detailReserve(ratio,.75f):
            selective?AutoGain::detailReserve(ratio,.25f+.55f*settings_.musicProtect):0.f;
        if(protect||idle)voiceGain_.reset();
        // Analysis remains live, but when both gains are exactly unity no
        // inverse FFT is needed. Preserve the context and profile histories.
        if(floor==1.f&&expTarget==1.f){renderUnity();return;}
        for(int k=0;k<bins;++k) {
            float evidence=0;
            for(int c=0;c<channels_;++c)
                evidence=std::max(evidence,power_[c][k]/std::max(settings_.spectralManual?profile_.anchor()[c][k]:profile_.powers()[c][k],eps));
            // Only explicit Voice opts into the stronger causal estimator.
            if(settings_.spectralManual){target_[k]=ManualSpectral::target(evidence,inverseNoiseScale_,floor,settings_.vocalProtection);continue;}
            float gain=selective?AutoGain::target(floor,context_.temporalRatio(profile_.anchor(),channels_,bins,k),evidence):
                activeVoice?voiceGain_.estimate(k,evidence):
                std::sqrt(std::max(0.f,1.f-1.f/std::max(1.f,evidence)));
            // Only noise-overlap bands in explicit/source-aware Voice.
            // A quiet neighboring bin must independently show foreground.
            // The existing temporal neighborhood is reused; no FFT or delay.
            if(!settings_.spectralManual&&activeVoice&&ratio>=2.f&&evidence>1.f) {
                const float relative=context_.relativeNoise(profile_.anchor(),channels_,k);
                if(relative>1.f) {
                    float neighbor=0.f;
                    for(int j=std::max(0,k-3);j<=std::min(bins-1,k+3);++j){
                        if(j==k||context_.relativeNoise(profile_.anchor(),channels_,j)>=1.f)continue;
                        for(int c=0;c<channels_;++c)
                            neighbor=std::max(neighbor,power_[c][j]/std::max(profile_.anchor()[c][j],eps));
                    }
                    if(neighbor>4.f)gain=VoiceGain<maxBins>::restore(gain,evidence,relative,
                        context_.temporalRatio(profile_.anchor(),channels_,bins,k),neighbor);
                }
            }
            // P1: positive evidence only. Independent narrow-band phase
            // coherence can restore soft upper harmonics without opening
            // the entire gate or lifting the dominant DC/low noise bins.
            if(!settings_.spectralManual&&coherenceEnabled&&k>=2&&k<=4&&ratio<4.f){
                float support=0.f;
                for(int c=0;c<channels_;++c){
                    const float local=coherence_.support(c,k);
                    float neighbor=0.f;
                    for(int j=2;j<=4;++j){if(j==k)continue;
                        const float localRatio=power_[c][j]/std::max(profile_.anchor()[c][j],eps);
                        neighbor=std::max(neighbor,coherence_.support(c,j)*std::clamp((localRatio-1.f)*.25f,0.f,1.f));}
                    support=std::max(support,std::min(local,neighbor));
                }
                gain+=std::min(.40f,1.f-gain)*std::clamp((support-.06f)*3.f,0.f,1.f);
            }
            // Restore foreground outside the learned dominant band only
            // when independent local excess is present.
            if(!settings_.spectralManual&&broadEnabled&&activeVoice&&evidence>2.f&&broadband_.outsideNoiseBand(k))
                gain=broadband_.restoreOutsideNoise(k,gain,evidence,
                    context_.hasTemporalExcess(profile_.anchor(),channels_,bins,k));
            const float localFloor=reserve>0.f?AutoGain::detailFloor(floor,reserve,
                context_.relativeNoise(profile_.anchor(),channels_,k)):floor;
            target_[k]=std::max(localFloor,gain);
        }
        for(int k=0;k<bins;++k) {
            if(settings_.spectralManual){
                const float g=ManualSpectral::smooth(target_[k],target_[std::max(0,k-1)],target_[std::min(bins-1,k+1)],settings_.artifactControl);
                const float close=maskClose_/(1.f+3.f*settings_.artifactControl);
                mask_[k]=g>=mask_[k]?g:mask_[k]+close*(g-mask_[k]);continue;
            }
            const float smooth=0.25f*target_[std::max(0,k-1)]+0.5f*target_[k]+0.25f*target_[std::min(bins-1,k+1)];
            // A protected band must not be pulled down by neighboring bins.
            float g=std::max(selective||reserve>0.f?target_[k]:floor,smooth);
            if(!settings_.spectralManual&&windEnabled&&k==0)g=std::min(g,wind_.cap(k,floor));
            if(!settings_.spectralManual&&broadEnabled&&broadband_.mayLimit(k,g,floor))g=std::min(g,broadband_.cap(k,power_,floor));
            mask_[k]=g>=mask_[k]?g:mask_[k]+maskClose_*(g-mask_[k]);
        }
        // Immediate opening in the next overlap; slow closure. No hard gate.
        // One shared expander envelope; use faster closure only with strict
        // idle or independently confirmed broadband noise shape.
        const float close=(idle||broadIdle)&&effectiveMode_==Mode::Voice?voiceExpandClose_:expandClose_;
        expansion_=expTarget>=expansion_?expTarget:expansion_+close*(expTarget-expansion_);
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
    WindSpectralGuard<maxBins> wind_{};
    ForegroundCoherence<maxN> coherence_{};
    BroadbandNoise<maxBins> broadband_{};
    VoiceGain<maxBins> voiceGain_{};
    ManualExpander manual_{};
    bool manualActive_=false;
    int auditionRemaining_=0;
    float auditionMix_=0;
    float inverseNoiseScale_=1.f;
    TonalNoise tonal_{};
    bool tonalIdle_=false;
    float maskClose_=0.18f,expandClose_=0.035f,voiceExpandClose_=0.09f;
    float cleanFloor_=1,singingFloor_=1,silenceFloor_=1,autoFloor_=1;
    std::array<std::array<float,maxN>,2> in_{},out_{};
    std::array<std::array<Complex,maxN>,2> work_{};
    NoiseProfile<maxBins> profile_{};
    std::array<std::array<float,maxBins>,2> power_{};
    bool powerValid_=false;
    std::array<float,maxBins> mask_{},target_{};
};
} // namespace armix::builtin_fx::anti_noiz
