// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <complex>
#include <numbers>

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
    };
    static constexpr int maxN=512, maxBins=maxN/2+1;
    bool prepare(double rate,int channels) noexcept {
        ready_=false;
        if(!std::isfinite(rate)||rate<44100.||rate>96000.||channels<1||channels>2) return false;
        rate_=rate; channels_=channels; n_=rate>48000.?512:256; hop_=n_/2;
        for(int i=0;i<n_;++i) {
            win_[i]=std::sqrt(0.5f-0.5f*std::cos(2.f*std::numbers::pi_v<float>*float(i)/float(n_)));
            unsigned x=static_cast<unsigned>(i),rev=0;
            for(int v=n_;v>1;v>>=1){rev=(rev<<1)|(x&1U);x>>=1;}
            rev_[i]=static_cast<int>(rev);
        }
        for(int i=0;i<hop_;++i) {
            const float a=-2.f*std::numbers::pi_v<float>*float(i)/float(n_);
            twiddle_[i]={std::cos(a),std::sin(a)};
        }
        for(auto& a:in_)a.fill(0);
        for(auto& a:out_)a.fill(0);
        for(auto& a:profile_)a.fill(0);
        for(auto& a:candidate_)a.fill(0);
        mask_.fill(1); pos_=ticks_=frames_=quiet_=learnCount_=learnTarget_=0;
        candidateEnergy_=0; learning_=valid_=false; expansion_=1;
        ready_=true; return true;
    }
    bool setSettings(Settings s) noexcept {
        if(!std::isfinite(s.cleanDb)||!std::isfinite(s.silenceDb)||s.cleanDb<0||s.cleanDb>24||
           s.silenceDb<0||s.silenceDb>48||static_cast<unsigned>(s.mode)>3U)return false;
        settings_=s; return true;
    }
    bool beginLearn() noexcept {
        if(!ready_||learning_)return false;
        for(auto& a:candidate_)a.fill(0);
        candidateEnergy_=0; learnCount_=0; learning_=true;
        learnTarget_=static_cast<int>(std::ceil(2.*rate_/hop_));
        quiet_=0; return true;
    }
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
    void fft(std::array<Complex,maxN>& a,bool inverse) noexcept {
        for(int i=0;i<n_;++i)if(i<rev_[i])std::swap(a[i],a[rev_[i]]);
        for(int len=2;len<=n_;len*=2) {
            const int half=len/2, step=n_/len;
            for(int base=0;base<n_;base+=len)for(int j=0;j<half;++j) {
                const auto w=inverse?std::conj(twiddle_[j*step]):twiddle_[j*step];
                const auto u=a[base+j],v=a[base+j+half]*w;
                a[base+j]=u+v;a[base+j+half]=u-v;
            }
        }
        if(inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
    }
    void frame() noexcept {
        const int bins=hop_+1;
        float energy=0;
        std::array<float,2> total{},noise{};
        for(int c=0;c<channels_;++c) {
            for(int i=0;i<n_;++i)work_[c][i]={in_[c][(pos_+i)%n_]*win_[i],0};
            fft(work_[c],false);
            for(int k=0;k<bins;++k) {
                power_[c][k]=std::norm(work_[c][k]);
                total[c]+=power_[c][k];noise[c]+=profile_[c][k];
            }
            energy+=total[c];
        }
        if(frames_<2)++frames_;
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
                    quiet_=0;
                }
            }
        }
        // No active classification claim: only conservative noise-like pause
        // detection. Any channel with excess energy protects both channels.
        float ratio=0,peakRatio=0;
        for(int c=0;c<channels_;++c) {
            ratio=std::max(ratio,total[c]/std::max(noise[c],eps));
            // Aggregate groups to avoid triggering on random single FFT bins.
            for(int k=0;k<bins;k+=8) {
                float p=0,q=0;
                for(int j=k;j<std::min(k+8,bins);++j){p+=power_[c][j];q+=profile_[c][j];}
                peakRatio=std::max(peakRatio,p/std::max(q,eps));
            }
        }
        const bool protect=learning_||!valid_||settings_.mode==Mode::Music;
        const bool noiseLike=!protect&&ratio<1.65f&&peakRatio<5.f;
        const int hold=static_cast<int>(std::ceil(0.18*rate_/hop_));
        quiet_=noiseLike?std::min(quiet_+1,hold):0;
        const bool idle=quiet_>=hold;
        const float limit=protect?0.f:idle?settings_.cleanDb:
            settings_.mode==Mode::Voice?settings_.cleanDb:
            settings_.mode==Mode::Singing?std::min(6.f,settings_.cleanDb):0.f;
        const float floor=gainDb(limit);
        for(int k=0;k<bins;++k) {
            float evidence=0;
            for(int c=0;c<channels_;++c)
                evidence=std::max(evidence,power_[c][k]/std::max(profile_[c][k],eps));
            // Conservative magnitude Wiener baseline (not decision-directed yet).
            target_[k]=std::max(floor,std::sqrt(std::max(0.f,1.f-1.f/std::max(1.f,evidence))));
        }
        for(int k=0;k<bins;++k) {
            const float smooth=0.25f*target_[std::max(0,k-1)]+0.5f*target_[k]+0.25f*target_[std::min(bins-1,k+1)];
            const float g=std::max(floor,smooth);
            mask_[k]=g>=mask_[k]?g:mask_[k]+0.18f*(g-mask_[k]);
        }
        const float expTarget=idle?gainDb(settings_.silenceDb):1.f;
        // Immediate opening in the next overlap; slow closure. No hard gate.
        expansion_=expTarget>=expansion_?expTarget:expansion_+0.035f*(expTarget-expansion_);
        for(int c=0;c<channels_;++c) {
            for(int k=0;k<bins;++k) {
                const float g=mask_[k]*expansion_;
                work_[c][k]*=g;
                if(k>0&&k<hop_)work_[c][n_-k]*=g;
            }
            fft(work_[c],true);
            for(int i=0;i<n_;++i)out_[c][(pos_+i)%n_]+=work_[c][i].real()*win_[i];
        }
    }
    Settings settings_{};
    double rate_=48000.;
    int channels_=0,n_=256,hop_=128,pos_=0,ticks_=0,frames_=0,quiet_=0,learnCount_=0,learnTarget_=0;
    bool ready_=false,valid_=false,learning_=false;
    float candidateEnergy_=0,expansion_=1;
    std::array<float,maxN> win_{};
    std::array<int,maxN> rev_{};
    std::array<Complex,maxN/2> twiddle_{};
    std::array<std::array<float,maxN>,2> in_{},out_{};
    std::array<std::array<Complex,maxN>,2> work_{};
    std::array<std::array<float,maxBins>,2> profile_{},candidate_{},power_{};
    std::array<float,maxBins> mask_{},target_{};
};
} // namespace armix::builtin_fx::anti_noiz
