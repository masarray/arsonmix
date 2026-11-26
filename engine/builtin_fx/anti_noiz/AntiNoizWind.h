// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// A narrowly scoped correction for learned near-DC rumble under clearly
// separated upper-band foreground. No broadband gating or PSD adaptation.
// No claim to distinguish wind from a weak overlapping low-register voice.
template<int MaxBins> class WindSpectralGuard final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    void prepare(double rate,int hop,int fft,int channels) noexcept {
        channels_=channels;bins_=fft/2+1;
        lowEnd_=std::clamp(int(400.*fft/rate)+1,2,bins_-1);
        close_=float(1.-std::exp(-double(hop)/(rate*.018)));
        valid_=false;reset();
    }
    void configure(const Powers& anchor) noexcept {
        reset();valid_=true;
        for(int c=0;c<channels_;++c){
            float total=0,low=0;
            for(int k=0;k<bins_;++k){total+=anchor[c][k];if(k<lowEnd_)low+=anchor[c][k];}
            total_[c]=total;
            // This is a spectral eligibility condition, not a wind label.
            valid_&=total>eps&&low>=.80f*total&&anchor[c][0]>=.45f*total;
            int groups=0;
            for(int k=lowEnd_;k<bins_;){
                float q=0;const int stop=std::min(k+8,bins_);
                for(;k<stop;++k)q+=anchor[c][k];
                refGroup_[c][groups++]=q>eps?1.f/q:0.f;
            }
        }
    }
    void reset() noexcept {cut_=1.f;}
    bool eligible() const noexcept {return valid_;}
    void update(const Powers& p,const Powers& anchor,bool enabled) noexcept {
        if(!enabled||!valid_){reset();return;}
        bool possible=false;
        for(int c=0;c<channels_;++c)possible|=p[c][0]>.10f*anchor[c][0]&&
            p[c][2]>8.f*anchor[c][2]&&p[c][2]>.10f*total_[c];
        if(!possible){reset();return;}
        bool upper=false,lower=false;
        for(int c=0;c<channels_;++c){
            std::array<float,maxGroups> ratios{};int g=0,used=0;
            for(int k=lowEnd_;k<bins_;){
                float v=0;const int stop=std::min(k+8,bins_);
                for(;k<stop;++k)v+=p[c][k];
                const float inv=refGroup_[c][g++];
                if(inv==0.f)continue;
                ratios[used++]=std::clamp(v*inv,.25f,16.f);
            }
            if(used<3){lower=true;continue;}
            // The selection range is statically bounded by maxGroups (33).
            std::nth_element(ratios.begin(),ratios.begin()+used/2,ratios.begin()+used);
            const float observedScale=ratios[used/2];
            const float scale=std::clamp(observedScale,1.f,8.f);
            const float low=std::max(0.f,p[c][1]-scale*anchor[c][1]);
            // Require an independent higher harmonic / formant region.
            // Only the existing DC bin gains extra attenuation. Never cap
            // any positive-frequency bin, which may contain the fundamental.
            const float middle=std::max(0.f,p[c][2]-scale*anchor[c][2]);
            float high=0;
            for(int k=4;k<std::min(bins_,13);++k)high=std::max(high,p[c][k]-scale*anchor[c][k]);
            upper|=observedScale>=.50f&&p[c][0]>.10f*anchor[c][0]&&
                middle>8.f*scale*anchor[c][2]&&middle>.10f*total_[c]&&
                low<.45f*middle&&high>.04f*total_[c];
            lower|=low>4.f*scale*anchor[c][1]&&low>.45f*middle;
        }
        // Linked union protects low foreground on either leg. Asymmetric
        // gain motion, immediate opening; no extra output delay.
        const float target=upper&&!lower?0.f:1.f;
        cut_=target>=cut_?target:cut_+close_*(target-cut_);
    }
    float cap(int bin,float floor) const noexcept {
        return valid_&&bin==0?floor+(1.f-floor)*cut_:1.f;
    }
private:
    static constexpr float eps=1e-16f;
    static constexpr int maxGroups=(MaxBins+7)/8;
    std::array<std::array<float,maxGroups>,2> refGroup_{};
    std::array<float,2> total_{};
    int channels_=0,bins_=0,lowEnd_=3;
    float close_=1,cut_=1;
    bool valid_=false;
};
}
