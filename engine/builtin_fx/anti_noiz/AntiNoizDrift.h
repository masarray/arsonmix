// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Bounded local search; cannot learn a new noise line. Only contiguous,
// externally confirmed idle samples are eligible. No coefficient synthesis.
class TonalDrift final {
public:
    static constexpr int probes=5,radius=2;
    void prepare(int stride,int window) noexcept {stride_=stride;window_=window;reset();}
    void reset() noexcept {clear();last_=-1;votes_=0;}
    int observe(float l,float r,int channels,bool eligible,int anchor,int current,
                int count,const double* coefficients,const std::array<double,2>& inverse) noexcept {
        if(!eligible){if(samples_||phase_||votes_)reset();return -1;}
        partial_[0]+=l;if(channels==2)partial_[1]+=r;
        if(++phase_<stride_)return -1;
        phase_=0;
        for(int c=0;c<channels;++c){
            const double x=partial_[c]/stride_;partial_[c]=0.;energy_[c]+=x*x;
            for(int j=0;j<probes;++j){
                const int i=anchor+j-radius;if(i<0||i>=count)continue;
                auto& s=state_[c][j];const double v=x+coefficients[i]*s[0]-s[1];s[1]=s[0];s[0]=v;
            }
        }
        if(++samples_<window_)return -1;
        int selected=-1;
        for(int c=0;c<channels;++c){
            std::array<double,probes> p{};int peak=-1;double best=0.;
            for(int j=0;j<probes;++j){
                const int i=anchor+j-radius;if(i<0||i>=count)continue;
                const auto& s=state_[c][j];
                p[j]=std::max(0.,s[0]*s[0]+s[1]*s[1]-coefficients[i]*s[0]*s[1]);
                if(p[j]>best){best=p[j];peak=j;}
            }
            const int now=current-anchor+radius;
            const double relative=2.*best/(double(window_)*window_)*inverse[c];
            const double fraction=2.*best/std::max(1e-18,double(window_)*energy_[c]);
            // Level and concentration veto: no following a new strong note,
            // no retune from a weak/ambiguous or multi-peaked window.
            double runnerUp=0.;for(int j=0;j<probes;++j)if(j!=peak)runnerUp=std::max(runnerUp,p[j]);
            if(peak<0||now<0||now>=probes||peak==now||relative<.5||relative>1.65||
               fraction<.65||best<1.8*std::max(runnerUp,1e-15)||
               best<2.*std::max(p[now],1e-15)||(c&&selected!=peak)){
                reset();return -1;
            }
            selected=peak;
        }
        clear();
        if(selected!=last_){last_=selected;votes_=1;}else ++votes_;
        if(votes_<3)return -1;
        const int next=anchor+selected-radius;reset();return next;
    }
private:
    void clear() noexcept {samples_=phase_=0;partial_={};energy_={};state_={};}
    int stride_=1,window_=800,samples_=0,phase_=0,last_=-1,votes_=0;
    std::array<double,2> partial_{},energy_{};
    std::array<std::array<std::array<double,2>,probes>,2> state_{};
};
} // namespace armix::builtin_fx::anti_noiz
