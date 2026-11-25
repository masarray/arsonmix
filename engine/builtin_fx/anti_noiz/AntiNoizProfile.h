// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "AntiNoizDrift.h"
namespace armix::builtin_fx::anti_noiz {
// Single audio owner. Candidate, trusted anchor and bounded working estimate
// are distinct: a rejected learning transaction never replaces trusted noise.
template<int MaxBins> class NoiseProfile final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    enum class LearnResult { waiting, accepted, rejected };
    void prepare(double rate,int hop,int channels,int bins) noexcept {
        channels_=channels;bins_=bins;
        target_=int(std::ceil(2.*rate/hop));
        trackHold_=int(std::ceil(.5*rate/hop));
        alpha_=float(1.-std::exp(-double(hop)/(rate*2.)));
        drift_.prepare(rate,hop,channels,bins);
        valid_=false;anchor_={};working_={};cancel();
    }
    bool begin() noexcept {
        if(learning_)return false;
        candidate_={};energy_={};count_=0;warmup_=2;learning_=true;idle_=0;drift_.reset();return true;
    }
    void cancel() noexcept {learning_=false;idle_=0;drift_.reset();}
    // Revoke the extra Voice authority at a policy boundary, not on each
    // foreground frame. No Learn invalidation or new profile transaction.
    void limitToIdleBounds() noexcept {
        if(valid_)for(int c=0;c<channels_;++c)for(int k=0;k<bins_;++k)
            working_[c][k]=std::clamp(working_[c][k],.7f*anchor_[c][k],1.3f*anchor_[c][k]);
        idle_=0;drift_.reset();
    }
    // Native lifecycle readback, only queried on the AUDIO owner after a
    // running attempt stops. Uses the existing Learn transaction evidence.
    bool lastAttemptQualified() const noexcept {
        return !learning_&&count_>=target_&&
            (energy_[0]+energy_[1])/float(count_)>1e-9f;
    }
    bool learning() const noexcept {return learning_;}
    bool valid() const noexcept {return valid_;}
    const Powers& powers() const noexcept {return working_;}
    const Powers& anchor() const noexcept {return anchor_;}
    LearnResult observe(const Powers& power) noexcept {
        if(!learning_)return LearnResult::waiting;
        // Exclude the two first overlapping frames, which may include PCM
        // preceding the command. No dependence on callback partition.
        if(warmup_>0){--warmup_;return LearnResult::waiting;}
        std::array<float,2> total{};
        for(int c=0;c<channels_;++c){
            for(int k=0;k<bins_;++k)total[c]+=power[c][k];
            if(count_>=8){
                const float mean=energy_[c]/float(count_);
                if(total[c]>4.f*std::max(mean,1e-12f)||total[c]<.1f*mean){cancel();return LearnResult::rejected;}
            }
        }
        for(int c=0;c<channels_;++c){
            energy_[c]+=total[c];
            for(int k=0;k<bins_;++k)candidate_[c][k]+=power[c][k];
        }
        if(++count_<target_)return LearnResult::waiting;
        cancel();
        if((energy_[0]+energy_[1])/float(count_)<=1e-9f)return LearnResult::rejected;
        const float inv=1.f/float(count_);
        for(int c=0;c<channels_;++c)for(int k=0;k<bins_;++k)
            anchor_[c][k]=working_[c][k]=candidate_[c][k]*inv;
        valid_=true;return LearnResult::accepted;
    }
    // Ordinary confirmed-idle tracking retains the original 70-130% bounds.
    // An independently qualified coherent upward drift may extend working PSD,
    // but never the immutable anchor or the caller's idle/foreground decision.
    void track(const Powers& power,bool idle,bool allowCoherent=false,float broadband=0.f,float subbandPeak=0.f) noexcept {
        if(!valid_||learning_){idle_=0;drift_.reset();return;}
        if(!idle){
            idle_=0;
            if(!allowCoherent){drift_.reset();return;}
            if(drift_.observe(power,anchor_,broadband,subbandPeak)){
                for(int c=0;c<channels_;++c)if(drift_.channelEligible(c))
                    for(int k=0;k<bins_;++k){
                        const float anchor=anchor_[c][k];auto& v=working_[c][k];
                        const float target=anchor*drift_.targetScale(c,k);
                        v=std::clamp(v+drift_.alpha()*(target-v),.7f*anchor,
                                     NoiseDriftGuard<MaxBins>::maxScale*anchor);
                    }
            }
            return;
        }
        drift_.reset();
        if(idle_<trackHold_){++idle_;return;}
        for(int c=0;c<channels_;++c)for(int k=0;k<bins_;++k){
            const float anchor=anchor_[c][k];auto& v=working_[c][k];
            const float observed=std::min(power[c][k],8.f*anchor);
            // When returning from coherent drift, slew back rather than
            // instantly clamping an extended working estimate to 1.3x.
            v=std::clamp(v+alpha_*(observed-v),.7f*anchor,std::max(1.3f*anchor,v));
        }
    }
private:
    Powers anchor_{},working_{},candidate_{};
    NoiseDriftGuard<MaxBins> drift_{};
    std::array<float,2> energy_{};
    int channels_=0,bins_=0,target_=1,trackHold_=1,count_=0,warmup_=0,idle_=0;
    float alpha_=0;
    bool valid_=false,learning_=false;
};
} // namespace armix::builtin_fx::anti_noiz
