// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include "AntiNoizTonalDrift.h"
namespace armix::builtin_fx::anti_noiz {
// Past-only Learn search, fixed grid, up to three related linked lines. The fine grid is
// oversampled relative to the 200ms analysis resolution, not super-resolution.
// At runtime a level guard releases attenuation on strong coincident energy.
class TonalNoise final {
public:
    static constexpr int side=6, count=65, bank=count+2*side, maxLines=3;
    static constexpr float step=2.5f;
    static constexpr float hz(int i) noexcept {return 40.f+step*float(i-side);}
    void prepare(double rate,int channels) noexcept {
        channels_=channels;
        mixAlpha_=1.-std::exp(-1./(.01*rate));
        guardAlpha_=1.-std::exp(-1./(.035*rate));
        stride_=std::max(1,int(std::lround(rate/2000.)));
        const double reduced=rate/stride_;
        window_=std::max(1,int(std::lround(.2*reduced)));
        drift_.prepare(stride_,std::max(1,int(std::lround(.4*reduced))));
        fadeStep_=1./(.1*rate);
        for(int i=0;i<bank;++i)
            coef_[i]=2.*std::cos(2.*std::numbers::pi*hz(i)/reduced);
        for(int i=0;i<count;++i){
            const double f=hz(i+side),w=2.*std::numbers::pi*f/rate;
            const double alpha=std::sin(w)/(2.*(f/4.));
            auto& k=filter_[i];k.b=alpha/(1.+alpha);
            k.a1=-2.*std::cos(w)/(1.+alpha);k.a2=(1.-alpha)/(1.+alpha);
        }
        lineCount_=0;lines_={};begin();
    }
    void clear() noexcept {lineCount_=0;lines_={};begin();}
    void begin() noexcept {
        votes_={};harmonicVotes_={};score_={};primaryScore_={};candidatePower_={};primaryPower_={};windows_=0;phase_=0;partial_={};clearWindow();resetSignal();
    }
    void resetSignal() noexcept {
        for(auto& line:lines_){line.index=line.anchor;line.state={};line.livePower={};line.mix=0.;}
        drift_.reset();previous_={};fade_=1.;
    }
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
                qualified&=energy_[c]>1e-8&&fraction>=.12&&center>=8.*std::max(sides,1e-15);
                // Suppress side-lobe votes; tolerate neighboring grid ties
                // for a line between grid points without treating it as drift.
                qualified&=center>=.8*std::max(p[k-1],p[k+1]);
                strength=std::min(strength,fraction);
            }
            if(qualified){
                ++harmonicVotes_[i];score_[i]+=strength;
                const bool primary=strength>=.35;
                if(primary){++votes_[i];primaryScore_[i]+=strength;}
                for(int c=0;c<channels_;++c){
                    const double p=2.*power[c][k]/(double(window_)*window_);
                    candidatePower_[i][c]+=p;
                    if(primary)primaryPower_[i][c]+=p;
                }
            }
        }
        clearWindow();
    }
    // Accepted broadband Learn is the sole commit fence. Cancel/reject keeps
    // both trusted frequency and level. No coefficient design on audio thread.
    void commit() noexcept {
        lineCount_=0;lines_={};double best=0.;int primary=-1;
        if(windows_>=8)for(int i=0;i<count;++i)
            if(votes_[i]*10>=windows_*8&&primaryScore_[i]>best){primary=i;best=primaryScore_[i];}
        if(primary>=0){
            addLine(primary,true);
            // Only a persistent integer-related peak can supplement the
            // strong primary. No unrelated peaks or adjacent grid copies.
            while(lineCount_<maxLines){
                int next=-1;double strength=0.;
                for(int i=0;i<count;++i){
                    if(harmonicVotes_[i]*10<windows_*8||score_[i]<=strength||
                       !related(hz(primary+side),hz(i+side)))continue;
                    bool distinct=true;
                    for(int j=0;j<lineCount_;++j)
                        distinct&=std::abs(i-lines_[j].index)*step>=20.f;
                    if(distinct){next=i;strength=score_[i];}
                }
                if(next<0)break;
                addLine(next,false);
            }
        }
        resetSignal();
    }
    int selected() const noexcept {return lineCount_?lines_[0].index:-1;}
    int lineCount() const noexcept {return lineCount_;}
    float frequency(int line=0) const noexcept {
        return line>=0&&line<lineCount_?hz(lines_[line].index+side):0.f;
    }
    // Single-line only: multi-line coherence needs its own qualification.
    // Raw input is authorized by the preceding strict-idle WOLA frame.
    void track(float l,float r,bool strictIdle) noexcept {
        if(lineCount_!=1)return;
        auto& line=lines_[0];
        const int next=drift_.observe(l,r,channels_,strictIdle&&fade_>=1.,
            line.anchor,line.index,count,coef_.data()+side,line.inverse);
        if(next<0)return;
        previous_=line;line.index=next;line.state={};fade_=0.;
    }
    // Strong line excess may be a note rather than noise. Use the maximum
    // channel evidence for linked release; never reinterpret it as a pitch
    // classifier or adapt the trusted learned level during playback.
    void apply(float& l,float& r,float amount) noexcept {
        if(!lineCount_)return;
        // Parallel filters see identical original PCM; one guard release
        // must not reduce suppression of another independent learned line.
        const double scale=.292893218*std::clamp(double(amount),0.,1.);
        double cutL=0.,cutR=0.;
        for(int i=0;i<lineCount_;++i){
            auto& line=lines_[i];
            double y0=bandpass(l,line,0);
            double y1=channels_==2?bandpass(r,line,1):0.;
            if(i==0&&fade_<1.){
                const double oldL=bandpass(l,previous_,0),oldR=channels_==2?bandpass(r,previous_,1):0.;
                y0=oldL+(y0-oldL)*fade_;y1=oldR+(y1-oldR)*fade_;
                fade_=std::min(1.,fade_+fadeStep_);
            }
            const std::array<double,2> y{y0,y1};
            double excess=0.;
            for(int c=0;c<channels_;++c){
                auto& p=line.livePower[c];p+=guardAlpha_*(y[c]*y[c]-p);
                if(p<1e-25)p=0.;
                excess=std::max(excess,p*line.inverse[c]);
            }
            const double confidence=1.-std::clamp((excess-1.5)/2.5,0.,1.);
            const double target=scale*confidence;
            line.mix+=mixAlpha_*(target-line.mix);
            cutL+=line.mix*y0;cutR+=line.mix*y1;
        }
        l=float(double(l)-cutL);
        if(channels_==2)r=float(double(r)-cutR);
    }
private:
    struct Coeff {double b=0.,a1=0.,a2=0.;};
    struct State {double x1=0.,x2=0.,y1=0.,y2=0.;};
    struct Line {
        int index=-1,anchor=-1;
        double mix=0.;
        std::array<double,2> inverse{},livePower{};
        std::array<State,2> state{};
    };
    static bool related(float a,float b) noexcept {
        const float lo=std::min(a,b),hi=std::max(a,b);
        for(int m=2;m<=4;++m)if(std::abs(hi-float(m)*lo)<=2.5f)return true;
        return false;
    }
    void addLine(int i,bool primary) noexcept {
        auto& line=lines_[lineCount_++];line.index=line.anchor=i;
        const auto& powers=primary?primaryPower_[i]:candidatePower_[i];
        const int votes=primary?votes_[i]:harmonicVotes_[i];
        for(int c=0;c<channels_;++c)
            line.inverse[c]=1./std::max(1e-15,powers[c]/votes);
    }
    double bandpass(float x,Line& line,int c) noexcept {
        auto& s=line.state[c];const auto& k=filter_[line.index];
        const double y=k.b*(double(x)-s.x2)-k.a1*s.y1-k.a2*s.y2;
        s.x2=s.x1;s.x1=x;s.y2=s.y1;s.y1=std::abs(y)<1e-25?0.:y;
        return y;
    }
    void clearWindow() noexcept {frames_=0;energy_={};go_={};}
    int channels_=1,stride_=24,window_=400,frames_=0,phase_=0,windows_=0,lineCount_=0;
    double mixAlpha_=0.,guardAlpha_=0.;
    std::array<double,bank> coef_{};
    std::array<Coeff,count> filter_{};
    std::array<Line,maxLines> lines_{};
    Line previous_{};
    TonalDrift drift_{};
    double fade_=1.,fadeStep_=0.;
    std::array<std::array<std::array<double,2>,bank>,2> go_{};
    std::array<double,2> energy_{},partial_{};
    std::array<int,count> votes_{},harmonicVotes_{};
    std::array<double,count> score_{},primaryScore_{};
    std::array<std::array<double,2>,count> candidatePower_{},primaryPower_{};
};
} // namespace armix::builtin_fx::anti_noiz
