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
        adaptiveHold_=std::max(1,int(std::ceil(.35*rate/hop)));
        reset();
    }
    void reset() noexcept {quiet_=adaptiveQuiet_=0;initialized_=false;broadband_=0;subbandPeak_=0;inverseMean_.fill(0);}
    // A separate, lower-authority residual expansion label. Never used
    // for profile tracking, Voice estimator reset, or spectral authority.
    bool adaptiveIdle() const noexcept {return adaptiveQuiet_>=adaptiveHold_;}
    float broadbandRatio() const noexcept {return broadband_;}
    float subbandPeakRatio() const noexcept {return subbandPeak_;}
    // Stereo union: a quiet band in either channel deserves protection.
    float relativeNoise(const Powers& profile,int channels,int bin) const noexcept {
        float relative=profile[0][bin]*inverseMean_[0];
        if(channels==2)relative=std::min(relative,profile[1][bin]*inverseMean_[1]);
        return relative;
    }
    float temporalRatio(const Powers& profile,int channels,int bins,int bin) const noexcept {
        float ratio=0;
        for(int c=0;c<channels;++c){
            float p=0,q=0;
            for(int k=std::max(0,bin-1);k<=std::min(bins-1,bin+1);++k){p+=average_[c][k];q+=profile[c][k];}
            ratio=std::max(ratio,p/std::max(q,eps));
        }
        return ratio;
    }
    // Boolean corroboration for foreground restoration. Compare linear
    // powers directly and stop on the first independent channel; no division.
    bool hasTemporalExcess(const Powers& profile,int channels,int bins,int bin) const noexcept {
        for(int c=0;c<channels;++c){
            float p=0,q=0;
            for(int k=std::max(0,bin-1);k<=std::min(bins-1,bin+1);++k){p+=average_[c][k];q+=profile[c][k];}
            if(p>2.f*std::max(q,eps))return true;
        }
        return false;
    }
    bool update(const Powers& power,const Powers& profile,int channels,int bins,
                bool eligible,const std::array<float,2>* variability=nullptr) noexcept {
        if(!eligible){reset();return false;}
        bool foreground=false,adaptiveForeground=false,anyVariable=false;broadband_=0;subbandPeak_=0;
        for(int c=0;c<channels;++c){
            float total=0,noise=0,temporalTotal=0;
            const float cv=variability?(*variability)[c]:0.f;
            const bool variable=cv>=.8f;
            anyVariable|=variable;
            for(int k=0;k<bins;++k){
                const float p=power[c][k];total+=p;noise+=profile[c][k];
                // Winsorize only the temporal evidence: loud music must not
                // leave a many-second detector memory after the signal ends.
                const float bounded=std::min(p,8.f*std::max(profile[c][k],eps));
                auto& a=average_[c][k];a=initialized_?a+alpha_*(bounded-a):profile[c][k];
                if(variable)temporalTotal+=a;
            }
            const float denominator=std::max(noise,eps);
            inverseMean_[c]=float(bins)/denominator;
            const float ratio=total/denominator;
            broadband_=std::max(broadband_,ratio);
            const bool broadForeground=total>=1.65f*denominator;
            foreground|=broadForeground;
            // Relax only the learned variable channel, within a hard bound.
            // Independent local guards below still veto new foreground.
            const float ceiling=variable?std::clamp(1.f+5.f*cv,1.65f,12.f):1.65f;
            adaptiveForeground|=ratio>=ceiling;
            const float temporalLevel=variable?temporalTotal/denominator:1.f;
            // Existing broadband onset guard. Merge short terminal group.
            for(int k=0;k<bins;){
                const int end=std::min(k+8,bins),stop=bins-end<4?bins:end;
                float p=0,q=0;for(int j=k;j<stop;++j){p+=power[c][j];q+=profile[c][j];}
                const float bandDenominator=std::max(q,eps);
                foreground|=p>=5.f*bandDenominator;
                // Only a dominant learned group may follow common-mode
                // excursions. Quiet bands keep the original onset guard.
                const float bandLimit=variable&&q>=.1f*denominator?
                    std::max(5.f,2.5f*ratio):5.f;
                adaptiveForeground|=p>=bandLimit*bandDenominator;
                subbandPeak_=std::max(subbandPeak_,p/bandDenominator);k=stop;
            }
            // Narrow persistent excess can be buried in broadband noise. Pair
            // adjacent bins to reduce sensitivity to FFT leakage and random peaks.
            // Ignore isolated DC/Nyquist edge bins; broadband guard still applies.
            for(int k=1;k+1<bins-1;++k){
                const float p=average_[c][k]+average_[c][k+1];
                const float q=profile[c][k]+profile[c][k+1];
                foreground|=p>=2.5f*std::max(q,eps);
                const float pairLimit=variable&&q>=.05f*denominator?
                    std::max(2.5f,2.5f*temporalLevel):2.5f;
                adaptiveForeground|=p>=pairLimit*std::max(q,eps);
            }
        }
        initialized_=true;
        quiet_=foreground?0:std::min(quiet_+1,hold_);
        adaptiveQuiet_=(!variability||!anyVariable||adaptiveForeground)?0:
            std::min(adaptiveQuiet_+1,adaptiveHold_);
        return quiet_>=hold_;
    }
private:
    static constexpr float eps=1e-16f;
    Powers average_{};
    std::array<float,2> inverseMean_{};
    int quiet_=0,hold_=1,adaptiveQuiet_=0,adaptiveHold_=1;
    float alpha_=1,broadband_=0,subbandPeak_=0;
    bool initialized_=false;
};
} // namespace armix::builtin_fx::anti_noiz
