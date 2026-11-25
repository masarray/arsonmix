// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include "DeEzzerContext.h"
namespace armix::builtin_fx::de_ezzer {
enum class Mode { Auto, Voice };
enum class Shape { Shelf, Focused };
struct Settings {
    float amount = .5f, sensitivity = .5f, rangeDb = 8.f;
    bool enabled = true;
    Mode mode = Mode::Auto;
    Shape shape = Shape::Shelf;
};
namespace detail {
struct Band {
    double b0=0,b2=0,a1=0,a2=0,z1=0,z2=0;
    void prepare(double rate,double hz,double q) noexcept {
        const double w=2*std::numbers::pi*hz/rate, a=std::sin(w)/(2*q);
        b0=a/(1+a); b2=-b0; a1=-2*std::cos(w)/(1+a); a2=(1-a)/(1+a); reset();
    }
    void reset() noexcept { z1=z2=0; }
    double tick(double x) noexcept {
        const double y=b0*x+z1;
        z1=-a1*y+z2; z2=b2*x-a2*y;
        if(std::abs(z1)<1e-25)z1=0;
        if(std::abs(z2)<1e-25)z2=0;
        return y;
    }
};
struct Channel {
    Band body,lower,upper,witness;
    Context context;
    // Causal two-lag residual prediction error, evaluated only in explicit Voice.
    double prev=0,prev2=0,ep=0,es=0,cross=0;
    void resetVoice() noexcept {prev=prev2=ep=es=cross=0;}
    void voice(double x,double a) noexcept {
        const double h=x-low, sum=h+prev2;
        ep=energy(ep,prev,a);es=energy(es,sum,a);
        cross+=a*(prev*sum-cross);if(std::abs(cross)<1e-25)cross=0;
        prev2=prev;prev=h;
    }
    double voiceAuthority(double hf) const noexcept {
        if(ep<=1e-10||es<=1e-10)return 0;
        const double residual=std::clamp((es-cross*cross/ep)/(ep+1e-10),0.,4.);
        return std::clamp((residual-.12)/.35,0.,1.)*std::clamp((hf-1e-7)/9e-7,0.,1.);
    }
    double eBody=0,eLower=0,eUpper=0,eWitness=0,low=0,lowerSample=0,upperSample=0;
    void reset() noexcept {body.reset();lower.reset();upper.reset();witness.reset();context.reset();resetVoice();eBody=eLower=eUpper=eWitness=low=lowerSample=upperSample=0;}
    static double energy(double prior,double x,double a) noexcept {
        const double v=prior+a*(x*x-prior);return v<1e-25?0:v;
    }
    void analyze(double x,double a,double lp) noexcept {
        eBody=energy(eBody,body.tick(x),a);
        lowerSample=lower.tick(x);upperSample=upper.tick(x);
        eLower=energy(eLower,lowerSample,a);
        eUpper=energy(eUpper,upperSample,a);
        eWitness=energy(eWitness,witness.tick(x),a);
        low+=lp*(x-low);if(std::abs(low)<1e-25)low=0;
    }
};
} // namespace detail
// Single audio owner. prepare/reset only while quiescent; settings at audio boundary.
class Core final {
public:
    bool prepare(double rate,int channels) noexcept {
        if(!std::isfinite(rate)||rate<44100||rate>96000||channels<1||channels>2)return false;
        channels_=channels;warmupSamples_=static_cast<int>(rate*.001);
        auto coeff=[rate](double ms){return -std::expm1(-1000/(rate*ms));};
        focusSmooth_=coeff(8);envelope_=coeff(1.2);attack_=coeff(.65);release_=coeff(45);lowPass_=coeff(1000/(2*std::numbers::pi*3200));
        controlPeriod_=std::max(1,static_cast<int>(std::round(rate/6000.)));
        memoryDecay_=std::exp(-controlPeriod_/(rate*.085));
        adapt_= -std::expm1(-controlPeriod_/(rate*.12));
        for(auto& c:ch_){c.witness.prepare(rate,600,1.5);c.body.prepare(rate,950,.55);c.lower.prepare(rate,4600,1.1);c.upper.prepare(rate,8000,1.1);}
        reset();return true;
    }
    void reset() noexcept {for(auto& c:ch_)c.reset();gain_=target_=1;focus_=settings_.shape==Shape::Focused?1.:0.;weight_=weightTarget_=.5;phase_=0;warmup_=warmupSamples_;}
    bool setSettings(Settings s) noexcept {
        if(!std::isfinite(s.amount)||!std::isfinite(s.sensitivity)||!std::isfinite(s.rangeDb)||s.amount<0||s.amount>1||s.sensitivity<0||s.sensitivity>1||s.rangeDb<0||s.rangeDb>18)return false;
        if(s.mode!=Mode::Auto&&s.mode!=Mode::Voice)return false;
        if(s.shape!=Shape::Shelf&&s.shape!=Shape::Focused)return false;
        if(s.mode!=settings_.mode)for(auto& c:ch_)c.resetVoice();
        settings_=s;thresholdRatio_=std::pow(10.,(2-12*s.sensitivity)/10.);return true;
    }
    Settings settings() const noexcept {return settings_;}
    static constexpr int latencySamples() noexcept {return 0;}
    float focusLowerWeight() const noexcept {return static_cast<float>(weight_);}
    float bandReductionDb() const noexcept {return static_cast<float>(-20*std::log10(gain_));}
    void process(float* l,float* r,int n) noexcept {
        if(!channels_||!l||n<=0||(channels_==2&&(!r||r==l)))return;
        for(int i=0;i<n;++i){
            std::array<double,2> x{sanitize(l[i]),channels_==2?sanitize(r[i]):0};
            for(int c=0;c<channels_;++c){
                ch_[c].analyze(x[c],envelope_,lowPass_);
                if(settings_.mode==Mode::Voice)ch_[c].voice(x[c],envelope_);
            }
            if(warmup_>0)--warmup_;
            if(phase_==0&&warmup_==0){
                double excess=0;
                const bool trackFocus=settings_.shape==Shape::Focused||focus_>0;
                for(int c=0;c<channels_;++c){
                    auto& a=ch_[c];const double hf=std::max(a.eLower,a.eUpper);
                    double channelExcess=a.context.excess(a.eBody,hf,a.eWitness,thresholdRatio_,memoryDecay_,adapt_);
                    if(settings_.mode==Mode::Voice){
                        const double authority=a.voiceAuthority(hf);
                        const double limit=std::max(1.,thresholdRatio_);
                        const double ratio=hf/(a.eBody+1e-10);
                        if(authority>0&&ratio>limit)channelExcess=std::max(channelExcess,authority*std::min(12.,10*std::log10(ratio/limit)));
                    }
                    if(channelExcess>excess){
                        excess=channelExcess;
                        if(trackFocus){
                            const double w=a.eLower/(a.eLower+a.eUpper+1e-25);
                            const double lo=w*w,hi=(1-w)*(1-w);
                            weightTarget_=lo/(lo+hi);
                        }
                    }
                }
                const double db=settings_.enabled?std::min<double>(settings_.rangeDb,1.35*settings_.amount*excess):0;
                target_=db>0?std::pow(10.,-db/20):1.;
            }
            if(++phase_==controlPeriod_)phase_=0;
            gain_+=(target_<gain_?attack_:release_)*(target_-gain_);
            if(std::abs(target_-gain_)<1e-7)gain_=target_;
            const double shapeTarget=settings_.shape==Shape::Focused?1.:0.;
            if(focus_!=shapeTarget){focus_+=focusSmooth_*(shapeTarget-focus_);if(std::abs(shapeTarget-focus_)<1e-7)focus_=shapeTarget;}
            if(focus_>0&&weight_!=weightTarget_){weight_+=focusSmooth_*(weightTarget_-weight_);if(std::abs(weightTarget_-weight_)<1e-7)weight_=weightTarget_;}
            l[i]=output(x[0],ch_[0]);if(channels_==2)r[i]=output(x[1],ch_[1]);
        }
    }
private:
    static double sanitize(float x) noexcept {return std::isfinite(x)?static_cast<double>(x):0.;}
    float output(double x,const detail::Channel& c) const noexcept {
        // Complementary residual: exact dry at unity, no delayed dry path.
        double y=gain_==1?x:gain_*x+(1-gain_)*c.low;
        if(focus_>0&&gain_!=1){
            const double band=weight_*c.lowerSample+(1-weight_)*c.upperSample;
            const double focused=x-(1-gain_)*band;
            y+=focus_*(focused-y);
        }
        return static_cast<float>(std::clamp(y,-static_cast<double>(3.402823466e38f),static_cast<double>(3.402823466e38f)));
    }
    Settings settings_{};
    std::array<detail::Channel,2> ch_{};
    int channels_=0,phase_=0,controlPeriod_=8,warmup_=0,warmupSamples_=0;
    double focusSmooth_=0,focus_=0,weight_=.5,weightTarget_=.5;
    double memoryDecay_=0,adapt_=0,envelope_=0,attack_=0,release_=0,lowPass_=0,thresholdRatio_=.3981071705534972,gain_=1,target_=1;
};
} // namespace armix::builtin_fx::de_ezzer
