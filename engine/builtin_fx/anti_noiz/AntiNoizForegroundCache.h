// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
namespace armix::builtin_fx::anti_noiz {
// Frame scratch only: exact original relative noise and quiet-neighbor
// evidence, with at most one anchor division per channel per visited bin.
template<int MaxBins> class ForegroundCache final {
public:
    using Powers=std::array<std::array<float,MaxBins>,2>;
    template<class Context>
    void begin(const Context& ctx,const Powers& anchor,int channels,int bins,bool neighbors) noexcept {
        for(int k=0;k<bins;++k)relative_[k]=ctx.relativeNoise(anchor,channels,k);
        if(neighbors)std::fill_n(quietGamma_.begin(),bins,-1.f);
    }
    float relative(int k) const noexcept {return relative_[k];}
    float neighbor(const Powers& power,const Powers& anchor,int channels,int bins,int k) noexcept {
        float result=0.f;
        for(int j=std::max(0,k-3);j<=std::min(bins-1,k+3);++j){
            if(j==k||relative_[j]>=1.f)continue;
            auto& v=quietGamma_[j];
            if(v<0.f){
                v=0.f;
                for(int c=0;c<channels;++c)
                    v=std::max(v,power[c][j]/std::max(anchor[c][j],1e-16f));
            }
            result=std::max(result,v);
        }
        return result;
    }
private:
    std::array<float,MaxBins> relative_{},quietGamma_{};
};
} // namespace armix::builtin_fx::anti_noiz
