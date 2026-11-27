// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace armix::builtin_fx {
// A linked stereo gain cell driven by another channel, never by the program.
// No lookahead, allocation, locks, block-size assumptions or per-sample exp/log.
struct AutoDuckerSettings final {
    float thresholdDb=-34.f, depthDb=12.f, attackMs=18.f, holdMs=220.f,
          releaseMs=480.f, hysteresisDb=5.f, highPassHz=120.f, lowPassHz=5000.f;
};
struct AutoDuckerKey final {
    const float* left=nullptr;
    const float* right=nullptr; // null means mono, left must be present
    bool valid() const noexcept {return left!=nullptr;}
};
class AutoDuckerCore final {
public:
    static constexpr int addedLatencySamples=0;
    struct Prepared final {
        AutoDuckerSettings settings{};
        float onPower=0,offPower=0,floor=1,attack=0,release=0,
              detectorAttack=0,detectorRelease=0,hp=0,lp=0;
        std::uint32_t hold=0;
    };
    static bool prepareSettings(double rate,const AutoDuckerSettings& s,Prepared& out) noexcept {
        const auto in=[](float v,float lo,float hi){return std::isfinite(v)&&v>=lo&&v<=hi;};
        if(!std::isfinite(rate)||rate<8000.||rate>384000.||
           !in(s.thresholdDb,-72,0)||!in(s.depthDb,0,36)||
           !in(s.attackMs,2,250)||!in(s.holdMs,0,2000)||!in(s.releaseMs,20,3000)||
           !in(s.hysteresisDb,1,18)||!in(s.highPassHz,30,400)||
           !in(s.lowPassHz,1000,12000))return false;
        const auto pole=[rate](double ms){return static_cast<float>(std::exp(-2.302585092994046/(rate*ms*.001)));};
        const auto cutoff=[rate](double hz){return static_cast<float>(1.-std::exp(-6.283185307179586*hz/rate));};
        Prepared p{};p.settings=s;
        p.onPower=std::pow(10.f,s.thresholdDb*.1f);
        p.offPower=std::pow(10.f,(s.thresholdDb-s.hysteresisDb)*.1f);
        p.floor=std::pow(10.f,-s.depthDb*.05f);
        p.attack=pole(s.attackMs);p.release=pole(s.releaseMs);
        p.detectorAttack=pole(8);p.detectorRelease=pole(65);
        p.hp=cutoff(s.highPassHz);p.lp=cutoff(std::min<double>(s.lowPassHz,rate*.45));
        p.hold=static_cast<std::uint32_t>(std::llround(rate*s.holdMs*.001));
        out=p;return true;
    }
    bool prepare(double rate,const AutoDuckerSettings& s={}) noexcept {
        Prepared p;
        if(!prepareSettings(rate,s,p))return false;
        p_=p;ready_=true;reset();return true;
    }
    void applyPrepared(const Prepared& p) noexcept {
        // Only packets created by prepareSettings for the active sample rate
        // may enter this audio-owned method (control mailbox contract).
        p_=p;hold_=std::min(hold_,p_.hold);
    }
    void clearKey() noexcept {a_={};b_={};power_=0;hold_=0;active_=false;}
    void reset() noexcept {clearKey();gain_=1;}
    float gain() const noexcept {return gain_;}
    bool active() const noexcept {return active_;}
    void process(float* left,float* right,int frames,AutoDuckerKey key) noexcept {
        if(!ready_||!left||!right||left==right||frames<=0)return;
        if(!key.valid())clearKey();
        for(int i=0;i<frames;++i){
            if(key.valid()){
                const float x=filtered(key.left[i],a_);
                const float y=filtered(key.right?key.right[i]:key.left[i],b_);
                const float peak=std::max(x*x,y*y); // anti-phase safe; no stereo sum
                const float pole=peak>power_?p_.detectorAttack:p_.detectorRelease;
                power_=peak+pole*(power_-peak);
                if(power_<1.e-20f)power_=0;
                if(active_){
                    if(power_>=p_.offPower)hold_=p_.hold;
                    else if(hold_>0)--hold_;
                    else active_=false;
                }else if(power_>=p_.onPower){active_=true;hold_=p_.hold;}
            }
            const float target=active_?p_.floor:1.f;
            const float pole=target<gain_?p_.attack:p_.release;
            gain_=target+pole*(gain_-target);
            if(std::abs(gain_-target)<1.e-6f)gain_=target;
            left[i]*=gain_;right[i]*=gain_;
        }
    }
private:
    struct Filter final {float low=0,band=0;};
    float filtered(float x,Filter& f) const noexcept {
        x=std::isfinite(x)?std::clamp(x,-16.f,16.f):0.f;
        f.low+=p_.hp*(x-f.low);
        f.band+=p_.lp*((x-f.low)-f.band);
        if(std::abs(f.low)<1.e-20f)f.low=0;
        if(std::abs(f.band)<1.e-20f)f.band=0;
        return f.band;
    }
    Prepared p_{};
    Filter a_{},b_{};
    float power_=0,gain_=1;
    std::uint32_t hold_=0;
    bool ready_=false,active_=false;
};
} // namespace armix::builtin_fx
