// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace armix::builtin_fx::anti_noiz {
// Qualify coherent level drift or broad, smooth spectral drift against the
// immutable Learn anchor. Never promotes a frame to idle or changes expander
// authority. Evidence is not source identity: scope remains Voice policy only.
template<int MaxBins> class NoiseDriftGuard final {
    static constexpr int maxGroups=(MaxBins+7)/8;
    using Bands=std::array<float,maxGroups>;
    struct ChannelEvidence {
        Bands bands{};
        float level=0;
        bool spectral=false;
    };
    using Evidence=std::array<ChannelEvidence,2>;
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    static constexpr float maxScale=2.2f;
    void prepare(double rate,int hop,int channels,int bins) noexcept {
        channels_=channels;bins_=bins;
        frames_=std::max(1,int(std::ceil(.25*rate/hop)));
        alpha_=float(1.-std::exp(-double(frames_)*hop/(rate*2.)));
        // Match ContextGuard's grouping, including the merged terminal group.
        groups_=0;
        for(int k=0;k<bins_;){
            const int end=std::min(k+8,bins_),stop=bins_-end<4?bins_:end;
            ends_[groups_]=stop;
            centers_[groups_]=.5f*float(k+stop-1);
            for(int j=k;j<stop;++j)groupOf_[j]=groups_;
            ++groups_;k=stop;
        }
        // Prepare interpolation once, so commit does not create hard PSD steps.
        int left=0;
        for(int k=0;k<bins_;++k){
            while(left+1<groups_&&float(k)>centers_[left+1])++left;
            const int right=std::min(left+1,groups_-1);
            left_[k]=left;right_[k]=right;
            mix_[k]=right==left?0.f:std::clamp((float(k)-centers_[left])/(centers_[right]-centers_[left]),0.f,1.f);
        }
        reset();
    }
    void reset() noexcept {clearWindow();consecutive_=0;}
    float alpha() const noexcept {return alpha_;}
    bool channelEligible(int channel) const noexcept {return previous_[channel].level>=1.45f;}
    // Only call after observe returns true. Interpolate the accepted broad
    // shape, not individual noisy FFT bins. Scalar path is unchanged.
    float targetScale(int channel,int bin) const noexcept {
        const auto& e=previous_[channel];
        if(!e.spectral)return std::min(e.level,maxScale);
        const float a=e.bands[left_[bin]],b=e.bands[right_[bin]];
        return std::clamp(a+(b-a)*mix_[bin],.7f,maxScale);
    }
    bool observe(const Powers& power,const Powers& anchor,float broadband,float subbandPeak) noexcept {
        if(!std::isfinite(broadband)||!std::isfinite(subbandPeak)||broadband>4.5f||
           subbandPeak>6.f*std::max(1.f,broadband)){reset();return false;}
        for(int c=0;c<channels_;++c)for(int k=0;k<bins_;++k)sum_[c][k]+=power[c][k];
        if(++count_<frames_)return false;
        Evidence evidence{};
        bool elevated=false,compatible=true;
        const float inv=1.f/float(count_);
        for(int c=0;c<channels_;++c){
            auto& e=evidence[c];
            float p=0,q=0;
            for(int k=0;k<bins_;++k){p+=sum_[c][k];q+=anchor[c][k];}
            if(q<=1e-12f){compatible=false;break;}
            e.level=p*inv/q;
            if(!std::isfinite(e.level)||e.level<.65f||e.level>3.f){compatible=false;break;}
            elevated|=e.level>=1.45f;
            bool scalarShape=true,spectralShape=true;
            float low=3.2f,high=0.f;
            int start=0,run=0,longest=0;
            for(int g=0;g<groups_;++g){
                float a=0,b=0;
                for(int k=start;k<ends_[g];++k){a+=sum_[c][k];b+=anchor[c][k];}
                const float expected=e.level*b;
                if(a*inv<.65f*expected-1e-16f||a*inv>1.5f*expected+1e-16f)scalarShape=false;
                // Do not extrapolate from unobserved/near-zero anchor bands.
                const float band=b>1e-16f?a*inv/b:0.f;
                e.bands[g]=band;
                if(!std::isfinite(band)||band<.6f||band>3.2f)spectralShape=false;
                if(g>0&&(band>1.65f*e.bands[g-1]||e.bands[g-1]>1.65f*band))spectralShape=false;
                low=std::min(low,band);high=std::max(high,band);
                run=band>=1.6f?run+1:0;longest=std::max(longest,run);
                start=ends_[g];
            }
            bool scalarPeaks=true,localPeaks=true;
            for(int k=0;k+1<bins_;++k){
                const float a=(sum_[c][k]+sum_[c][k+1])*inv;
                const float b=anchor[c][k]+anchor[c][k+1];
                if(a>2.2f*e.level*b+1e-16f)scalarPeaks=false;
                const float local=std::max(e.bands[groupOf_[k]],e.bands[groupOf_[k+1]]);
                if(a>2.2f*local*b+1e-16f)localPeaks=false;
            }
            // Fallback only for actual broad color change, not a narrow
            // peak or an isolated band. A neutral channel stays on old policy.
            if(!(scalarShape&&scalarPeaks)){
                e.spectral=spectralShape&&localPeaks&&longest>=3&&high>=1.55f*low&&e.level>=1.45f;
                if(!e.spectral)compatible=false;
            }
        }
        clearWindow();
        if(!compatible||!elevated){consecutive_=0;return false;}
        bool stable=consecutive_>0;
        for(int c=0;c<channels_;++c){
            const auto& a=evidence[c];const auto& b=previous_[c];
            stable&=a.level>=.8f*b.level&&a.level<=1.2f*b.level&&a.spectral==b.spectral;
            if(a.spectral)for(int g=0;g<groups_;++g)
                stable&=a.bands[g]>=.8f*b.bands[g]&&a.bands[g]<=1.2f*b.bands[g];
        }
        consecutive_=stable?std::min(consecutive_+1,4):1;
        previous_=evidence;
        return consecutive_>=4;
    }
private:
    void clearWindow() noexcept {if(count_>0)sum_={};count_=0;}
    Powers sum_{};
    Evidence previous_{};
    std::array<int,maxGroups> ends_{};
    std::array<float,maxGroups> centers_{};
    std::array<int,MaxBins> groupOf_{},left_{},right_{};
    std::array<float,MaxBins> mix_{};
    int channels_=0,bins_=0,groups_=0,frames_=1,count_=0,consecutive_=0;
    float alpha_=0;
};
} // namespace armix::builtin_fx::anti_noiz
