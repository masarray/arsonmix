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
    };
    struct Prepared {Settings s{};float threshold=.01f,closeThreshold=.007079458f,floor=.001f;};
    static bool valid(const Settings& s) noexcept {
        auto in=[](float x,float a,float b){return std::isfinite(x)&&x>=a&&x<=b;};
        return in(s.thresholdDb,-80,0)&&in(s.ratio,1,20)&&in(s.rangeDb,0,96)&&
            in(s.attackMs,.5f,20)&&in(s.holdMs,0,500)&&in(s.releaseMs,10,1000)&&
            in(s.kneeDb,0,18)&&in(s.hpHz,20,500)&&in(s.lpHz,1000,20000);
    }
    static Prepared prepareSettings(Settings s) noexcept {
        return {s,std::pow(10.f,s.thresholdDb/20.f),std::pow(10.f,(s.thresholdDb-3.f)/20.f),std::pow(10.f,-s.rangeDb/20.f)};
    }
    void prepare(double rate,int channels,int delay) noexcept {rate_=float(rate);channels_=channels;delay_=delay;reset();apply(p_);}
    void apply(const Prepared& p) noexcept {
        const bool toggled=p.s.enabled!=p_.s.enabled||p.s.gate!=p_.s.gate;
        p_=p;
        const auto& s=p.s;
        hp_=1.f/(1.f+rate_/(2.f*std::numbers::pi_v<float>*s.hpHz));
        lp_=1.f/(1.f+rate_/(2.f*std::numbers::pi_v<float>*s.lpHz));
        decay_=1.f/(1.f+rate_*.008f);
        attack_=1000.f/(rate_*s.attackMs);release_=1000.f/(rate_*s.releaseMs);
        holdSamples_=int(rate_*s.holdMs*.001f)+delay_;
        if(toggled)reset();
        hold_=std::min(hold_,holdSamples_);ticks_=0;
    }
    void reset() noexcept {low_={};band_={};env_=0;gain_=target_=1;hold_=ticks_=0;open_=true;}
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
        const float raw[2]={left,right};float peak=0;
        for(int c=0;c<channels_;++c){
            const float x=std::isfinite(raw[c])?std::clamp(raw[c],-64.f,64.f):0.f;
            low_[c]+=hp_*(x-low_[c]);band_[c]+=lp_*(x-low_[c]-band_[c]);
            peak=std::max(peak,std::abs(band_[c]));
        }
        env_=std::max(peak,env_*(1.f-decay_));
        if(env_<1e-15f)env_=0;
        const auto& s=p_.s;
        const bool above=env_>=(s.gate&&open_?p_.closeThreshold:p_.threshold);
        if(above){hold_=holdSamples_;open_=true;}
        else if(hold_>0)--hold_;
        else open_=false;
        // Reopen immediately on a threshold crossing. Recompute the
        // continuous expansion curve only every 16 samples, not every sample.
        if(above||hold_>0)target_=1.f;
        else if(s.gate)target_=s.rangeDb>=96?0.f:p_.floor;
        else if(ticks_==0){const float db=20.f*std::log10(std::max(env_,1e-12f));target_=std::pow(10.f,-cut(db,s)/20.f);}
        ticks_=(ticks_+1)&15;
        if(target_>gain_)gain_=std::min(target_,gain_+attack_);
        else gain_=std::max(target_,gain_-release_);
        return gain_;
    }
private:
    Prepared p_{};std::array<float,2> low_{},band_{};
    float rate_=48000,hp_=0,lp_=0,decay_=0,attack_=1,release_=1,env_=0,gain_=1,target_=1;
    int channels_=1,delay_=256,holdSamples_=0,hold_=0,ticks_=0;bool open_=true;
};
}
