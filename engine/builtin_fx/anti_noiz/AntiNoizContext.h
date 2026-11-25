// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Conservative evidence guard, NOT a speech/music classifier. Audio-owner only.
// Existing per-frame PSD is reused; no additional transforms or lookahead.
template<int MaxBins> class ContextGuard final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    void prepare(double rate,int hop) noexcept {
        alpha_=float(1.-std::exp(-double(hop)/(rate*.045)));
        hold_=std::max(1,int(std::ceil(.18*rate/hop)));
        reset();
    }
    void reset() noexcept {quiet_=0;initialized_=false;broadband_=0;}
    float broadbandRatio() const noexcept {return broadband_;}
    float temporalRatio(const Powers& profile,int channels,int bins,int bin) const noexcept {
        float ratio=0;
        for(int c=0;c<channels;++c){
            float p=0,q=0;
            for(int k=std::max(0,bin-1);k<=std::min(bins-1,bin+1);++k){p+=average_[c][k];q+=profile[c][k];}
            ratio=std::max(ratio,p/std::max(q,eps));
        }
        return ratio;
    }
    bool update(const Powers& power,const Powers& profile,int channels,int bins,
                bool eligible) noexcept {
        if(!eligible){reset();return false;}
        bool foreground=false;broadband_=0;
        for(int c=0;c<channels;++c){
            float total=0,noise=0;
            for(int k=0;k<bins;++k){
                const float p=power[c][k];total+=p;noise+=profile[c][k];
                // Winsorize only the temporal evidence: loud music must not
                // leave a many-second detector memory after the signal ends.
                const float bounded=std::min(p,8.f*std::max(profile[c][k],eps));
                auto& a=average_[c][k];a=initialized_?a+alpha_*(bounded-a):profile[c][k];
            }
            const float ratio=total/std::max(noise,eps);
            broadband_=std::max(broadband_,ratio);
            foreground|=total>=1.65f*std::max(noise,eps);
            // Existing broadband onset guard. Merge short terminal group.
            for(int k=0;k<bins;){
                const int end=std::min(k+8,bins),stop=bins-end<4?bins:end;
                float p=0,q=0;for(int j=k;j<stop;++j){p+=power[c][j];q+=profile[c][j];}
                foreground|=p>=5.f*std::max(q,eps);k=stop;
            }
            // Narrow persistent excess can be buried in broadband noise. Pair
            // adjacent bins to reduce sensitivity to FFT leakage and random peaks.
            // Ignore isolated DC/Nyquist edge bins; broadband guard still applies.
            for(int k=1;k+1<bins-1;++k){
                const float p=average_[c][k]+average_[c][k+1];
                const float q=profile[c][k]+profile[c][k+1];
                foreground|=p>=2.5f*std::max(q,eps);
            }
        }
        initialized_=true;
        quiet_=foreground?0:std::min(quiet_+1,hold_);
        return quiet_>=hold_;
    }
private:
    static constexpr float eps=1e-16f;
    Powers average_{};
    int quiet_=0,hold_=1;
    float alpha_=1,broadband_=0;
    bool initialized_=false;
};
} // namespace armix::builtin_fx::anti_noiz
