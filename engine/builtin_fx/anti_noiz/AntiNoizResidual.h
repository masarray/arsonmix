// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Residual-only, learned rumble evidence. Not a voice/wind classifier.
// No authority over the trusted PSD, spectral estimator, or tonal DSP.
// Stereo union: independent foreground in either leg opens immediately.
template<int MaxBins> class ResidualNoiseGuard final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    void prepare(double rate,int hop,int fft) noexcept {
        lowEnd_=std::clamp(int(400.*fft/rate)+1,2,MaxBins-1);
        hold_=std::max(1,int(std::ceil(.060*rate/hop)));
        reset();
    }
    void reset() noexcept {quiet_=0;engaged_=false;}
    bool update(const Powers& power,const Powers& anchor,int channels,int bins,bool eligible) noexcept {
        if(!eligible||bins<=lowEnd_){reset();return false;}
        bool rumble=false,compatible=true;
        for(int c=0;c<channels;++c){
            float low=0,high=0,refLow=0,refHigh=0;
            for(int k=0;k<lowEnd_;++k){low+=power[c][k];refLow+=anchor[c][k];}
            // Scale broad random variation by the learned dominant band,
            // not by the full-band energy of a new foreground source.
            const float scale=std::clamp(low/std::max(refLow,eps),1.f,25.f);
            float peakExcess=0;
            for(int k=lowEnd_;k<bins;){
                float p=0,q=0;const int stop=std::min(k+4,bins);
                for(;k<stop;++k){p+=power[c][k];q+=anchor[c][k];}
                high+=p;refHigh+=q;
                peakExcess=std::max(peakExcess,p-8.f*scale*q);
            }
            const float ref=refLow+refHigh,total=low+high;
            // Strong independent evidence opens in this hop. Tiny random
            // powers cannot repeatedly clear a long hold in quiet bands.
            if(peakExcess>.01f*std::max(ref,eps)||high>std::max(3.f*scale*refHigh,.15f*total)){
                reset();return false;
            }
            if(ref<=eps){if(total>eps){reset();return false;}continue;}
            if(total<=1.2f*ref){
                rumble|=refLow>=.70f*ref&&low>=.65f*total;
                continue;
            }
            if(refLow<.70f*ref||low<.80f*total||low>25.f*std::max(refLow,eps)){
                compatible=false;continue;
            }
            rumble=true;
        }
        // A single uncertain frame is not a foreground onset. Bounded
        // evidence hysteresis prevents random gust fluctuations pumping.
        if(compatible&&rumble)quiet_=std::min(quiet_+1,hold_);
        else quiet_=std::max(0,quiet_-2);
        if(quiet_>=hold_)engaged_=true;
        if(quiet_==0)engaged_=false;
        return engaged_;
    }
private:
    static constexpr float eps=1e-16f;
    int lowEnd_=3,hold_=1,quiet_=0;
    bool engaged_=false;
};
}
