// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
namespace armix::builtin_fx::anti_noiz {
// Low-rate, past-only noise Learn analysis. One conservative linked line;
// not a pitch tracker, not a harmonic canceller, not a speech classifier.
// All arrays have fixed size. Coefficients are prepared off the audio thread.
class TonalNoise final {
public:
    static constexpr int count=6;
    static constexpr std::array<float,count> frequencies{50.f,60.f,100.f,120.f,150.f,180.f};
    void prepare(double rate,int channels) noexcept {
        channels_=channels;
        stride_=std::max(1,int(std::lround(rate/2000.)));
        const double reduced=rate/stride_;
        window_=std::max(1,int(std::lround(.2*reduced)));
        for(int i=0;i<count;++i){
            for(int j=0;j<3;++j){
                const double f=frequencies[i]+(j-1)*15.;
                coef_[i][j]=2.*std::cos(2.*std::numbers::pi*f/reduced);
            }
            // RBJ constant 0 dB peak bandpass, ~4 Hz bandwidth.
            const double w=2.*std::numbers::pi*frequencies[i]/rate;
            const double alpha=std::sin(w)/(2.*(frequencies[i]/4.));
            auto& k=filter_[i];k.b=alpha/(1.+alpha);
            k.a1=-2.*std::cos(w)/(1.+alpha);k.a2=(1.-alpha)/(1.+alpha);
        }
        selected_=-1;begin();resetSignal();
    }
    void begin() noexcept {
        votes_={};score_={};windows_=0;phase_=0;partial_={};clearWindow();resetSignal();
    }
    void resetSignal() noexcept {states_={};mix_=0.;}
    void observe(float l,float r) noexcept {
        partial_[0]+=l;if(channels_==2)partial_[1]+=r;
        if(++phase_<stride_)return;
        phase_=0;
        for(int c=0;c<channels_;++c){
            const double x=partial_[c]/stride_;partial_[c]=0.;energy_[c]+=x*x;
            for(int i=0;i<count;++i)for(int j=0;j<3;++j){
                auto& s=go_[c][i][j];const double v=x+coef_[i][j]*s[0]-s[1];s[1]=s[0];s[0]=v;
            }
        }
        if(++frames_<window_)return;
        ++windows_;
        for(int i=0;i<count;++i){
            bool qualified=true;double strength=1.;
            for(int c=0;c<channels_;++c){
                const auto power=[&](int j){const auto& s=go_[c][i][j];return std::max(0.,s[0]*s[0]+s[1]*s[1]-coef_[i][j]*s[0]*s[1]);};
                const double center=power(1),sides=std::max(power(0),power(2));
                const double fraction=2.*center/std::max(1e-18,double(window_)*energy_[c]);
                qualified&=energy_[c]>1e-8&&fraction>=.35&&center>=8.*std::max(sides,1e-15);
                strength=std::min(strength,fraction);
            }
            if(qualified){++votes_[i];score_[i]+=strength;}
        }
        clearWindow();
    }
    // Only call at accepted broadband Learn transaction boundary. A rejected
    // or canceled Learn never replaces the previous trusted line.
    void commit() noexcept {
        selected_=-1;double best=0.;
        if(windows_>=8)for(int i=0;i<count;++i)
            if(votes_[i]*10>=windows_*8&&score_[i]>best){selected_=i;best=score_[i];}
        resetSignal();
    }
    int selected() const noexcept {return selected_;}
    float frequency() const noexcept {return selected_>=0?frequencies[selected_]:0.f;}
    // Identical filter/strength on both channels. Small attenuation cap
    // limits damage to foreground sharing a learned line. No output delay.
    void apply(float& l,float& r,float amount) noexcept {
        if(selected_<0)return;
        const double target=.292893218*std::clamp(double(amount),0.,1.);
        mix_+=.002*(target-mix_);
        l=filterSample(l,0);
        if(channels_==2)r=filterSample(r,1);
    }
private:
    struct Coeff {double b=0.,a1=0.,a2=0.;};
    struct State {double x1=0.,x2=0.,y1=0.,y2=0.;};
    float filterSample(float x,int c) noexcept {
        auto& s=states_[c];const auto& k=filter_[selected_];
        const double y=k.b*(double(x)-s.x2)-k.a1*s.y1-k.a2*s.y2;
        s.x2=s.x1;s.x1=x;s.y2=s.y1;s.y1=std::abs(y)<1e-25?0.:y;
        return float(double(x)-mix_*y);
    }
    void clearWindow() noexcept {frames_=0;energy_={};go_={};}
    int channels_=1,stride_=24,window_=400,frames_=0,phase_=0,windows_=0,selected_=-1;
    double mix_=0.;
    std::array<std::array<double,3>,count> coef_{};
    std::array<Coeff,count> filter_{};
    std::array<State,2> states_{};
    std::array<std::array<std::array<std::array<double,2>,3>,count>,2> go_{};
    std::array<double,2> energy_{},partial_{};
    std::array<int,count> votes_{};
    std::array<double,count> score_{};
};
} // namespace armix::builtin_fx::anti_noiz
