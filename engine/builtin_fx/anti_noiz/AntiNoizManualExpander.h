// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
namespace armix::builtin_fx::anti_noiz {
// User-owned dynamics. Detector is raw input, output is the existing delayed
// audio. No FFT, allocation, extra delay, or noise-profile dependency.
class ManualExpander final {
public:
    struct Settings {
        bool enabled=false,gate=false;
        float thresholdDb=-40,ratio=4,rangeDb=60,attackMs=1,holdMs=100,releaseMs=120,kneeDb=0,hpHz=120,lpHz=12000;
        bool smartVoice=false;
        float smartMarginDb=9,smartMaxLiftDb=12;
    };
    struct Prepared {Settings s{};float threshold=.01f,closeThreshold=.007079458f,floor=.001f,kneeOpen=.01f,margin=2.818383f,maxLift=3.981072f;};
    static bool valid(const Settings& s) noexcept {
        auto in=[](float x,float a,float b){return std::isfinite(x)&&x>=a&&x<=b;};
        return in(s.thresholdDb,-80,0)&&in(s.ratio,1,20)&&in(s.rangeDb,0,96)&&
            in(s.attackMs,.5f,20)&&in(s.holdMs,0,500)&&in(s.releaseMs,10,1000)&&
            in(s.kneeDb,0,18)&&in(s.hpHz,20,500)&&in(s.lpHz,1000,20000)&&
            in(s.smartMarginDb,3,18)&&in(s.smartMaxLiftDb,0,24);
    }
    static Prepared prepareSettings(Settings s) noexcept {
        return {s,std::pow(10.f,s.thresholdDb/20.f),std::pow(10.f,(s.thresholdDb-3.f)/20.f),std::pow(10.f,-s.rangeDb/20.f),std::pow(10.f,(s.thresholdDb+s.kneeDb*.5f)/20.f),
                std::pow(10.f,s.smartMarginDb/20.f),std::pow(10.f,s.smartMaxLiftDb/20.f)};
    }
    void prepare(double rate,int channels,int delay) noexcept {
        rate_=float(rate);channels_=channels;delay_=delay;sampleEvery_=std::max(1,int(rate_*.001f));
        for(unsigned i=0;i<edges_.size();++i)edges_[i]=std::pow(10.f,(-120.f+3.f*float(i))/20.f);
        clearReference();reset();apply(p_);
    }
    void apply(const Prepared& p) noexcept {
        const bool timingChanged=p.s.attackMs!=p_.s.attackMs||p.s.releaseMs!=p_.s.releaseMs;
        const bool filterChanged=p.s.hpHz!=p_.s.hpHz||p.s.lpHz!=p_.s.lpHz;
        p_=p;
        if(filterChanged)clearReference();
        updateThreshold();
        const auto& s=p.s;
        hp_=1.f/(1.f+rate_/(2.f*std::numbers::pi_v<float>*s.hpHz));
        lp_=1.f/(1.f+rate_/(2.f*std::numbers::pi_v<float>*s.lpHz));
        decay_=1.f/(1.f+rate_*.008f);
        attackSamples_=std::max(1,int(std::lround(double(rate_)*s.attackMs*.001)));
        releaseSamples_=std::max(1,int(std::lround(double(rate_)*s.releaseMs*.001)));
        attack_=1.f/float(attackSamples_);release_=1.f/float(releaseSamples_);
        attackFollow_=6.907755f*attack_/(1.f+6.907755f*attack_);
        releaseFollow_=6.907755f*release_/(1.f+6.907755f*release_);
        holdSamples_=int(rate_*s.holdMs*.001f)+delay_;
        // Parameter changes retain the current gain. Re-time only when the
        // selected attack/release changes; no reset-to-unity on a slider edit.
        if(timingChanged){segmentRemaining_=0;segmentTarget_=gain_;}
        hold_=std::min(hold_,holdSamples_);ticks_=0;
    }
    void reset() noexcept {resetDetector();gain_=target_=segmentStart_=segmentTarget_=1;segmentRemaining_=hold_=ticks_=0;open_=true;}
    void beginReference() noexcept {cancelReference();capture_=true;referenceLow_={};referenceBand_={};referenceEnv_=0;}
    void cancelReference() noexcept {capture_=false;window_={};accepted_={};sampleTick_=0;}
    void clearReference() noexcept {cancelReference();referenceValid_=false;noisePeak_=0;updateThreshold();}
    void observeReference(float left,float right) noexcept {
        if(!capture_)return;
        detect(left,right,referenceLow_,referenceBand_,referenceEnv_);
        if(++sampleTick_<sampleEvery_)return;
        sampleTick_=0;
        const auto i=std::lower_bound(edges_.begin(),edges_.end(),referenceEnv_)-edges_.begin();
        ++window_[std::min<std::size_t>(std::size_t(i),edges_.size()-1)];
    }
    // The spectral Learn transaction owns acceptance of each window.
    void checkpointReference(bool accepted,bool restart=false) noexcept {
        if(!capture_)return;
        if(restart)accepted_={};
        if(accepted)for(unsigned i=0;i<window_.size();++i)accepted_[i]+=window_[i];
        window_={};
    }
    void finishReference(bool accepted) noexcept {
        if(!capture_)return;
        if(accepted){
            unsigned total=0;for(auto n:accepted_)total+=n;
            referenceValid_=total>=100;noisePeak_=0;
            if(referenceValid_){unsigned cumulative=0;const unsigned rank=(total*95+99)/100;
                for(unsigned i=0;i<accepted_.size();++i){cumulative+=accepted_[i];if(cumulative>=rank){noisePeak_=edges_[i];break;}}}
            updateThreshold();
        }
        cancelReference();
    }
    bool referenceValid() const noexcept {return referenceValid_;}
    float noisePeak() const noexcept {return noisePeak_;}
    float effectiveThreshold() const noexcept {return effectiveThreshold_;}
    float gain() const noexcept {return gain_;}
    float detector() const noexcept {return env_;}
    // Positive dB cut of the static soft-knee downward expansion curve.
    static float cut(float levelDb,const Settings& s) noexcept {
        const float d=s.thresholdDb-levelDb,k=s.kneeDb;
        const float below=k==0?std::max(0.f,d):d<=-k*.5f?0.f:d>=k*.5f?d:(d+k*.5f)*(d+k*.5f)/(2.f*k);
        return std::min(s.rangeDb,below*(s.ratio-1.f));
    }
    float step(float left,float right) noexcept {
        if(!p_.s.enabled)return 1;
        detect(left,right,low_,band_,env_);
        const auto& s=p_.s;
        const bool above=env_>=(s.gate?(open_?effectiveClose_:effectiveThreshold_):effectiveKnee_);
        if(above){hold_=holdSamples_;open_=true;}
        else if(hold_>0)--hold_;
        else open_=false;
        if(above||hold_>0||s.rangeDb==0.f||(!s.gate&&s.ratio==1.f))target_=1.f;
        else if(s.gate)target_=s.rangeDb>=96?0.f:p_.floor;
        else if(ticks_==0){
            const float db=s.smartVoice&&referenceValid_&&effectiveThreshold_!=p_.threshold?s.thresholdDb+20.f*std::log10(std::max(env_,1e-12f)/effectiveThreshold_):20.f*std::log10(std::max(env_,1e-12f));
            target_=std::pow(10.f,-cut(db,s)/20.f);
        }
        ticks_=(ticks_+1)&15;
        // Gate transitions have finite duration and zero slope at their
        // endpoints. Expansion follows the same static dB curve with a
        // continuous one-pole target follower; no restarted ramp per bin.
        return envelope(target_,s.gate);
    }
    float releaseToUnity() noexcept {
        target_=1.f;hold_=ticks_=0;open_=true;
        return envelope(1.f,true);
    }
private:
    float envelope(float target,bool gate) noexcept {
        if(gate){
            if(target!=segmentTarget_){
                segmentStart_=gain_;segmentTarget_=target;
                const bool opening=target>gain_;
                segmentSize_=segmentRemaining_=opening?attackSamples_:releaseSamples_;
                segmentInv_=opening?attack_:release_;
            }
            if(segmentRemaining_>0){
                if(--segmentRemaining_==0)gain_=segmentTarget_;
                else{
                    // Evaluate from the nearer endpoint to avoid subtracting
                    // almost equal floats during the final fade to silence.
                    const int elapsed=segmentSize_-segmentRemaining_;
                    const bool firstHalf=elapsed<=segmentRemaining_;
                    const float phase=float(firstHalf?elapsed:segmentRemaining_)*segmentInv_;
                    const float u=phase*phase*(3.f-2.f*phase);
                    const float change=(segmentTarget_-segmentStart_)*u;
                    gain_=firstHalf?segmentStart_+change:segmentTarget_-change;
                }
            }
        }else{
            // Approximate 60 dB settling over the selected time. A fixed
            // epsilon snap removes asymptotic tails and denormal work.
            const float difference=target-gain_;
            const float speed=difference>0.f?attackFollow_:releaseFollow_;
            const float next=gain_+difference*speed;
            gain_=std::abs(difference)<=1e-6f||next==gain_?target:next;
            segmentTarget_=gain_;segmentRemaining_=0;
        }
        return gain_;
    }
    void resetDetector() noexcept {low_={};band_={};env_=0;}
    void updateThreshold() noexcept {
        effectiveThreshold_=p_.threshold;effectiveClose_=p_.closeThreshold;effectiveKnee_=p_.kneeOpen;
        if(!p_.s.smartVoice||!referenceValid_)return;
        effectiveThreshold_=std::clamp(noisePeak_*p_.margin,p_.threshold,std::min(1.f,p_.threshold*p_.maxLift));
        const float scale=effectiveThreshold_/p_.threshold;
        effectiveClose_*=scale;effectiveKnee_*=scale;
    }
    void detect(float left,float right,std::array<float,2>& low,std::array<float,2>& band,float& env) noexcept {
        const float raw[2]={left,right};float peak=0;
        for(int c=0;c<channels_;++c){
            const float x=std::isfinite(raw[c])?std::clamp(raw[c],-64.f,64.f):0.f;
            low[c]+=hp_*(x-low[c]);band[c]+=lp_*(x-low[c]-band[c]);
            peak=std::max(peak,std::abs(band[c]));
        }
        env=std::max(peak,env*(1.f-decay_));
        if(env<1e-15f)env=0;
    }
    Prepared p_{};std::array<float,2> low_{},band_{},referenceLow_{},referenceBand_{};
    float referenceEnv_=0;
    std::array<float,53> edges_{};std::array<unsigned,53> window_{},accepted_{};
    float noisePeak_=0,effectiveThreshold_=.01f,effectiveClose_=.007079458f,effectiveKnee_=.01f;
    int sampleEvery_=48,sampleTick_=0;bool capture_=false,referenceValid_=false;
    float rate_=48000,hp_=0,lp_=0,decay_=0,attack_=1,release_=1,env_=0,gain_=1,target_=1;
    float attackFollow_=1.f,releaseFollow_=1.f;
    float segmentStart_=1.f,segmentTarget_=1.f,segmentInv_=1.f;
    int attackSamples_=1,releaseSamples_=1,segmentSize_=1,segmentRemaining_=0;
    int channels_=1,delay_=256,holdSamples_=0,hold_=0,ticks_=0;bool open_=true;
};
}
