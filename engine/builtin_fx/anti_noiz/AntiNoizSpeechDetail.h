// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
namespace armix::builtin_fx::anti_noiz {
// Local speech-detail evidence, not a classifier. Separate from the recursive
// estimator, trusted profile and idle labels. Only learned variable noise.
template<int MaxBins> class SpeechDetail final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    void analyze(const Powers& power,const Powers& anchor,int channels,int bins,
                 const std::array<float,2>& variability) noexcept {
        eligible_=false;
        for(int c=0;c<channels;++c)eligible_|=variability[c]>=.8f;
        if(!eligible_)return;
        for(int c=0;c<channels;++c){
            float total=0.f,noise=0.f;
            for(int k=0;k<bins;++k){total+=power[c][k];noise+=anchor[c][k];}
            const float inv=1.f/std::max(noise,eps);
            const float common=std::max(1.f,total*inv);
            const float meanInv=float(bins)*inv;
            for(int k=0;k<bins;++k){
                const float relative=anchor[c][k]*meanInv;
                const float excess=power[c][k]/std::max(anchor[c][k]*common,eps);
                // Corroboration only from quieter learned regions. Normalize
                // by common-mode level to avoid amplifying a louder fan.
                const float quiet=std::clamp(1.f-relative,0.f,1.f);
                support_[c][k]=variability[c]>=.8f?
                    quiet*std::clamp((excess-2.f)*.25f,0.f,1.f):0.f;
            }
        }
    }
    bool eligible() const noexcept {return eligible_;}
    static float restore(float gain,float gamma,float temporal,float relative,float support,float cleanFloor) noexcept {
        const float local=std::clamp((gamma-1.f)*.5f,0.f,1.f);
        const float sustain=std::clamp((temporal-1.f),0.f,1.f);
        const float overlap=std::clamp(relative*.5f,0.f,1.f);
        const float available=std::min(.12f*(1.f-cleanFloor),1.f-gain);
        return gain+available*local*sustain*overlap*std::clamp(support,0.f,1.f);
    }
    float support(int channels,int bins,int k) const noexcept {
        if(!eligible_)return 0.f;
        float linked=0.f;
        for(int c=0;c<channels;++c){
            // Two independent neighboring bins within one channel;
            // do not combine isolated peaks from different stereo legs.
            float first=0.f,second=0.f;
            for(int j=std::max(0,k-4);j<=std::min(bins-1,k+4);++j){
                if(j==k)continue;
                const float s=support_[c][j];
                if(s>first){second=first;first=s;}else second=std::max(second,s);
            }
            linked=std::max(linked,second);
        }
        return linked;
    }
private:
    static constexpr float eps=1e-16f;
    std::array<std::array<float,MaxBins>,2> support_{};
    bool eligible_=false;
};
} // namespace armix::builtin_fx::anti_noiz
