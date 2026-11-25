// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Independently qualify coherent upward noise-level drift against the immutable
// Learn anchor. Never changes foreground/idle classification or expander state.
// Evidence is not source identity: limit authority to known Voice policy.
template<int MaxBins> class NoiseDriftGuard final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    using Scales=std::array<float,2>;
    static constexpr float maxScale=2.2f;
    void prepare(double rate,int hop,int channels,int bins) noexcept {
        channels_=channels;bins_=bins;
        frames_=std::max(1,int(std::ceil(.25*rate/hop)));
        alpha_=float(1.-std::exp(-double(frames_)*hop/(rate*2.)));
        reset();
    }
    void reset() noexcept {
        clearWindow();consecutive_=0;previous_={};
    }
    float alpha() const noexcept {return alpha_;}
    // Caller supplies already computed max-channel broadband ratio.
    // Strong foreground cancels the window immediately. Moderate contamination
    // is checked by the independent window shape and cross-window consistency.
    bool observe(const Powers& power,const Powers& anchor,float broadband,
                 Scales& accepted) noexcept {
        if(!std::isfinite(broadband)||broadband>4.5f){reset();return false;}
        for(int c=0;c<channels_;++c)for(int k=0;k<bins_;++k)
            sum_[c][k]+=power[c][k];
        if(++count_<frames_)return false;
        Scales scale{};
        bool elevated=false,compatible=true;
        const float inv=1.f/float(count_);
        for(int c=0;c<channels_;++c){
            float p=0,q=0;
            for(int k=0;k<bins_;++k){p+=sum_[c][k];q+=anchor[c][k];}
            if(q<=1e-12f){compatible=false;break;}
            const float ratio=p*inv/q;
            scale[c]=ratio;
            if(!std::isfinite(ratio)||ratio<.65f||ratio>3.f){compatible=false;break;}
            elevated|=ratio>=1.45f;
            // Compare disjoint groups in normalized spectral space. Merge
            // short terminal group just like ContextGuard.
            for(int k=0;k<bins_;){
                const int end=std::min(k+8,bins_),stop=bins_-end<4?bins_:end;
                float a=0,b=0;
                for(int j=k;j<stop;++j){a+=sum_[c][j];b+=anchor[c][j];}
                const float expected=ratio*b;
                if(a*inv<.65f*expected-1e-16f||a*inv>1.5f*expected+1e-16f)
                    compatible=false;
                k=stop;
            }
            // Narrow coherent peaks can hide inside an otherwise valid group.
            for(int k=0;k+1<bins_;++k){
                const float a=(sum_[c][k]+sum_[c][k+1])*inv;
                const float b=ratio*(anchor[c][k]+anchor[c][k+1]);
                if(a>2.2f*b+1e-16f)compatible=false;
            }
        }
        clearWindow();
        if(!compatible||!elevated){consecutive_=0;return false;}
        bool stable=consecutive_>0;
        for(int c=0;c<channels_;++c)
            stable&=scale[c]>=.8f*previous_[c]&&scale[c]<=1.2f*previous_[c];
        consecutive_=stable?std::min(consecutive_+1,4):1;
        previous_=scale;
        if(consecutive_<4)return false;
        for(int c=0;c<channels_;++c)
            accepted[c]=scale[c]>=1.45f?std::min(scale[c],maxScale):0.f;
        return true;
    }
private:
    void clearWindow() noexcept {
        if(count_>0)sum_={};
        count_=0;
    }
    Powers sum_{};
    Scales previous_{};
    int channels_=0,bins_=0,frames_=1,count_=0,consecutive_=0;
    float alpha_=0;
};
} // namespace armix::builtin_fx::anti_noiz
