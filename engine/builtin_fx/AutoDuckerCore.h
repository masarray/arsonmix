// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <span>
#include <cstddef>
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
// Bounded, independently filtered sources. A source is never PCM-summed.
struct AutoDuckerSource final {std::uint64_t id=0;AutoDuckerKey key{};};
class AutoDuckerCore final {
public:
    static constexpr int addedLatencySamples=0;
    static constexpr std::size_t maxSources=32;
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
    void clearKey() noexcept {detectors_={};hold_=0;active_=false;}
    void reset() noexcept {clearKey();gain_=1;}
    float gain() const noexcept {return static_cast<float>(gain_);}
    bool active() const noexcept {return active_;}
    float sourcePower(std::size_t k) const noexcept {return k<detectors_.size()?detectors_[k].power:0.f;}
    int phase() const noexcept {
        if(!active_)return gain_<.99999?4:0; // release / ready
        float maximum=0;for(const auto& d:detectors_)maximum=std::max(maximum,d.power);
        if(maximum<p_.offPower)return 3; // hold
        return std::abs(gain_-p_.floor)>1.e-4?1:2; // attack / duck
    }
    void process(float* left,float* right,int frames,AutoDuckerKey key) noexcept {
        const AutoDuckerSource source{1,key};
        processSources(left,right,frames,std::span<const AutoDuckerSource>(&source,1));
    }
    void processSources(float* left,float* right,int frames,
                        std::span<const AutoDuckerSource> keys) noexcept {
        if(!ready_||!left||!right||left==right||frames<=0)return;
        // Control sends a canonical sorted set. Defensively reject malformed
        // packets without reading beyond the prepared detector array.
        if(keys.size()>maxSources)keys={};
        for(std::size_t k=0;k<keys.size();++k){
            if(!keys[k].id||(k&&keys[k-1].id>=keys[k].id)){keys={};break;}
        }
        // Preserve detector state across source list changes by stable ID.
        // Reconciliation is only on changed identity, not every sample.
        bool changed=false;
        for(std::size_t k=0;k<maxSources;++k)
            if(detectors_[k].id!=(k<keys.size()?keys[k].id:0)){changed=true;break;}
        if(changed){
            std::array<Detector,maxSources> next{};
            for(std::size_t k=0;k<keys.size();++k){
                next[k].id=keys[k].id;
                for(const auto& old:detectors_)if(old.id==next[k].id){next[k]=old;break;}
            }
            detectors_=next;
        }
        bool available=false;
        for(std::size_t k=0;k<keys.size();++k){
            if(keys[k].key.valid())available=true;
            else {detectors_[k]={};detectors_[k].id=keys[k].id;}
        }
        if(!available){hold_=0;active_=false;}
        for(int i=0;i<frames;++i){
            float maximum=0;
            bool finiteSource=false;
            for(std::size_t k=0;k<keys.size();++k){
                const auto& key=keys[k].key;
                if(!key.valid())continue;
                auto& d=detectors_[k];
                const float rawL=key.left[i],rawR=key.right?key.right[i]:rawL;
                const bool finiteL=std::isfinite(rawL),finiteR=std::isfinite(rawR);
                if(!finiteL&&!finiteR){
                    d.a={};d.b={};d.power=0;continue;
                }
                finiteSource=true;
                if(!finiteL)d.a={};
                if(!finiteR)d.b={};
                const float x=finiteL?filtered(rawL,d.a):0.f;
                const float y=finiteR?filtered(rawR,d.b):0.f;
                const float peak=std::max(x*x,y*y);
                const float pole=peak>d.power?p_.detectorAttack:p_.detectorRelease;
                d.power=peak+pole*(d.power-peak);
                if(d.power<1.e-20f)d.power=0;
                maximum=std::max(maximum,d.power);
            }
            if(!finiteSource){active_=false;hold_=0;}
            else if(active_){
                if(maximum>=p_.offPower)hold_=p_.hold;
                else if(hold_>0)--hold_;
                else active_=false;
            }else if(maximum>=p_.onPower){active_=true;hold_=p_.hold;}
            const double target=active_?p_.floor:1.;
            const double pole=target<gain_?p_.attack:p_.release;
            gain_=target+pole*(gain_-target);
            if(std::abs(gain_-target)<1.e-6)gain_=target;
            const float linkedGain=static_cast<float>(gain_);
            left[i]*=linkedGain;right[i]*=linkedGain;
        }
    }
private:
    struct Filter final {float low=0,band=0;};
    struct Detector final {std::uint64_t id=0;Filter a{},b{};float power=0;};
    float filtered(float x,Filter& f) const noexcept {
        x=std::isfinite(x)?std::clamp(x,-16.f,16.f):0.f;
        f.low+=p_.hp*(x-f.low);
        f.band+=p_.lp*((x-f.low)-f.band);
        if(std::abs(f.low)<1.e-20f)f.low=0;
        if(std::abs(f.band)<1.e-20f)f.band=0;
        return f.band;
    }
    Prepared p_{};
    std::array<Detector,maxSources> detectors_{};
    double gain_=1; // avoid float convergence stall at high rates/long release
    std::uint32_t hold_=0;
    bool ready_=false,active_=false;
};
} // namespace armix::builtin_fx
