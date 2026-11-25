// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Lower-authority Voice residual mask. Never updates PSD, idle labels,
// tonal tracking, or decision-directed gains. All state is audio-owner fixed.
template<int MaxBins> class ResidualShape final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    void prepare(double rate,int hop) noexcept {
        close_=float(1.-std::exp(-double(hop)/(rate*.12)));reset();
    }
    void reset() noexcept {mask_.fill(1.f);}
    float gain(int bin) const noexcept {return mask_[bin];}
    // Match the current spectral shape against the immutable learned shape.
    // A common-mode level excursion is not by itself evidence of speech.
    // Foreground on either channel vetoes extra local attenuation.
    void update(const Powers& power,const Powers& anchor,int channels,int bins,
                const std::array<float,2>& variability,float silenceFloor,
                bool eligible) noexcept {
        if(!eligible||silenceFloor>=1.f){reset();return;}
        std::array<float,2> scale{},mean{},shape{};
        for(int c=0;c<channels;++c){
            float p=0.f,q=0.f;
            for(int k=0;k<bins;++k){p+=power[c][k];q+=anchor[c][k];}
            scale[c]=p/std::max(q,eps);mean[c]=q/float(bins);
            if(variability[c]<.8f||scale[c]<.35f||scale[c]>12.f){reset();return;}
            // L1 normalized spectral distribution distance, insensitive to
            // overall level. Aggregate 4-bin bands to avoid periodogram spikes.
            float distance=0.f;
            for(int k=0;k<bins;k+=4){
                float a=0.f,b=0.f;
                for(int j=k;j<std::min(k+4,bins);++j){a+=power[c][j];b+=anchor[c][j];}
                distance+=std::abs(a/std::max(p,eps)-b/std::max(q,eps));
            }
            shape[c]=std::clamp((.85f-distance)/.45f,0.f,1.f);
        }
        // No extra cut in quiet learned regions or around local foreground.
        // Linked confidence is min across channels. Nominal extra cap 6 dB.
        for(int k=0;k<bins;++k){
            float confidence=1.f;
            for(int c=0;c<channels;++c){
                float p=0.f,q=0.f;
                for(int j=std::max(0,k-1);j<=std::min(bins-1,k+1);++j){p+=power[c][j];q+=anchor[c][j];}
                const float relative=anchor[c][k]/std::max(mean[c],eps);
                const float dominant=std::clamp((relative-.5f)/1.5f,0.f,1.f);
                const float excess=p/std::max(q*std::max(1.f,scale[c]),eps);
                const float local=std::clamp((2.f-excess),0.f,1.f);
                confidence=std::min(confidence,shape[c]*dominant*local);
            }
            const float target=1.f-(1.f-std::max(silenceFloor,.50118723f))*confidence;
            auto& g=mask_[k];g=target>=g?target:g+close_*(target-g);
        }
    }
private:
    static constexpr float eps=1e-16f;
    std::array<float,MaxBins> mask_{};
    float close_=1.f;
};
} // namespace armix::builtin_fx::anti_noiz
