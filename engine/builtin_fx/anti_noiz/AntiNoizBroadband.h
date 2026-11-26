// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Causal profile-shaped high-band estimator. No authority over the learned
// PSD or spectral policies outside Voice. Separate idle evidence may
// request bounded residual attenuation, never profile tracking.
template<int MaxBins> class BroadbandNoise final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    void prepare(double rate,int hop,int fft,int channels) noexcept {
        channels_=channels;bins_=fft/2+1;first_=std::clamp(int(std::ceil(3000.*fft/rate)),1,bins_-1);
        last_=std::clamp(int(std::ceil(12000.*fft/rate)),first_+1,bins_);
        alpha_=float(1.-std::exp(-double(hop)/(rate*.022)));
        hold_=std::max(1,int(std::ceil(.075*rate/hop)));
        valid_=false;reset();
    }
    void configure(const Powers& anchor) noexcept {
        reset();valid_=true;
        for(int c=0;c<channels_;++c){
            float all=0,high=0;for(int k=0;k<bins_;++k){all+=anchor[c][k];if(k>=first_&&k<last_)high+=anchor[c][k];}
            valid_&=all>eps&&high>.80f*all;
            total_[c]=all;
            for(int k=0,g=0;k<first_;++g){float q=0;const int end=std::min(k+4,first_);for(;k<end;++k)q+=anchor[c][k];lowRef_[c][g]=q;}
            float upper=0;for(int k=last_;k<bins_;++k)upper+=anchor[c][k];upperRef_[c]=upper;
            for(int k=first_;k<last_;++k){
                const float q=anchor[c][k];
                invBin_[c][k]=q>=.0001f*all&&q>eps?1.f/q:0.f;
            }
            for(int k=first_,g=0;k<last_;++g){
                float q=0;const int end=std::min(k+8,last_);
                for(;k<end;++k)q+=anchor[c][k];
                groupRef_[c][g]=q;
                invGroup_[c][g]=q>.015f*all?1.f/q:0.f;
            }
        }
    }
    void reset() noexcept {initialized_=false;quiet_=0;engaged_=false;}
    bool eligible() const noexcept {return valid_;}
    bool residualIdle() const noexcept {return engaged_;}
    void update(const Powers& p,const Powers& anchor,bool enabled,bool residualEnabled) noexcept {
        if(!enabled||!valid_){reset();return;}
        bool idle=residualEnabled,strong=false;
        for(int c=0;c<channels_;++c){
            std::array<float,(MaxBins+7)/8> groups{},instantGroups{};int n=0;
            for(int k=first_,g=0;k<last_;++g){
                float a=0,instant=0;const int end=std::min(k+8,last_);
                for(;k<end;++k){auto& avg=average_[c][k];avg=initialized_?avg+alpha_*(p[c][k]-avg):anchor[c][k];a+=avg;if(residualEnabled)instant+=p[c][k];}
                if(invGroup_[c][g]>0.f)groups[n++]=a*invGroup_[c][g];
                if(residualEnabled)instantGroups[g]=instant;
            }
            if(n>=4){
                std::sort(groups.begin(),groups.begin()+n);
                const float middle=groups[n/2],lower=groups[n/4],upper=groups[(3*n)/4];
                inverseScale_[c]=1.f/std::clamp(lower,1.f,12.f);
                const float shape=std::min(std::clamp((lower/std::max(middle,eps)-.65f)*5.f,0.f,1.f),
                    std::clamp((1.75f-upper/std::max(middle,eps))*2.f,0.f,1.f));
                confidence_[c]=shape*std::clamp((lower-.30f)*3.f,0.f,1.f);
                // Independent lower and upper frequency evidence, with no
                // authority to modify the trusted learned PSD.
                if(!residualEnabled)continue;
                const float level=std::clamp(middle,1.f,12.f);
                // Immediate novel high-band event veto, independent of the
                // smoothed estimate. Group power avoids single-bin jitter.
                for(int g=0,k=first_;k<last_;++g,k+=8)
                    if(instantGroups[g]>8.f*level*groupRef_[c][g]+(invGroup_[c][g]>0.f?0.f:.02f*total_[c]))strong=true;
                float low=0,lowRef=0,upperPower=0;
                for(int k=0,g=0;k<first_;++g){float q=0;const int end=std::min(k+4,first_);for(;k<end;++k)q+=p[c][k];const float ref=lowRef_[c][g];low+=q;lowRef+=ref;
                    if(q>8.f*level*ref+.005f*total_[c])strong=true;}
                for(int k=last_;k<bins_;++k)upperPower+=p[c][k];
                if(low>3.5f*level*lowRef||upperPower>5.f*level*upperRef_[c])strong=true;
                if(low>2.2f*level*lowRef||upperPower>3.f*level*upperRef_[c]||upper>2.5f*std::max(middle,eps)||middle>4.f||lower<.5f*middle)idle=false;

            }else {inverseScale_[c]=1.f;confidence_[c]=0.f;idle=false;}
        }
        initialized_=true;
        if(!residualEnabled){quiet_=0;engaged_=false;return;}
        quiet_=strong?0:idle?std::min(quiet_+1,hold_):std::max(quiet_-2,0);
        if(strong||quiet_<hold_/2)engaged_=false;
        else if(quiet_>=hold_)engaged_=true;
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
    std::array<std::array<float,(MaxBins+7)/8>,2> invGroup_{},groupRef_{};
    std::array<float,2> inverseScale_{},confidence_{},total_{},upperRef_{};
    std::array<std::array<float,(MaxBins+3)/4>,2> lowRef_{};
    int hold_=1,quiet_=0;
    int channels_=0,bins_=0,first_=1,last_=2;float alpha_=1;
    bool valid_=false,initialized_=false,engaged_=false;
};
}
