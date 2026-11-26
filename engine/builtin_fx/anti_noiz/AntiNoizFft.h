// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif

namespace armix::builtin_fx::anti_noiz {
// Fixed-capacity transform plan; only prepare computes twiddles.
class FftPlan {
public:
    static constexpr int maxN=512;
    using Complex=std::complex<float>;
    void prepare(int n) noexcept {
        n_=n;
        const int hop_=n_/2;
        swaps_=0;
        for(int i=0;i<n_;++i) {
            unsigned x=static_cast<unsigned>(i),rev=0;
            for(int v=n_;v>1;v>>=1){rev=(rev<<1)|(x&1U);x>>=1;}
            if(i<static_cast<int>(rev)){swapA_[swaps_]=i;swapB_[swaps_++]=static_cast<int>(rev);}
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
        for(int i=0;i<swaps_;++i)std::swap(a[swapA_[i]],a[swapB_[i]]);
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
    // Opt-in packed real-stereo WOLA only. Core sanitizes and bounds input
    // samples to +/-64, N <= 512, and the finite shared mask is <= 1.
    // Thus no complex NaN/overflow recovery is needed in these butterflies.
    // The scalar legacy path above and non-SSE2 targets retain their math.
    void transformPackedFinite(std::array<Complex,maxN>& a,bool inverse) noexcept {
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        for(int i=0;i<swaps_;++i)std::swap(a[swapA_[i]],a[swapB_[i]]);
        const auto& stages=inverse?inverse_:forward_;
        for(int base=0;base<n_;base+=2){
            const auto u=a[base],v=a[base+1]*stages[1];
            a[base]=u+v;a[base+1]=u-v;
        }
        const auto sign=_mm_setr_ps(-0.f,0.f,-0.f,0.f);
        for(int len=4;len<=n_;len*=2){
            const int half=len/2;
            const auto* w=stages.data()+half;
            for(int base=0;base<n_;base+=len)for(int j=0;j<half;j+=2){
                // C++ specifies array-oriented access to complex<float>
                // as interleaved real/imaginary floats. No alignment promise.
                auto* lo=reinterpret_cast<float*>(a.data()+base+j);
                auto* hi=reinterpret_cast<float*>(a.data()+base+j+half);
                const auto u=_mm_loadu_ps(lo),b=_mm_loadu_ps(hi);
                const auto t=_mm_loadu_ps(reinterpret_cast<const float*>(w+j));
                const auto re=_mm_shuffle_ps(t,t,_MM_SHUFFLE(2,2,0,0));
                const auto im=_mm_shuffle_ps(t,t,_MM_SHUFFLE(3,3,1,1));
                const auto swap=_mm_shuffle_ps(b,b,_MM_SHUFFLE(2,3,0,1));
                const auto v=_mm_add_ps(_mm_mul_ps(b,re),_mm_xor_ps(_mm_mul_ps(swap,im),sign));
                _mm_storeu_ps(lo,_mm_add_ps(u,v));
                _mm_storeu_ps(hi,_mm_sub_ps(u,v));
            }
        }
        if(inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
#else
        transform(a,inverse);
#endif
    }
private:
    int n_=256;
    int swaps_=0;
    std::array<int,maxN/2> swapA_{},swapB_{};
    std::array<Complex,maxN/2> twiddle_{};
    std::array<Complex,maxN> forward_{},inverse_{};
};
} // namespace armix::builtin_fx::anti_noiz
