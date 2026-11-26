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
        lowBins_=std::min(bins_,int(400.*(2*hop)/rate)+1);
        target_=int(std::ceil(2.*rate/hop));
        // 50 ms: aggregate multiple overlapping FFT frames before deciding
        // a learned noise estimate is unstable. Keep the 2 s wall clock.
        learnWindow_=int(std::ceil(.05*rate/hop));
        maximumDiscarded_=target_*15/100;
        trackHold_=int(std::ceil(.5*rate/hop));
        alpha_=float(1.-std::exp(-double(hop)/(rate*2.)));
        drift_.prepare(rate,hop,channels,bins);
        valid_=false;qualified_=false;anchor_={};working_={};variability_={};cancel();
    }
    bool begin() noexcept {
        if(learning_)return false;
        candidate_={};checkpoint_={};energy_={};checkpointEnergy_={};windowEnergy_={};
        acceptedSquares_={};windowSquares_={};
        count_=observed_=checkpointCount_=0;warmup_=2;qualified_=false;
        learning_=true;idle_=0;drift_.reset();return true;
    }
    void clear() noexcept {cancel();valid_=qualified_=false;anchor_={};working_={};variability_={};checkpoint_={};checkpointCount_=0;}
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
        return !learning_&&qualified_;
    }
    bool learning() const noexcept {return learning_;}
    bool valid() const noexcept {return valid_;}
    const Powers& powers() const noexcept {return working_;}
    const Powers& anchor() const noexcept {return anchor_;}
    // Visualization may read only the committed 50 ms checkpoint, never
    // an unqualified current window or a rejected transaction.
    const Powers& captured() const noexcept {return checkpoint_;}
    int capturedFrames() const noexcept {return learning_?checkpointCount_:0;}
    int observedFrames() const noexcept {return learning_?observed_:0;}
    int targetFrames() const noexcept {return target_;}
    // Coefficient of variation of accepted FFT-frame broadband power.
    // Trusted only on successful Learn; rejected windows and attempts cannot replace it.
    const std::array<float,2>& variability() const noexcept {return variability_;}
    LearnResult observe(const Powers& power) noexcept {
        if(!learning_)return LearnResult::waiting;
        // Exclude the two first overlapping frames, which may include PCM
        // preceding the command. No dependence on callback partition.
        if(warmup_>0){--warmup_;return LearnResult::waiting;}
        std::array<float,2> total{};
        for(int c=0;c<channels_;++c)
            for(int k=0;k<bins_;++k)total[c]+=power[c][k];
        // Retain a narrow fail-closed protection for a previously learned
        // extremely quiet stereo leg: broadband stereo summing must not
        // hide a new source on that leg. This preserves the original
        // quiet-channel regression without penalizing mono colored noise.
        if(valid_&&channels_==2&&count_>=8)
            for(int c=0;c<2;++c){
                const float mean=energy_[c]/float(count_);
                const float other=energy_[1-c]/float(count_);
                if(mean<.01f*other&&total[c]>4.f*std::max(mean,1e-12f)){
                    cancel();return LearnResult::rejected;
                }
            }
        // Preserve per-frame accumulation order exactly for a wholly
        // accepted learning attempt. At a window boundary take a checkpoint;
        // a contaminated window rolls back, rather than entering the PSD.
        for(int c=0;c<channels_;++c){
            energy_[c]+=total[c];windowEnergy_[c]+=total[c];
            windowSquares_[c]+=double(total[c])*total[c];
            for(int k=0;k<bins_;++k)candidate_[c][k]+=power[c][k];
        }
        ++count_;++observed_;
        if(observed_%learnWindow_==0||observed_==target_){
            const int frames=count_-checkpointCount_;
            bool discard=false;
            if(checkpointCount_>0){
                for(int c=0;c<channels_;++c){
                    const float mean=checkpointEnergy_[c]/float(checkpointCount_);
                    const float current=windowEnergy_[c]/float(frames);
                    // Large sustained changes are not a trustworthy room
                    // noise profile. Fail immediately for extreme windows.
                    if(current>32.f*std::max(mean,1e-12f)){
                        cancel();return LearnResult::rejected;
                    }
                    // Wind-like deep rumble can have very quiet troughs. Do not
                    // mistake missing energy for foreground contamination when
                    // both the accepted checkpoint and new window have the
                    // same exceptionally low-frequency concentration. Upward
                    // contamination and all extreme vetoes remain unchanged.
                    bool quietRumble=false;
                    if(current<.1f*mean&&current>std::max(1e-12f,.001f*mean)&&
                       checkpointCount_>=2*learnWindow_&&lowBins_>=2){
                        float low=0.f,prior=0.f;
                        for(int k=0;k<lowBins_;++k){prior+=checkpoint_[c][k];low+=candidate_[c][k]-checkpoint_[c][k];}
                        quietRumble=prior>=.98f*checkpointEnergy_[c]&&low>=.98f*windowEnergy_[c];
                    }
                    discard|=current>4.f*std::max(mean,1e-12f)||(current<.1f*mean&&!quietRumble);
                }
            }
            if(discard){
                candidate_=checkpoint_;energy_=checkpointEnergy_;count_=checkpointCount_;
            }else{
                checkpoint_=candidate_;checkpointEnergy_=energy_;checkpointCount_=count_;
                for(int c=0;c<channels_;++c)acceptedSquares_[c]+=windowSquares_[c];
            }
            windowEnergy_={};windowSquares_={};
            if(observed_-count_>maximumDiscarded_){cancel();return LearnResult::rejected;}
        }
        if(observed_<target_)return LearnResult::waiting;
        cancel();
        if(count_<=0||(energy_[0]+energy_[1])/float(count_)<=1e-9f)return LearnResult::rejected;
        const float inv=1.f/float(count_);
        for(int c=0;c<channels_;++c)for(int k=0;k<bins_;++k)
            anchor_[c][k]=working_[c][k]=candidate_[c][k]*inv;
        for(int c=0;c<channels_;++c){
            const double mean=double(energy_[c])/count_;
            const double second=acceptedSquares_[c]/count_;
            variability_[c]=mean>1e-12?float(std::sqrt(std::max(0.,second/(mean*mean)-1.))):0.f;
        }
        qualified_=valid_=true;return LearnResult::accepted;
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
    Powers anchor_{},working_{},candidate_{},checkpoint_{};
    NoiseDriftGuard<MaxBins> drift_{};
    std::array<float,2> energy_{},checkpointEnergy_{},windowEnergy_{},variability_{};
    std::array<double,2> acceptedSquares_{},windowSquares_{};
    int channels_=0,bins_=0,lowBins_=0,target_=1,trackHold_=1,count_=0,warmup_=0,idle_=0;
    int learnWindow_=1,maximumDiscarded_=0,observed_=0,checkpointCount_=0;
    float alpha_=0;
    bool valid_=false,learning_=false,qualified_=false;
};
} // namespace armix::builtin_fx::anti_noiz
