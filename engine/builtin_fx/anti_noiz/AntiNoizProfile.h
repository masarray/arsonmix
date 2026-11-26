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
        // 50 ms: aggregate multiple overlapping FFT frames before deciding
        // a window contains a burst. Nominal 2 s; extend only for insufficient usable noise.
        learnWindow_=int(std::ceil(.05*rate/hop));
        minimumUsable_=(target_+1)/2; maximumFrames_=2*target_;
        seedFloor_=float(4.*hop*hop*1e-12);
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
        boundary_=windowAccepted_=seedReset_=seedRetried_=windowVeto_=false; seedWindows_=0;seedLevel_={};
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
    int checkpointFrameCount() const noexcept {return checkpointCount_;}
    bool atCheckpointBoundary() const noexcept {return boundary_;}
    bool lastWindowAccepted() const noexcept {return windowAccepted_;}
    bool checkpointReset() const noexcept {return seedReset_;}
    bool tonalCaptureSafe() const noexcept {return !seedRetried_&&count_*100>=observed_*85;}
    int observedFrames() const noexcept {return learning_?observed_:0;}
    int targetFrames() const noexcept {return observed_>=target_&&learning_?maximumFrames_:target_;}
    // Coefficient of variation of accepted FFT-frame broadband power.
    // Trusted only on successful Learn; rejected windows and attempts cannot replace it.
    const std::array<float,2>& variability() const noexcept {return variability_;}
    template<bool BoundedFftPower=false> LearnResult observe(const Powers& power) noexcept {
        boundary_=windowAccepted_=seedReset_=false;
        if(!learning_)return LearnResult::waiting;
        // Exclude the two first overlapping frames, which may include PCM
        // preceding the command. No dependence on callback partition.
        if(warmup_>0){--warmup_;return LearnResult::waiting;}
        std::array<float,2> total{};
        for(int c=0;c<channels_;++c)
            for(int k=0;k<bins_;++k){
                const float v=power[c][k];
                if constexpr(BoundedFftPower){total[c]+=v;candidate_[c][k]+=v;}
                else if(!std::isfinite(v)||v<0.f)windowVeto_=true;
                else{total[c]+=v;candidate_[c][k]+=v;}
            }
        // Retain a narrow fail-closed protection for a previously learned
        // extremely quiet stereo leg: broadband stereo summing must not
        // hide a new source on that leg. This preserves the original
        // quiet-channel protection without treating ordinary frame variation
        // as an immediate failure.
        if(valid_&&channels_==2&&checkpointCount_>=learnWindow_)
            for(int c=0;c<2;++c){
                const float mean=checkpointEnergy_[c]/float(checkpointCount_);
                const float other=checkpointEnergy_[1-c]/float(checkpointCount_);
                if(mean<.01f*other&&total[c]>32.f*std::max(mean,1e-30f)){
                    windowVeto_=true;
                }
            }
        // Preserve per-frame accumulation order exactly for a wholly
        // accepted learning attempt. At a window boundary take a checkpoint;
        // a contaminated window rolls back, rather than entering the PSD.
        for(int c=0;c<channels_;++c){
            energy_[c]+=total[c];windowEnergy_[c]+=total[c];
            windowSquares_[c]+=double(total[c])*total[c];
        }
        ++count_;++observed_;
        if(observed_%learnWindow_==0||observed_==target_||observed_==maximumFrames_){
            boundary_=true;
            const int frames=count_-checkpointCount_;
            std::array<float,2> level{};float sum=0.f;
            for(int c=0;c<channels_;++c){level[c]=windowEnergy_[c]/float(frames);sum+=level[c];}
            // Exact digital silence has no measurable noise profile. A very
            // quiet but nonzero microphone does: no arbitrary dBFS floor.
            const bool usable=std::isfinite(sum)&&sum>0.f&&!windowVeto_;
            bool discard=!usable,upward=false;
            if(checkpointCount_>0&&usable){
                for(int c=0;c<channels_;++c){
                    const float mean=checkpointEnergy_[c]/float(checkpointCount_);
                    upward|=level[c]>4.f*std::max(mean,1e-30f);
                }
                // Quieter nonzero windows are not foreground contamination.
                // Isolate louder bursts rather than aborting the transaction.
                discard=upward;
            }
            // A single near-digital-silence 50 ms seed may be a device fade-in. Only
            // before a reference is established, allow one bounded restart
            // after three mutually consistent louder windows. Never replace
            // an established quiet checkpoint with a sustained loud signal.
            if(upward&&!seedRetried_&&checkpointCount_<=learnWindow_&&
               (checkpointEnergy_[0]+checkpointEnergy_[1])/float(checkpointCount_)<=seedFloor_){
                bool consistent=seedWindows_>0;
                for(int c=0;c<channels_;++c){const float v=level[c],r=seedLevel_[c];consistent&=v<=2.f*std::max(r,1e-30f)&&r<=2.f*std::max(v,1e-30f);}
                seedWindows_=consistent?seedWindows_+1:1;seedLevel_=level;
                if(seedWindows_>=3){
                    checkpoint_={};checkpointEnergy_={};acceptedSquares_={};checkpointCount_=0;
                    seedReset_=seedRetried_=true;seedWindows_=0;
                }
            }else seedWindows_=0;
            if(discard){
                candidate_=checkpoint_;energy_=checkpointEnergy_;count_=checkpointCount_;
            }else{
                checkpoint_=candidate_;checkpointEnergy_=energy_;checkpointCount_=count_;
                for(int c=0;c<channels_;++c)acceptedSquares_[c]+=windowSquares_[c];
                windowAccepted_=true;
            }
            windowEnergy_={};windowSquares_={};windowVeto_=false;
            if(observed_>=target_){
                // Typical capture still completes at 2 s. Recover from short
                // interruptions by accepting >=1 s usable noise, or collect
                // more up to a strict 4 s cap. No perpetual Learn.
                if(count_<minimumUsable_){
                    if(count_>0&&observed_<maximumFrames_)return LearnResult::waiting;
                    cancel();return LearnResult::rejected;
                }
            }
        }
        if(observed_<target_||!boundary_)return LearnResult::waiting;
        cancel();
        if(count_<minimumUsable_||!std::isfinite(energy_[0]+energy_[1])||energy_[0]+energy_[1]<=0.f)return LearnResult::rejected;
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
    int channels_=0,bins_=0,target_=1,trackHold_=1,count_=0,warmup_=0,idle_=0;
    int learnWindow_=1,minimumUsable_=1,maximumFrames_=2,observed_=0,checkpointCount_=0,seedWindows_=0;
    std::array<float,2> seedLevel_{};
    bool boundary_=false,windowAccepted_=false,seedReset_=false,seedRetried_=false,windowVeto_=false;
    float alpha_=0,seedFloor_=0;
    bool valid_=false,learning_=false,qualified_=false;
};
} // namespace armix::builtin_fx::anti_noiz
