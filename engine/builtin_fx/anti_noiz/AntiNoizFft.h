// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <limits>
#if defined(__SSE2__) || defined(_M_X64)
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
        for(int i=0;i<n_;++i) {
            unsigned x=static_cast<unsigned>(i),rev=0;
            for(int v=n_;v>1;v>>=1){rev=(rev<<1)|(x&1U);x>>=1;}
            rev_[i]=static_cast<int>(rev);
        }
        for(int i=0;i<hop_;++i) {
            const float a=-2.f*std::numbers::pi_v<float>*float(i)/float(n_);
            twiddle_[i]={std::cos(a),std::sin(a)};
        }
#if defined(__SSE2__) || defined(_M_X64)
        // Contiguous per-stage twiddles allow two complex butterflies per SIMD vector.
        for(int len=4;len<=n_;len*=2){const int half=len/2,step=n_/len;for(int j=0;j<half;++j)stage_[half+j]=twiddle_[j*step];}
#endif
    }
    void transform(std::array<Complex,maxN>& a,bool inverse) noexcept {
        for(int i=0;i<n_;++i)if(i<rev_[i])std::swap(a[i],a[rev_[i]]);
        for(int len=2;len<=n_;len*=2) {
            const int half=len/2, step=n_/len;
#if defined(__SSE2__) || defined(_M_X64)
            if(half>=2){
                // std::complex<float> has a standard interleaved float layout.
                constexpr int sign=std::numeric_limits<int>::min();
                const __m128 conjugate=_mm_castsi128_ps(_mm_setr_epi32(0,sign,0,sign)), subtractReal=_mm_castsi128_ps(_mm_setr_epi32(sign,0,sign,0));
                const auto* stage=reinterpret_cast<const float*>(stage_.data()+half);
                for(int base=0;base<n_;base+=len)for(int j=0;j<half;j+=2){
                    auto* u=reinterpret_cast<float*>(a.data()+base+j);auto* x=u+2*half;
                    __m128 w=_mm_loadu_ps(stage+2*j);if(inverse)w=_mm_xor_ps(w,conjugate);
                    const __m128 U=_mm_loadu_ps(u),X=_mm_loadu_ps(x);
                    const __m128 wr=_mm_shuffle_ps(w,w,_MM_SHUFFLE(2,2,0,0)),wi=_mm_shuffle_ps(w,w,_MM_SHUFFLE(3,3,1,1));
                    const __m128 cross=_mm_mul_ps(_mm_shuffle_ps(X,X,_MM_SHUFFLE(2,3,0,1)),wi);
                    const __m128 V=_mm_add_ps(_mm_mul_ps(X,wr),_mm_xor_ps(cross,subtractReal));
                    _mm_storeu_ps(u,_mm_add_ps(U,V));_mm_storeu_ps(x,_mm_sub_ps(U,V));
                }
                continue;
            }
#endif
            for(int base=0;base<n_;base+=len)for(int j=0;j<half;++j) {
                const auto w=inverse?std::conj(twiddle_[j*step]):twiddle_[j*step];
                const auto u=a[base+j],v=a[base+j+half]*w;
                a[base+j]=u+v;a[base+j+half]=u-v;
            }
        }
        if(inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
    }
private:
    int n_=256;
    std::array<int,maxN> rev_{};
    std::array<Complex,maxN/2> twiddle_{};
#if defined(__SSE2__) || defined(_M_X64)
    std::array<Complex,maxN> stage_{};
#endif
};
} // namespace armix::builtin_fx::anti_noiz
