// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Causal profile-shaped high-band estimator. No authority over the learned
// PSD, residual gate, low fundamentals, or explicit musical policies.
template<int MaxBins> class BroadbandNoise final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    void prepare(double rate,int hop,int fft,int channels) noexcept {
        channels_=channels;bins_=fft/2+1;first_=std::clamp(int(std::ceil(3000.*fft/rate)),1,bins_-1);
        last_=std::clamp(int(std::ceil(12000.*fft/rate)),first_+1,bins_);
        alpha_=float(1.-std::exp(-double(hop)/(rate*.022)));
        valid_=false;reset();
    }
    void configure(const Powers& anchor) noexcept {
        reset();valid_=true;
        for(int c=0;c<channels_;++c){
            float all=0,high=0;for(int k=0;k<bins_;++k){all+=anchor[c][k];if(k>=first_&&k<last_)high+=anchor[c][k];}
            valid_&=all>eps&&high>.80f*all;
            for(int k=first_;k<last_;++k){
                const float q=anchor[c][k];
                invBin_[c][k]=q>=.0001f*all&&q>eps?1.f/q:0.f;
            }
            for(int k=first_,g=0;k<last_;++g){
                float q=0;const int end=std::min(k+8,last_);
                for(;k<end;++k)q+=anchor[c][k];
                invGroup_[c][g]=q>.015f*all?1.f/q:0.f;
            }
        }
    }
    void reset() noexcept {initialized_=false;}
    bool eligible() const noexcept {return valid_;}
    void update(const Powers& p,const Powers& anchor,bool enabled) noexcept {
        if(!enabled){reset();return;}
        for(int c=0;c<channels_;++c){
            std::array<float,(MaxBins+7)/8> groups{};int n=0;
            for(int k=first_,g=0;k<last_;++g){
                float a=0;const int end=std::min(k+8,last_);
                for(;k<end;++k){auto& avg=average_[c][k];avg=initialized_?avg+alpha_*(p[c][k]-avg):anchor[c][k];a+=avg;}
                if(invGroup_[c][g]>0.f)groups[n++]=a*invGroup_[c][g];
            }
            if(n>=4){
                std::sort(groups.begin(),groups.begin()+n);
                const float middle=groups[n/2],lower=groups[n/4],upper=groups[(3*n)/4];
                inverseScale_[c]=1.f/std::clamp(lower,1.f,12.f);
                const float shape=std::min(std::clamp((lower/std::max(middle,eps)-.65f)*5.f,0.f,1.f),
                    std::clamp((1.75f-upper/std::max(middle,eps))*2.f,0.f,1.f));
                confidence_[c]=shape*std::clamp((lower-.30f)*3.f,0.f,1.f);
            }else {inverseScale_[c]=1.f;confidence_[c]=0.f;}
        }
        initialized_=true;
    }
    float cap(int k,const Powers& p,float floor) const noexcept {
        if(!initialized_||k<first_||k>=last_)return 1.f;
        float g=0;
        for(int c=0;c<channels_;++c){
            if(invBin_[c][k]==0.f||confidence_[c]==0.f)return 1.f;
            const float inv=invBin_[c][k]*inverseScale_[c];
            const float gamma=average_[c][k]*inv;
            const float instant=p[c][k]*inv;
            // Strong local excess restores immediately; statistical floor
            // avoids opening every random periodogram peak.
            const float estimate=std::clamp(1.f-1.25f/std::max(gamma,1.f),0.f,1.f);
            const float onset=std::clamp((instant-5.f)/8.f,0.f,1.f);
            g=std::max(g,1.f-confidence_[c]*(1.f-std::max(estimate,onset)));
        }
        return std::max(floor,.20f+.80f*g);
    }
private:
    static constexpr float eps=1e-16f;
    Powers average_{},invBin_{};
    std::array<std::array<float,(MaxBins+7)/8>,2> invGroup_{};
    std::array<float,2> inverseScale_{},confidence_{};
    int channels_=0,bins_=0,first_=1,last_=2;float alpha_=1;
    bool valid_=false,initialized_=false;
};
}
