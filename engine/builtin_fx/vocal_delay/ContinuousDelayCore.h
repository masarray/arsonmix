// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Parameters.h"
#include "DelayLine.h"
#include "AdaptiveVocalFocus.h"
#include "ConcertThrow.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace armix::builtin_fx::vocal_delay {
// No JUCE, bridge, device, or shared graph dependency. Prepare on CONTROL
// before publication; apply/process/invalidate only on AUDIO once published.
class ContinuousDelayCore final {
private:
    struct Taps {DelayLine::Tap main{},half{},threeQuarter{};EchoStyle style=EchoStyle::classic;};
public:
    static constexpr int addedLatencySamples=0;
    class Prepared final {
        friend class ContinuousDelayCore;
        double rate_=0.;
        Taps taps_{};
        float feedback_=0,echo_=0,hp_=0,lp_=1,duckDepth_=0;
        float attack_=0,release_=0,mono_=0,cross_=0;
        bool enabled_=true,adaptiveFocus_=false,spillover_=false,smartStage_=false,hit_=false;
        ThrowMode throwMode_=ThrowMode::off;
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
        const auto makeTap=[](double n){const auto whole=static_cast<std::size_t>(n);return DelayLine::Tap{whole,float(n-double(whole))};};
        q.taps_={makeTap(samples),makeTap(samples*.5),makeTap(samples*.75),p.style};
        const auto performance=performanceSettings(p);
        q.feedback_=performance.feedback;q.echo_=performance.echo;
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
        q.enabled_=p.enabled;q.adaptiveFocus_=p.adaptiveFocus;q.spillover_=p.spillover;q.smartStage_=p.smartStage;q.throwMode_=p.throwMode;q.hit_=p.hit;
        out=q;return true;
    }
    bool prepare(double rate,const Parameters& p=Parameters{}) {
        Prepared q;
        if(!prepareEdit(p,rate,q))return false;
        ready_=false;
        const auto capacity=static_cast<std::size_t>(std::ceil(rate*4.))+2;
        left_.prepare(capacity);right_.prepare(capacity);throw_.prepare(rate);
        activeThrow_=q.throwMode_;switchGain_=1.f;
        rate_=rate;target_=q;active_=next_=q.taps_;
        fadeLength_=static_cast<std::uint32_t>(std::ceil(rate*.03));
        fadePosition_=fadeLength_;
        slew_=timeCoefficient(rate,15.f);
        powerStep_=float(1./std::ceil(rate*.01));
        feedback_=q.feedback_;echo_=q.echo_;hp_=q.hp_;lp_=q.lp_;
        mono_=q.mono_;cross_=q.cross_;depth_=q.duckDepth_;
        wetTone_=q.taps_.style==EchoStyle::classic?0.f:1.f;
        tape_=q.taps_.style==EchoStyle::tape?1.f:0.f;
        stageBlend_=q.smartStage_?1.f:0.f;
        sustainAttack_=timeCoefficient(rate,200.f);sustainRelease_=timeCoefficient(rate,90.f);
        headroomRelease_=timeCoefficient(rate,65.f);
        focus_.prepare(rate);focusBlend_=q.adaptiveFocus_?1.f:0.f;
        power_=q.enabled_?1.f:0.f;tailLimit_=static_cast<std::uint32_t>(std::ceil(rate*30.));ready_=true;
        invalidate();return true;
    }
    bool applyPrepared(const Prepared& q) noexcept {
        if(!ready_||q.rate_!=rate_)return false;
        // Only an actual ON->OFF transition may arm a tail. Repeated edits
        // while OFF cannot extend it, nor resurrect cleared history.
        if(target_.enabled_&&!q.enabled_&&q.spillover_&&power_>0.f)tailRemaining_=tailLimit_;
        if(q.enabled_||!q.spillover_)tailRemaining_=0;
        target_=q;return true; // latest target; no unbounded edit queue in DSP
    }
    // AUDIO only, or CONTROL on an unpublished instance.
    void invalidate() noexcept {
        left_.invalidate();right_.invalidate();throw_.reset();tailRemaining_=0;
        toneL_={};toneR_={};wetL_={};wetR_={};env_=0.f;sustain_=0.f;headroom_=1.f;focus_.reset();publishMeter(0,0);
        active_=next_=target_.taps_;fadePosition_=fadeLength_;
    }
    std::size_t ringBytes() const noexcept {
        return (left_.capacity()+right_.capacity())*sizeof(float)+throw_.ringBytes();
    }
    // One coherent block observation, packed in a lock-free word. 10 bits each
    // for wet peak and fractional attenuation, 10-bit rolling block sequence, 2-bit throw status.
    struct Meter {float wetPeak=0,duckReduction=0;std::uint32_t sequence=0;ConcertThrow::Status throwStatus=ConcertThrow::Status::armed;};
    Meter meter() const noexcept{
        const auto w=meterWord_.load(std::memory_order_relaxed);
        return {float(w&1023u)/1023.f,float((w>>10)&1023u)/1023.f,(w>>20)&1023u,static_cast<ConcertThrow::Status>(w>>30)};
    }
    void processStereo(float* l,float* r,std::size_t frames) noexcept {
        if(!ready_||!l||!r||frames==0)return;
        const bool monoBuffer=l==r;
        if(!target_.enabled_&&power_==0.f){
            // Preserve previous finite-input contract, but skip all wet work.
            for(std::size_t i=0;i<frames;++i){l[i]=finite(l[i]);if(!monoBuffer)r[i]=finite(r[i]);}
            publishMeter(0,0);return;
        }
        float blockWet=0,blockReduction=0;
        for(std::size_t i=0;i<frames;++i) {
            const float dryL=finite(l[i]),dryR=monoBuffer?dryL:finite(r[i]);
            const bool switching=activeThrow_!=target_.throwMode_;
            if(switching){
                switchGain_=std::max(0.f,switchGain_-powerStep_);
                if(switchGain_==0.f){
                    // Wet-only fade before O(1) retirement; no prior lyrics leak into throw mode.
                    left_.invalidate();right_.invalidate();throw_.reset();toneL_={};toneR_={};wetL_={};wetR_={};
                    activeThrow_=target_.throwMode_;
                }
            }else switchGain_=std::min(1.f,switchGain_+powerStep_);
            // Dry is never buffered, filtered, or limited by the echo engine.
            if(!target_.enabled_&&power_==0.f) {
                l[i]=dryL;if(!monoBuffer)r[i]=dryR;continue;
            }
            move(feedback_,target_.feedback_);move(echo_,target_.echo_);
            move(hp_,target_.hp_);move(lp_,target_.lp_);
            move(mono_,target_.mono_);move(cross_,target_.cross_);
            move(depth_,target_.duckDepth_);
            const bool spilling=!target_.enabled_&&tailRemaining_>0;
            if(spilling)--tailRemaining_;
            else power_=target_.enabled_?std::min(1.f,power_+powerStep_):std::max(0.f,power_-powerStep_);
            if(power_==0.f&&!target_.enabled_) {
                invalidate();l[i]=dryL;if(!monoBuffer)r[i]=dryR;continue;
            }
            if(fadePosition_==fadeLength_&&!same(active_,target_.taps_)) {
                next_=target_.taps_;fadePosition_=0;
            }
            auto a=read(active_);
            if(fadePosition_<fadeLength_) {
                const auto b=read(next_);
                const float mix=float(++fadePosition_)/float(fadeLength_);
                a.l+=(b.l-a.l)*mix;a.r+=(b.r-a.r)*mix;
                a.wl+=(b.wl-a.wl)*mix;a.wr+=(b.wr-a.wr)*mix;
                if(fadePosition_==fadeLength_)active_=next_;
            }
            const float dL=a.l,dR=a.r;
            move(wetTone_,target_.taps_.style==EchoStyle::classic?0.f:1.f);
            move(tape_,target_.taps_.style==EchoStyle::tape?1.f:0.f);
            float wL=a.wl,wR=a.wr;
            if(wetTone_!=0.f){
                const float tL=wetL_.tick(wL,hp_,lp_),tR=wetR_.tick(wR,hp_,lp_);
                wL+=(tL-wL)*wetTone_;wR+=(tR-wR)*wetTone_;
            }
            if(tape_!=0.f){
                // Bounded soft saturation of wet only, no oversampled path.
                wL+=(wL/(1.f+.45f*std::abs(wL))-wL)*tape_;
                wR+=(wR/(1.f+.45f*std::abs(wR))-wR)*tape_;
            }
            // Convex cross-feedback; wet safety bound, not a dry limiter.
            // Mono input/return folds with an average, never +6 dB summation.
            const float safeL=bounded(dryL,4.f),safeR=bounded(dryR,4.f);
            float srcL=safeL,srcR=safeR;
            if(activeThrow_!=ThrowMode::off){
                if(target_.enabled_&&!switching){
                    const auto captured=throw_.tick(safeL,safeR,activeThrow_,target_.hit_);
                    srcL=captured.l;srcR=captured.r;
                }else{throw_.reset();srcL=srcR=0.f;}
            }
            const float fold=.5f*(srcL+srcR);
            float inL=srcL+(fold-srcL)*mono_;
            float inR=srcR+(fold-srcR)*mono_;
            if(target_.smartStage_||stageBlend_!=0.f)move(stageBlend_,target_.smartStage_?1.f:0.f);
            // Seed one delay line, even for identical stereo microphone PCM.
            // Average fold avoids a +6 dB mono sum; old sessions retain their topology.
            const float seed=stageBlend_*cross_;
            inL+=(fold-inL)*seed;inR*=1.f-seed;
            const float fL=toneL_.tick(dL+(dR-dL)*cross_,hp_,lp_);
            const float fR=toneR_.tick(dR+(dL-dR)*cross_,hp_,lp_);
            // Stop feeding new audio on OFF; ramp the wet return, then invalidate.
            const float feed=target_.enabled_&&!switching?power_:0.f;
            const float peak=std::min(1.f,std::max(std::abs(dryL),std::abs(dryR)));
            env_=tiny(env_+(peak-env_)*(peak>env_?target_.attack_:target_.release_));
            // Soft activity ramp: full duck at ~-18 dBFS, never hard-gate echo.
            float activity=std::clamp((env_-.015f)/.11f,0.f,1.f);
            move(focusBlend_,target_.adaptiveFocus_?1.f:0.f);
            if(focusBlend_>0.f){
                const float adaptive=focus_.tick(peak,target_.attack_,target_.release_,target_.smartStage_);
                activity+=(adaptive-activity)*focusBlend_;
            }else focus_.reset();
            // A causal sustained-phrase budget, not lyric recognition. Attenuate
            // injection and recirculation while singing, retain fresh phrase energy.
            // No hard gate or buffer clear at phrase boundaries; dry stays untouched.
            const float stageFocus=stageBlend_*focusBlend_;
            if(stageFocus>0.f)sustain_=tiny(sustain_+(activity-sustain_)*(activity>sustain_?sustainAttack_:sustainRelease_));
            else sustain_=0.f;
            const float budget=activeThrow_==ThrowMode::off?stageFocus*depth_*sustain_:0.f;
            const float send=feed*(1.f-.45f*budget); // retain more phrase-end energy
            const float regen=feedback_*(1.f-.45f*budget);
            left_.push(bounded(inL*send+regen*fL,8.f));
            right_.push(bounded(inR*send+regen*fR,8.f));
            const float reduction=depth_*activity;
            const float gain=echo_*power_*(1.f-reduction)*switchGain_;
            const float mid=.5f*(wL+wR);
            if(gain==0.f) {
                l[i]=dryL;if(!monoBuffer)r[i]=dryR;
                headroom_=1.f;
            }else {
                float wetL=gain*(monoBuffer?mid:wL+(mid-wL)*mono_);
                float wetR=monoBuffer?wetL:gain*(wR+(mid-wR)*mono_);
                if(stageBlend_>0.f){
                    // Linked wet-only peak budget. Never limit, delay or turn
                    // down dry; already-hot dry remains the mixer's responsibility.
                    // Immediate protection, causal slow recovery, no lookahead.
                    const float cap=std::min(wetAllowance(dryL,wetL),wetAllowance(dryR,wetR));
                    headroom_=cap<headroom_?cap:headroom_+headroomRelease_*(cap-headroom_);
                    const float scale=1.f-stageBlend_*(1.f-headroom_);
                    wetL*=scale;wetR*=scale;
                }else headroom_=1.f;
                l[i]=dryL+wetL;if(!monoBuffer)r[i]=dryR+wetR;
                blockWet=std::max(blockWet,std::max(std::abs(wetL),std::abs(wetR)));
            }
            if(gain>0.f)blockReduction=std::max(blockReduction,reduction);
        }
        publishMeter(blockWet,blockReduction);
    }
private:
    void publishMeter(float wet,float reduction) noexcept{
        const auto quant=[](float v){return static_cast<std::uint32_t>(std::clamp(v,0.f,1.f)*1023.f+.5f);};
        meterSequence_=(meterSequence_+1u)&1023u;
        meterWord_.store((std::uint32_t(throw_.status())<<30)|(meterSequence_<<20)|(quant(reduction)<<10)|quant(wet),std::memory_order_relaxed);
    }
    static float wetAllowance(float dry,float wet) noexcept {
        // Do not allow wet to increase an existing over-ceiling peak.
        const float limit=std::max(1.f,std::abs(dry));
        if(wet>0.f&&wet>limit-dry)return std::max(0.f,(limit-dry)/wet);
        if(wet<0.f&&-wet>limit+dry)return std::max(0.f,(limit+dry)/(-wet));
        return 1.f;
    }
    static float finite(float v) noexcept { return std::isfinite(v)?v:0.f; }
    static float bounded(float v,float limit) noexcept {return std::clamp(v,-limit,limit);}
    static float tiny(float v) noexcept {return std::abs(v)<1.e-20f?0.f:v;}
    static float timeCoefficient(double rate,float ms) noexcept {
        return float(-std::expm1(-1./(rate*double(ms)*.001)));
    }
    static bool same(DelayLine::Tap a,DelayLine::Tap b) noexcept {
        return a.whole==b.whole&&a.fraction==b.fraction;
    }
    static bool same(const Taps& a,const Taps& b) noexcept{return same(a.main,b.main)&&a.style==b.style;}
    struct Reads {float l,r,wl,wr;};
    Reads read(const Taps& t) const noexcept{
        const float l=left_.read(t.main),r=right_.read(t.main);
        // Secondary heads are wet-only, no feedback gain multiplication.
        // Weights sum to one. Dual is asymmetric wet timing, not two engines.
        if(t.style==EchoStyle::dual)return {l,r,l,right_.read(t.threeQuarter)};
        if(t.style==EchoStyle::multitap)return {l,r,
            .65f*l+.25f*left_.read(t.half)+.10f*left_.read(t.threeQuarter),
            .65f*r+.25f*right_.read(t.half)+.10f*right_.read(t.threeQuarter)};
        return {l,r,l,r};
    }
    void move(float& v,float target) const noexcept {
        if(v==target)return;
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
    Tone toneL_,toneR_,wetL_,wetR_;
    Prepared target_{};
    Taps active_{},next_{};
    double rate_=0.;
    std::uint32_t fadeLength_=1,fadePosition_=1,tailLimit_=0,tailRemaining_=0;
    float slew_=0,powerStep_=0,power_=0;
    float feedback_=0,echo_=0,hp_=0,lp_=1,mono_=0,cross_=0,depth_=0,env_=0;
    float wetTone_=0.f,tape_=0.f,focusBlend_=0.f;
    float stageBlend_=0.f,sustain_=0.f,sustainAttack_=0.f,sustainRelease_=0.f;
    float headroom_=1.f,headroomRelease_=0.f;
    AdaptiveVocalFocus focus_{};
    ConcertThrow throw_{};ThrowMode activeThrow_=ThrowMode::off;float switchGain_=1.f;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    std::atomic<std::uint32_t> meterWord_{0};std::uint32_t meterSequence_=0;
    bool ready_=false;
};
} // namespace armix::builtin_fx::vocal_delay
