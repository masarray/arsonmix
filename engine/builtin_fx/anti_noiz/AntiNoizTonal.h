// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
namespace armix::builtin_fx::anti_noiz {
// Past-only Learn search, fixed grid, one linked line. The fine grid is
// oversampled relative to the 200ms analysis resolution, not super-resolution.
// At runtime a level guard releases attenuation on strong coincident energy.
class TonalNoise final {
public:
    static constexpr int side=6, count=65, bank=count+2*side;
    static constexpr float step=2.5f;
    static constexpr float hz(int i) noexcept {return 40.f+step*float(i-side);}
    void prepare(double rate,int channels) noexcept {
        channels_=channels;
        mixAlpha_=1.-std::exp(-1./(.01*rate));
        guardAlpha_=1.-std::exp(-1./(.035*rate));
        stride_=std::max(1,int(std::lround(rate/2000.)));
        const double reduced=rate/stride_;
        window_=std::max(1,int(std::lround(.2*reduced)));
        for(int i=0;i<bank;++i)
            coef_[i]=2.*std::cos(2.*std::numbers::pi*hz(i)/reduced);
        for(int i=0;i<count;++i){
            const double f=hz(i+side),w=2.*std::numbers::pi*f/rate;
            const double alpha=std::sin(w)/(2.*(f/4.));
            auto& k=filter_[i];k.b=alpha/(1.+alpha);
            k.a1=-2.*std::cos(w)/(1.+alpha);k.a2=(1.-alpha)/(1.+alpha);
        }
        selected_=-1;trustedPower_={};begin();
    }
    void begin() noexcept {
        votes_={};score_={};candidatePower_={};windows_=0;phase_=0;partial_={};clearWindow();resetSignal();
    }
    void resetSignal() noexcept {states_={};livePower_={};mix_=0.;}
    void observe(float l,float r) noexcept {
        partial_[0]+=l;if(channels_==2)partial_[1]+=r;
        if(++phase_<stride_)return;
        phase_=0;
        for(int c=0;c<channels_;++c){
            const double x=partial_[c]/stride_;partial_[c]=0.;energy_[c]+=x*x;
            for(int i=0;i<bank;++i){
                auto& s=go_[c][i];const double v=x+coef_[i]*s[0]-s[1];s[1]=s[0];s[0]=v;
            }
        }
        if(++frames_<window_)return;
        ++windows_;
        std::array<std::array<double,bank>,2> power{};
        for(int c=0;c<channels_;++c)for(int i=0;i<bank;++i){
            const auto& s=go_[c][i];
            power[c][i]=std::max(0.,s[0]*s[0]+s[1]*s[1]-coef_[i]*s[0]*s[1]);
        }
        for(int i=0;i<count;++i){
            const int k=i+side;
            bool qualified=true;double strength=1.;
            for(int c=0;c<channels_;++c){
                const auto& p=power[c];const double center=p[k],sides=std::max(p[k-side],p[k+side]);
                const double fraction=2.*center/std::max(1e-18,double(window_)*energy_[c]);
                qualified&=energy_[c]>1e-8&&fraction>=.35&&center>=8.*std::max(sides,1e-15);
                // Suppress side-lobe votes; tolerate neighboring grid ties
                // for a line between grid points without treating it as drift.
                qualified&=center>=.8*std::max(p[k-1],p[k+1]);
                strength=std::min(strength,fraction);
            }
            if(qualified){
                ++votes_[i];score_[i]+=strength;
                for(int c=0;c<channels_;++c)
                    candidatePower_[i][c]+=2.*power[c][k]/(double(window_)*window_);
            }
        }
        clearWindow();
    }
    // Accepted broadband Learn is the sole commit fence. Cancel/reject keeps
    // both trusted frequency and level. No coefficient design on audio thread.
    void commit() noexcept {
        selected_=-1;trustedPower_={};double best=0.;
        if(windows_>=8)for(int i=0;i<count;++i)
            if(votes_[i]*10>=windows_*8&&score_[i]>best){selected_=i;best=score_[i];}
        if(selected_>=0)for(int c=0;c<channels_;++c)
            trustedPower_[c]=candidatePower_[selected_][c]/votes_[selected_];
        resetSignal();
    }
    int selected() const noexcept {return selected_;}
    float frequency() const noexcept {return selected_>=0?hz(selected_+side):0.f;}
    // Strong line excess may be a note rather than noise. Use the maximum
    // channel evidence for linked release; never reinterpret it as a pitch
    // classifier or adapt the trusted learned level during playback.
    void apply(float& l,float& r,float amount) noexcept {
        if(selected_<0)return;
        const double y0=bandpass(l,0);
        const double y1=channels_==2?bandpass(r,1):0.;
        const std::array<double,2> y{y0,y1};
        double excess=0.;
        for(int c=0;c<channels_;++c){
            auto& p=livePower_[c];p+=guardAlpha_*(y[c]*y[c]-p);
            if(p<1e-25)p=0.;
            excess=std::max(excess,p/std::max(1e-15,trustedPower_[c]));
        }
        const double confidence=1.-std::clamp((excess-1.5)/2.5,0.,1.);
        const double target=.292893218*std::clamp(double(amount),0.,1.)*confidence;
        mix_+=mixAlpha_*(target-mix_);
        l=float(double(l)-mix_*y0);
        if(channels_==2)r=float(double(r)-mix_*y1);
    }
private:
    struct Coeff {double b=0.,a1=0.,a2=0.;};
    struct State {double x1=0.,x2=0.,y1=0.,y2=0.;};
    double bandpass(float x,int c) noexcept {
        auto& s=states_[c];const auto& k=filter_[selected_];
        const double y=k.b*(double(x)-s.x2)-k.a1*s.y1-k.a2*s.y2;
        s.x2=s.x1;s.x1=x;s.y2=s.y1;s.y1=std::abs(y)<1e-25?0.:y;
        return y;
    }
    void clearWindow() noexcept {frames_=0;energy_={};go_={};}
    int channels_=1,stride_=24,window_=400,frames_=0,phase_=0,windows_=0,selected_=-1;
    double mix_=0.,mixAlpha_=0.,guardAlpha_=0.;
    std::array<double,bank> coef_{};
    std::array<Coeff,count> filter_{};
    std::array<State,2> states_{};
    std::array<std::array<std::array<double,2>,bank>,2> go_{};
    std::array<double,2> energy_{},partial_{},trustedPower_{},livePower_{};
    std::array<int,count> votes_{};
    std::array<double,count> score_{};
    std::array<std::array<double,2>,count> candidatePower_{};
};
} // namespace armix::builtin_fx::anti_noiz
