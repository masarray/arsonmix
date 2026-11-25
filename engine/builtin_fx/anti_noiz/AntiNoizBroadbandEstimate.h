// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Conservative common-mode noise scale for the active Voice estimator.
// Not a speech classifier; never writes trusted/working PSD or idle labels.
// Fixed eight-band lower order statistic rejects isolated spectral excess.
template<int MaxBins> class BroadbandEstimate final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    static constexpr int bands=8;
    void prepare(double rate,int hop) noexcept {
        rise_=float(1.-std::exp(-double(hop)/(rate*.080)));reset();
    }
    void reset() noexcept {scale_.fill(1.f);active_=false;}
    bool active() const noexcept {return active_;}
    float scale(int channel) const noexcept {return scale_[channel];}
    void update(const Powers& power,const Powers& reference,int channels,int bins,
                const std::array<float,2>& variability,bool eligible) noexcept {
        if(!eligible){reset();return;}
        active_=false;
        for(int c=0;c<channels;++c){
            if(variability[c]<.8f){scale_[c]=1.f;continue;}
            std::array<float,bands> p{},q{},ratios{};
            float total=0.f;
            for(int g=0;g<bands;++g){
                for(int k=g*bins/bands;k<(g+1)*bins/bands;++k){
                    p[g]+=power[c][k];q[g]+=reference[c][k];
                }
                total+=q[g];
            }
            int count=0;
            for(int g=0;g<bands;++g){
                if(q[g]<std::max(eps,.0001f*total))continue;
                // Insert into a tiny fixed order-statistic buffer.
                const float v=std::clamp(p[g]/q[g],0.f,8.f);
                int j=count++;
                while(j>0&&v<ratios[j-1]){ratios[j]=ratios[j-1];--j;}
                ratios[j]=v;
            }
            // Lower quartile of reliable broad bands, not the loudest band.
            // At least four measured regions; no unobserved-band inference.
            const float target=count>=4?std::clamp(ratios[(count-1)/4],1.f,1.6f):1.f;
            auto& s=scale_[c];
            s=target<=s?target:s+rise_*(target-s);
            active_|=s>1.f;
        }
        if(channels==1)scale_[1]=1.f;
    }
    static float corrected(float gamma,float relative,float scale) noexcept {
        // Above 4x power, preserve the original high-SNR onset response.
        // Avoid new attenuation in relatively quiet learned bands.
        const float dominant=std::clamp((relative-.5f),0.f,1.f);
        const float local=std::clamp((4.f-gamma)*.5f,0.f,1.f);
        return gamma/(1.f+(std::clamp(scale,1.f,1.6f)-1.f)*dominant*local);
    }
private:
    static constexpr float eps=1e-16f;
    std::array<float,2> scale_{1.f,1.f};
    float rise_=1.f;
    bool active_=false;
};
} // namespace armix::builtin_fx::anti_noiz
