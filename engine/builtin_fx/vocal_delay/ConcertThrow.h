// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Parameters.h"
#include "DelayLine.h"
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace armix::builtin_fx::vocal_delay {
// AUDIO-owned fixed-memory causal selective capture. No pitch/word recognition.
// 10ms energy frames; prior normal-level reference, sustained elevation,
// confirmed silence, cooldown. No per-trigger copy, search or allocation.
class ConcertThrow final {
public:
    struct Capture{float l=0,r=0;bool retire=false;};
    enum class Status:std::uint32_t { armed=0, tracking=1, capture=2, cooldown=3 };
    void prepare(double rate){
        frame_=std::max(1u,std::uint32_t(std::lround(rate*.01)));
        edge_=std::max(1u,std::uint32_t(std::lround(rate*.008)));
        left_.prepare(frame_*64+2);right_.prepare(frame_*64+2);reset();
    }
    std::size_t ringBytes()const noexcept{return (left_.capacity()+right_.capacity())*sizeof(float);}
    void reset() noexcept{
        left_.invalidate();right_.invalidate();count_=phrase_=gap_=high_=lastHigh_=learned_=cooldown_=remaining_=length_=age_=0;
        energy_=difference_=reference_=previous_=0;held_=autoTail_=false;recent_=sinceVoice_=0;status_=Status::armed;
    }
    Status status()const noexcept{return status_;}
    Capture tick(float l,float r,ThrowMode mode,bool manual) noexcept{
        if(mode!=policyMode_){policy_=config(mode);policyMode_=mode;}
        const auto& policy=policy_;
        // Read before write; constant age replays the selected past segment
        // in real time, so work is O(1) even on the trigger sample.
        Capture out{};
        if(remaining_){
            const auto pos=length_-remaining_;
            const float gain=std::min(1.f,float(std::min(pos,remaining_-1))/float(edge_));
            out={left_.read({age_,0})*gain,right_.read({age_,0})*gain};
            --remaining_;
        }
        if(cooldown_)--cooldown_;
        if(recent_){--recent_;sinceVoice_=std::min(frame_*64,sinceVoice_+1);}
        if(manual&&!held_&&recent_&&!remaining_&&sinceVoice_<=frame_*20){
            start(std::min(policy.capture*frame_,recent_),sinceVoice_,policy,false);
        }
        held_=manual;
        // Max-energy channel (not stereo fold) avoids false silence on
        // anti-phase sources. Derivative rejects many isolated HF noises.
        const float v=std::abs(l)>=std::abs(r)?l:r;
        energy_+=v*v;const float diff=v-previous_;difference_+=diff*diff;previous_=v;
        left_.push(l);right_.push(r);
        if(++count_>=frame_){
            const float rms=std::sqrt(energy_/float(frame_));
            const bool coherent=difference_<=energy_*.85f;
            count_=0;energy_=difference_=0;
            const float floor=std::clamp(reference_*.10f,.006f,.015f);
            if(rms>floor&&coherent){
                // A new voiced frame supersedes an automatic throw. The core
                // ramps/invalidates wet only; never gate the microphone.
                if(autoTail_){autoTail_=false;remaining_=0;out.retire=true;out.l=out.r=0.f;}
                recent_=std::min(frame_*32,recent_+frame_*2);sinceVoice_=0;
                if(gap_>=policy.gap){phrase_=high_=lastHigh_=0;}
                gap_=0;phrase_=std::min(phrase_+1,10000u);
                if(learned_>=12&&rms>=std::max(.018f,reference_*policy.ratio)){
                    high_=std::min(high_+1,10000u);lastHigh_=0;
                }else{
                    if(high_&&++lastHigh_>5)high_=0;
                    reference_=learned_?reference_+.035f*(rms-reference_):rms;
                    learned_=std::min(learned_+1,10000u);
                }
            }else{
                if(phrase_&&gap_<policy.gap){
                    ++gap_;
                    if(gap_==policy.gap){
                        if(mode!=ThrowMode::manual&&!cooldown_&&!remaining_&&learned_>=12&&
                           phrase_>=policy.phrase&&high_>=policy.high&&lastHigh_<=5){
                            const auto n=std::min(policy.capture,phrase_)*frame_;
                            start(n,gap_*frame_,policy,true);
                        }
                        phrase_=high_=lastHigh_=0;
                    }
                }
                // Silence does not drag the learned reference downward.
            }
        }
        status_=remaining_?Status::capture:cooldown_?Status::cooldown:phrase_?Status::tracking:Status::armed;
        return out;
    }
private:
    struct Policy{float ratio;std::uint32_t phrase,high,gap,capture,cooldown;};
    static Policy config(ThrowMode m)noexcept{
        switch(m){
            case ThrowMode::ballad:return {1.55f,28,18,15,19,115};
            case ThrowMode::pop:return {1.8f,20,12,11,13,80};
            case ThrowMode::dangdut:return {2.1f,22,15,14,12,105};
            case ThrowMode::rock:return {2.f,23,15,13,16,95};
            case ThrowMode::ambient:return {1.65f,35,20,18,22,150};
            case ThrowMode::intimate:return {1.7f,25,16,15,12,120};
            default:return {1.85f,24,15,14,17,110};
        }
    }
    void start(std::uint32_t n,std::uint32_t gap,const Policy& p,bool automatic)noexcept{
        if(n<edge_*2||n+gap>=left_.capacity())return;
        length_=remaining_=n;age_=n+gap;cooldown_=p.cooldown*frame_;autoTail_=automatic;
    }
    Policy policy_=config(ThrowMode::off);ThrowMode policyMode_=ThrowMode::off;
    DelayLine left_,right_;
    std::uint32_t frame_=1,edge_=1,count_=0,phrase_=0,gap_=0,high_=0,lastHigh_=0,learned_=0,
        cooldown_=0,remaining_=0,length_=0,age_=0,recent_=0,sinceVoice_=0;
    float energy_=0,difference_=0,reference_=0,previous_=0;
    bool held_=false,autoTail_=false;Status status_=Status::armed;
};
} // namespace
