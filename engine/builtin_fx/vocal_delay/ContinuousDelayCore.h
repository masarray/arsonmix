// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Parameters.h"
#include "DelayLine.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace armix::builtin_fx::vocal_delay {
// No JUCE, bridge, device, or shared graph dependency. Prepare on CONTROL
// before publication; apply/process/invalidate only on AUDIO once published.
class ContinuousDelayCore final {
public:
    static constexpr int addedLatencySamples=0;
    class Prepared final {
        friend class ContinuousDelayCore;
        double rate_=0.;
        DelayLine::Tap tap_{};
        float feedback_=0,echo_=0,hp_=0,lp_=1,duckDepth_=0;
        float attack_=0,release_=0,mono_=0,cross_=0;
        bool enabled_=true;
    public:
        Prepared()=default;
    };
    ContinuousDelayCore()=default;
    ContinuousDelayCore(const ContinuousDelayCore&)=delete;
    ContinuousDelayCore& operator=(const ContinuousDelayCore&)=delete;
    static bool prepareEdit(const Parameters& p,double rate,Prepared& out) noexcept {
        if(!valid(p)||!validRate(rate))return false;
        Prepared q;
        q.rate_=rate;
        const double samples=rate*double(p.timeMs)*.001;
        q.tap_.whole=static_cast<std::size_t>(samples);
        q.tap_.fraction=float(samples-double(q.tap_.whole));
        q.feedback_=p.feedback;q.echo_=p.echo;
        const auto pole=[rate](float hz) {
            return float(-std::expm1(-6.283185307179586*std::min(double(hz),rate*.45)/rate));
        };
        q.hp_=p.highPassHz==0?0.f:pole(p.highPassHz);
        q.lp_=p.lowPassHz==0?1.f:pole(p.lowPassHz);
        q.duckDepth_=1.f-float(std::pow(10.,-24.*p.duck/20.));
        q.attack_=timeCoefficient(rate,p.duckAttackMs);
        q.release_=timeCoefficient(rate,p.duckReleaseMs);
        q.mono_=p.mode==StereoMode::mono?1.f:0.f;
        q.cross_=p.mode==StereoMode::pingPong?1.f:0.f;
        q.enabled_=p.enabled;
        out=q;return true;
    }
    bool prepare(double rate,const Parameters& p=Parameters{}) {
        Prepared q;
        if(!prepareEdit(p,rate,q))return false;
        ready_=false;
        const auto capacity=static_cast<std::size_t>(std::ceil(rate*4.))+2;
        left_.prepare(capacity);right_.prepare(capacity);
        rate_=rate;target_=q;active_=next_=q.tap_;
        fadeLength_=static_cast<std::uint32_t>(std::ceil(rate*.03));
        fadePosition_=fadeLength_;
        slew_=timeCoefficient(rate,15.f);
        powerStep_=float(1./std::ceil(rate*.01));
        feedback_=q.feedback_;echo_=q.echo_;hp_=q.hp_;lp_=q.lp_;
        mono_=q.mono_;cross_=q.cross_;depth_=q.duckDepth_;
        power_=q.enabled_?1.f:0.f;ready_=true;
        invalidate();return true;
    }
    bool applyPrepared(const Prepared& q) noexcept {
        if(!ready_||q.rate_!=rate_)return false;
        target_=q;return true; // latest target; no unbounded edit queue in DSP
    }
    // AUDIO only, or CONTROL on an unpublished instance.
    void invalidate() noexcept {
        left_.invalidate();right_.invalidate();
        toneL_={};toneR_={};env_=0.f;
        active_=next_=target_.tap_;fadePosition_=fadeLength_;
    }
    std::size_t ringBytes() const noexcept {
        return (left_.capacity()+right_.capacity())*sizeof(float);
    }
    void processStereo(float* l,float* r,std::size_t frames) noexcept {
        if(!ready_||!l||!r)return;
        const bool monoBuffer=l==r;
        for(std::size_t i=0;i<frames;++i) {
            const float dryL=finite(l[i]),dryR=monoBuffer?dryL:finite(r[i]);
            // Dry is never buffered, filtered, or limited by the echo engine.
            if(!target_.enabled_&&power_==0.f) {
                l[i]=dryL;if(!monoBuffer)r[i]=dryR;continue;
            }
            move(feedback_,target_.feedback_);move(echo_,target_.echo_);
            move(hp_,target_.hp_);move(lp_,target_.lp_);
            move(mono_,target_.mono_);move(cross_,target_.cross_);
            move(depth_,target_.duckDepth_);
            power_=target_.enabled_?std::min(1.f,power_+powerStep_):std::max(0.f,power_-powerStep_);
            if(power_==0.f&&!target_.enabled_) {
                invalidate();l[i]=dryL;if(!monoBuffer)r[i]=dryR;continue;
            }
            if(fadePosition_==fadeLength_&&!same(active_,target_.tap_)) {
                next_=target_.tap_;fadePosition_=0;
            }
            float dL=left_.read(active_),dR=right_.read(active_);
            if(fadePosition_<fadeLength_) {
                const float mix=float(++fadePosition_)/float(fadeLength_);
                dL+=(left_.read(next_)-dL)*mix;
                dR+=(right_.read(next_)-dR)*mix;
                if(fadePosition_==fadeLength_)active_=next_;
            }
            // Convex cross-feedback; wet safety bound, not a dry limiter.
            // Mono input/return folds with an average, never +6 dB summation.
            const float fold=.5f*(bounded(dryL,4.f)+bounded(dryR,4.f));
            const float inL=bounded(dryL,4.f)+(fold-bounded(dryL,4.f))*mono_;
            const float inR=bounded(dryR,4.f)+(fold-bounded(dryR,4.f))*mono_;
            const float fL=toneL_.tick(dL+(dR-dL)*cross_,hp_,lp_);
            const float fR=toneR_.tick(dR+(dL-dR)*cross_,hp_,lp_);
            // Stop feeding new audio on OFF; ramp the wet return, then invalidate.
            const float feed=target_.enabled_?power_:0.f;
            left_.push(bounded(inL*feed+feedback_*fL,8.f));
            right_.push(bounded(inR*feed+feedback_*fR,8.f));
            const float peak=std::min(1.f,std::max(std::abs(dryL),std::abs(dryR)));
            env_=tiny(env_+(peak-env_)*(peak>env_?target_.attack_:target_.release_));
            // Soft activity ramp: full duck at ~-18 dBFS, never hard-gate echo.
            const float activity=std::clamp((env_-.015f)/.11f,0.f,1.f);
            const float gain=echo_*power_*(1.f-depth_*activity);
            const float mid=.5f*(dL+dR);
            if(gain==0.f) { l[i]=dryL;if(!monoBuffer)r[i]=dryR; }
            else if(monoBuffer)l[i]=dryL+gain*mid;
            else {
                l[i]=dryL+gain*(dL+(mid-dL)*mono_);
                r[i]=dryR+gain*(dR+(mid-dR)*mono_);
            }
        }
    }
private:
    static float finite(float v) noexcept { return std::isfinite(v)?v:0.f; }
    static float bounded(float v,float limit) noexcept {return std::clamp(v,-limit,limit);}
    static float tiny(float v) noexcept {return std::abs(v)<1.e-20f?0.f:v;}
    static float timeCoefficient(double rate,float ms) noexcept {
        return float(-std::expm1(-1./(rate*double(ms)*.001)));
    }
    static bool same(DelayLine::Tap a,DelayLine::Tap b) noexcept {
        return a.whole==b.whole&&a.fraction==b.fraction;
    }
    void move(float& v,float target) const noexcept {
        v+=slew_*(target-v);
        if(std::abs(target-v)<1.e-7f)v=target;
    }
    struct Tone final {
        float low=0.f,filtered=0.f;
        float tick(float x,float hp,float lp) noexcept {
            // When bypassed track input with no stale state on re-enable.
            if(hp==0.f)low=0.f;
            else low=tiny(low+hp*(x-low));
            const float high=x-low;
            filtered=lp==1.f?high:tiny(filtered+lp*(high-filtered));
            return filtered;
        }
    };
    DelayLine left_,right_;
    Tone toneL_,toneR_;
    Prepared target_{};
    DelayLine::Tap active_{},next_{};
    double rate_=0.;
    std::uint32_t fadeLength_=1,fadePosition_=1;
    float slew_=0,powerStep_=0,power_=0;
    float feedback_=0,echo_=0,hp_=0,lp_=1,mono_=0,cross_=0,depth_=0,env_=0;
    bool ready_=false;
};
} // namespace armix::builtin_fx::vocal_delay
