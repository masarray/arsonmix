// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

namespace armix::builtin_fx::anti_noiz {
// Fixed-capacity transform plan; only prepare computes twiddles.
class FftPlan {
public:
    static constexpr int maxN=512;
    using Complex=std::complex<float>;
    void prepare(int n) noexcept {
        n_=n;
        const int hop_=n_/2;
        for(int i=0;i<n_;++i) {
            unsigned x=static_cast<unsigned>(i),rev=0;
            for(int v=n_;v>1;v>>=1){rev=(rev<<1)|(x&1U);x>>=1;}
            rev_[i]=static_cast<int>(rev);
        }
        for(int i=0;i<hop_;++i) {
            const float a=-2.f*std::numbers::pi_v<float>*float(i)/float(n_);
            twiddle_[i]={std::cos(a),std::sin(a)};
        }
        // Stage-contiguous twiddles and conjugates are prepared once, rather
        // than recomputing index products and conjugation in every butterfly.
        for(int len=2;len<=n_;len*=2){const int half=len/2,step=n_/len;
            for(int j=0;j<half;++j){forward_[half+j]=twiddle_[j*step];inverse_[half+j]=std::conj(twiddle_[j*step]);}}
    }
    void transform(std::array<Complex,maxN>& a,bool inverse) noexcept {
        for(int i=0;i<n_;++i)if(i<rev_[i])std::swap(a[i],a[rev_[i]]);
        const auto& stages=inverse?inverse_:forward_;
        for(int len=2;len<=n_;len*=2) {
            const int half=len/2;
            const auto* w=stages.data()+half;
            for(int base=0;base<n_;base+=len)for(int j=0;j<half;++j) {
                const auto u=a[base+j],v=a[base+j+half]*w[j];
                a[base+j]=u+v;a[base+j+half]=u-v;
            }
        }
        if(inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
    }
private:
    int n_=256;
    std::array<int,maxN> rev_{};
    std::array<Complex,maxN/2> twiddle_{};
    std::array<Complex,maxN> forward_{},inverse_{};
};
} // namespace armix::builtin_fx::anti_noiz
