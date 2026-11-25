// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "DeEzzerCore.h"
#include <functional>
#include <cstdint>
#include <limits>
namespace armix::builtin_fx::de_ezzer {
enum class CueMode { Mix, Ess, Sidechain, Delta };
struct Observation {
    static constexpr std::size_t bands=5;
    std::array<float,bands> inputDb{},wetDb{};
    float essDb=0,sidechainDb=0,deltaDb=0;
    bool valid=false;
};
// Optional observation and separate audition PCM. The caller owns destination
// buffers and their routing. This never replaces the programme PCM.
class Monitor final {
public:
    static constexpr std::array<double,Observation::bands> centers{3200.,4600.,6300.,8800.,12000.};
    bool prepare(double rate,int channels) noexcept {
        if(!std::isfinite(rate)||rate<44100||rate>96000||channels<1||channels>2)return false;
        channels_=channels;alpha_=-std::expm1(-1./(.030*rate));
        minimumFrames_=static_cast<unsigned>(rate*.030);cueFadeFrames_=std::max(1,static_cast<int>(rate*.005));
        for(auto& c:filters_)for(std::size_t k=0;k<centers.size();++k){
            c.input[k].prepare(rate,centers[k],1.3);c.wet[k].prepare(rate,centers[k],1.3);
        }
        reset();return true;
    }
    void reset() noexcept {
        for(auto& c:filters_){for(auto& b:c.input)b.reset();for(auto& b:c.wet)b.reset();}
        input_.fill(0);wet_.fill(0);ess_=side_=delta_=0;frames_=0;
        cueWeights_.fill(0);cueSteps_.fill(0);cueRemaining_=0;cueActive_=false;
    }
    Observation read() const noexcept {
        Observation out;
        if(!channels_||frames_<minimumFrames_)return out;
        for(std::size_t k=0;k<centers.size();++k){out.inputDb[k]=db(input_[k]);out.wetDb[k]=db(wet_[k]);}
        out.essDb=db(ess_);out.sidechainDb=db(side_);out.deltaDb=db(delta_);out.valid=true;
        return out;
    }
    bool render(Core& core,float* l,float* r,int n,CueMode mode=CueMode::Mix,
                float* cueL=nullptr,float* cueR=nullptr) noexcept {
        if(!channels_||!l||n<1||n>4096||
           (channels_==2&&(!r||overlap(l,r,n)))||
           (channels_==1&&(r||cueR))||
           (mode!=CueMode::Mix&&mode!=CueMode::Ess&&mode!=CueMode::Sidechain&&mode!=CueMode::Delta))return false;
        if(cueL||cueR){
            if(!cueL||overlap(l,cueL,n)||(channels_==2&&
               (!cueR||overlap(r,cueL,n)||overlap(l,cueR,n)||overlap(r,cueR,n)||overlap(cueL,cueR,n))))return false;
        }
        if(cueL){
            if(!cueActive_||mode!=cueMode_){
                if(!cueActive_)cueWeights_.fill(0);
                cueActive_=true;cueMode_=mode;cueRemaining_=cueFadeFrames_;
                const auto selected=static_cast<std::size_t>(mode);
                for(std::size_t k=0;k<cueWeights_.size();++k)cueSteps_[k]=((k==selected?1.:0.)-cueWeights_[k])/cueFadeFrames_;
            }
        }else cueActive_=false;
        constexpr int chunk=128;
        const double scale=1./channels_;
        for(int pos=0;pos<n;){
            const int count=std::min(chunk,n-pos);
            std::array<float,chunk> dryL{},dryR{},sideL{},sideR{},essL{},essR{};
            for(int j=0;j<count;++j){dryL[j]=clean(l[pos+j]);if(channels_==2)dryR[j]=clean(r[pos+j]);}
            core.processWithTaps(l+pos,channels_==2?r+pos:nullptr,count,sideL.data(),sideR.data(),essL.data(),essR.data());
            for(int j=0;j<count;++j){
                if(cueL&&cueRemaining_>0){
                    for(std::size_t k=0;k<cueWeights_.size();++k)cueWeights_[k]+=cueSteps_[k];
                    if(--cueRemaining_==0){cueWeights_.fill(0);cueWeights_[static_cast<std::size_t>(cueMode_)]=1.;}
                }
                const std::array<double,2> dry{dryL[j],dryR[j]},wet{l[pos+j],channels_==2?r[pos+j]:0.f};
                const std::array<double,2> side{sideL[j],sideR[j]},ess{essL[j],essR[j]};
                std::array<double,Observation::bands> inputPower{},wetPower{};
                double sidePower=0,essPower=0,deltaPower=0;
                for(int c=0;c<channels_;++c){
                    auto& f=filters_[c];
                    for(std::size_t k=0;k<centers.size();++k){
                        const double a=f.input[k].tick(dry[c]),b=f.wet[k].tick(wet[c]);
                        inputPower[k]+=a*a;wetPower[k]+=b*b;
                    }
                    const double delta=dry[c]-wet[c];
                    sidePower+=side[c]*side[c];essPower+=ess[c]*ess[c];deltaPower+=delta*delta;
                    if(cueL){
                        const double y=cueWeights_[0]*wet[c]+cueWeights_[1]*ess[c]+cueWeights_[2]*side[c]+cueWeights_[3]*delta;
                        // Separate operator signal has fixed -12 dB headroom and
                        // bounded peak, never automatic gain or audience mixing.
                        (c==0?cueL:cueR)[pos+j]=static_cast<float>(std::clamp(y*.251188643150958,-.5,.5));
                    }
                }
                for(std::size_t k=0;k<centers.size();++k){
                    smooth(input_[k],inputPower[k]*scale);smooth(wet_[k],wetPower[k]*scale);
                }
                smooth(side_,sidePower*scale);smooth(ess_,essPower*scale);smooth(delta_,deltaPower*scale);
            }
            pos+=count;
        }
        frames_=std::min<std::uint64_t>(static_cast<std::uint64_t>(frames_)+static_cast<unsigned>(n),minimumFrames_);
        return true;
    }
private:
    struct Filters {std::array<detail::Band,Observation::bands> input{},wet{};};
    static bool overlap(const float* a,const float* b,int n) noexcept {
        const std::less<const float*> less;
        return less(a,b+n)&&less(b,a+n);
    }
    static float clean(float x) noexcept {return std::isfinite(x)?x:0.f;}
    static float db(double x) noexcept {return static_cast<float>(std::clamp(10.*std::log10(std::max(x,1e-12)),-120.,120.));}
    void smooth(double& x,double v) const noexcept {x+=alpha_*(v-x);if(x<1e-25)x=0;}
    std::array<Filters,2> filters_{};
    std::array<double,Observation::bands> input_{},wet_{};
    double alpha_=0,ess_=0,side_=0,delta_=0;
    std::array<double,4> cueWeights_{},cueSteps_{};
    CueMode cueMode_=CueMode::Mix;bool cueActive_=false;int cueFadeFrames_=1,cueRemaining_=0;
    unsigned minimumFrames_=0;std::uint64_t frames_=0;int channels_=0;
};
} // namespace armix::builtin_fx::de_ezzer
