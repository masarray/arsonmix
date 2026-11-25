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

namespace armix::builtin_fx::anti_noiz {
// Independent bounded WOLA reference, not a trained context classifier.
// All methods belong to the single DSP owner; prepare is off the audio thread.
class Core {
public:
    enum class Mode { AutoProtect, Voice, Singing, Music };
    struct Settings {
        Mode mode=Mode::AutoProtect;
        float cleanDb=14.f;
        float silenceDb=32.f;
        float musicProtect=1.f;
        bool enabled=true;
    };
    static constexpr int maxN=FftPlan::maxN, maxBins=maxN/2+1;
    bool prepare(double rate,int channels) noexcept {
        ready_=false;
        if(!std::isfinite(rate)||rate<44100.||rate>96000.||channels<1||channels>2) return false;
        rate_=rate; channels_=channels; n_=rate>48000.?512:256; hop_=n_/2;
        for(int i=0;i<n_;++i) {
            win_[i]=std::sqrt(0.5f-0.5f*std::cos(2.f*std::numbers::pi_v<float>*float(i)/float(n_)));
        }
        fft_.prepare(n_);
        prepareTiming();
        context_.prepare(rate_,hop_);
        voiceGain_.prepare(rate_,hop_);
        for(auto& a:in_)a.fill(0);
        for(auto& a:out_)a.fill(0);
        for(auto& a:profile_)a.fill(0);
        for(auto& a:candidate_)a.fill(0);
        mask_.fill(1); pos_=ticks_=frames_=learnCount_=learnTarget_=0;
        candidateEnergy_=0; learning_=valid_=false; expansion_=1;
        prepareGains();
        ready_=true; return true;
    }
    bool setSettings(Settings s) noexcept {
        if(!std::isfinite(s.cleanDb)||!std::isfinite(s.silenceDb)||s.cleanDb<0||s.cleanDb>24||
           s.silenceDb<0||s.silenceDb>48||!std::isfinite(s.musicProtect)||s.musicProtect<0||s.musicProtect>1||static_cast<unsigned>(s.mode)>3U)return false;
        if(!s.enabled||s.mode==Mode::Music)learning_=false;
        if(s.mode!=settings_.mode||s.enabled!=settings_.enabled)voiceGain_.reset();
        settings_=s; prepareGains(); return true;
    }
    bool beginLearn() noexcept {
        if(!ready_||learning_)return false;
        for(auto& a:candidate_)a.fill(0);
        candidateEnergy_=0; learnCount_=0; learning_=true;
        learnTarget_=static_cast<int>(std::ceil(2.*rate_/hop_));
        context_.reset(); voiceGain_.reset(); return true;
    }
    // Quiescent/offline owner only: restart the signal timeline, keep trusted PSD.
    void resetStreamKeepProfile() noexcept {
        for(auto& a:in_)a.fill(0);
        for(auto& a:out_)a.fill(0);
        mask_.fill(1); expansion_=1;
        pos_=ticks_=frames_=learnCount_=learnTarget_=0;
        candidateEnergy_=0; learning_=false; context_.reset(); voiceGain_.reset();
    }
    Settings settings() const noexcept {return settings_;}
    float spectralGain() const noexcept {return *std::min_element(mask_.begin(),mask_.begin()+hop_+1);}
    float residualGain() const noexcept {return expansion_;}
    bool ready() const noexcept {return ready_;}
    bool hasProfile() const noexcept {return valid_;}
    bool learning() const noexcept {return learning_;}
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
    void prepareGains() noexcept {
        cleanFloor_=gainDb(settings_.cleanDb);
        singingFloor_=gainDb(std::min(12.f-6.f*settings_.musicProtect,settings_.cleanDb));
        silenceFloor_=gainDb(settings_.silenceDb);
    }
    void renderUnity() noexcept {
        // Algebraically equivalent unity WOLA, with the same delay and overlap.
        for(int c=0;c<channels_;++c)for(int i=0;i<n_;++i) {
            const int j=(pos_+i)%n_;
            out_[c][j]+=in_[c][j]*win_[i]*win_[i];
        }
        mask_.fill(1); expansion_=1; context_.reset();
    }
    void frame() noexcept {
        if(frames_<2)++frames_;
        if(!learning_&&(!valid_||!settings_.enabled||settings_.mode==Mode::Music)) {
            renderUnity(); return;
        }
        const int bins=hop_+1;
        float energy=0;
        for(int c=0;c<channels_;++c) {
            float total=0;
            for(int i=0;i<n_;++i)work_[c][i]={in_[c][(pos_+i)%n_]*win_[i],0};
            fft_.transform(work_[c],false);
            for(int k=0;k<bins;++k) {
                power_[c][k]=std::norm(work_[c][k]);
                total+=power_[c][k];
            }
            energy+=total;
        }
        // Transactional manual learn. Reject gross energy jumps; previous
        // profile remains valid. Stationary music cannot be ruled out here.
        if(learning_&&frames_>=2) {
            const float mean=learnCount_?candidateEnergy_/float(learnCount_):energy;
            if(learnCount_>=8 && (energy>mean*4.f || energy<mean*0.1f)) {
                learning_=false;
            } else {
                candidateEnergy_+=energy;
                for(int c=0;c<channels_;++c)for(int k=0;k<bins;++k)candidate_[c][k]+=power_[c][k];
                if(++learnCount_>=learnTarget_) {
                    if(candidateEnergy_/float(learnCount_)>1e-9f) {
                        for(int c=0;c<channels_;++c)for(int k=0;k<bins;++k)
                            profile_[c][k]=candidate_[c][k]/float(learnCount_);
                        valid_=true;
                    }
                    learning_=false;
                    context_.reset(); voiceGain_.reset();
                }
            }
        }
        const bool protect=learning_||!valid_||!settings_.enabled||settings_.mode==Mode::Music;
        const bool idle=context_.update(power_,profile_,channels_,bins,!protect);
        const float floor=protect?1.f:idle||settings_.mode==Mode::Voice?cleanFloor_:
            settings_.mode==Mode::Singing?singingFloor_:1.f;
        if(protect||idle)voiceGain_.reset();
        for(int k=0;k<bins;++k) {
            float evidence=0;
            for(int c=0;c<channels_;++c)
                evidence=std::max(evidence,power_[c][k]/std::max(profile_[c][k],eps));
            // Only explicit Voice opts into the stronger causal estimator.
            const float gain=!protect&&!idle&&settings_.mode==Mode::Voice?voiceGain_.estimate(k,evidence):
                std::sqrt(std::max(0.f,1.f-1.f/std::max(1.f,evidence)));
            target_[k]=std::max(floor,gain);
        }
        for(int k=0;k<bins;++k) {
            const float smooth=0.25f*target_[std::max(0,k-1)]+0.5f*target_[k]+0.25f*target_[std::min(bins-1,k+1)];
            const float g=std::max(floor,smooth);
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
            for(int i=0;i<n_;++i)out_[c][(pos_+i)%n_]+=work_[c][i].real()*win_[i];
        }
    }
    Settings settings_{};
    double rate_=48000.;
    int channels_=0,n_=256,hop_=128,pos_=0,ticks_=0,frames_=0,learnCount_=0,learnTarget_=0;
    bool ready_=false,valid_=false,learning_=false;
    float candidateEnergy_=0,expansion_=1;
    std::array<float,maxN> win_{};
    FftPlan fft_{};
    ContextGuard<maxBins> context_{};
    VoiceGain<maxBins> voiceGain_{};
    float maskClose_=0.18f,expandClose_=0.035f;
    float cleanFloor_=1,singingFloor_=1,silenceFloor_=1;
    std::array<std::array<float,maxN>,2> in_{},out_{};
    std::array<std::array<Complex,maxN>,2> work_{};
    std::array<std::array<float,maxBins>,2> profile_{},candidate_{},power_{};
    std::array<float,maxBins> mask_{},target_{};
};
} // namespace armix::builtin_fx::anti_noiz
