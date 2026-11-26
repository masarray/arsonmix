// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define ARSONMIX_ANTI_NOIZ_PACKED_SSE2 1
#endif

namespace armix::builtin_fx::anti_noiz {
// Fixed-capacity transform plan; only prepare computes twiddles.
class FftPlan {
public:
    static constexpr int maxN=512;
    using Complex=std::complex<float>;
    void prepare(int n) noexcept {
        n_=n;
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
        swaps_=0;
#endif
        const int hop_=n_/2;
        for(int i=0;i<n_;++i) {
            unsigned x=static_cast<unsigned>(i),rev=0;
            for(int v=n_;v>1;v>>=1){rev=(rev<<1)|(x&1U);x>>=1;}
            rev_[i]=static_cast<int>(rev);
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
            if(i<rev_[i])swapPairs_[swaps_++]={i,rev_[i]};
#endif
        }
        for(int i=0;i<hop_;++i) {
            const float a=-2.f*std::numbers::pi_v<float>*float(i)/float(n_);
            twiddle_[i]={std::cos(a),std::sin(a)};
        }
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
        for(int half=1;half<n_;half*=2)for(int j=0;j<half;++j){
            const auto w=twiddle_[j*(n_/(2*half))];
            stage_[half+j]=w;stageInv_[half+j]=std::conj(w);
        }
#endif
    }
    void transform(std::array<Complex,maxN>& a,bool inverse) noexcept {
        if(inverse)run<true>(a);else run<false>(a);
    }
    // Packed kernel stays public; scalar specialization is below.
    // Only the packed manual-stereo path uses this finite-input kernel.
    // Input PCM is sanitized and bounded to +/-64, with bounded real masks;
    // no fast-math, reciprocal approximation, new buffering or CPU dispatch.
    // Legacy automatic/mono arithmetic remains on transform().
    template<bool Inverse> void transformPacked(std::array<Complex,maxN>& a) noexcept {
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
        for(int i=0;i<swaps_;++i)std::swap(a[swapPairs_[i][0]],a[swapPairs_[i][1]]);
        const auto& stage=Inverse?stageInv_:stage_;
        for(int base=0;base<n_;base+=2){
            const auto u=a[base],x=a[base+1];
            const float wr=stage[1].real(),wi=stage[1].imag();
            const float vr=x.real()*wr-x.imag()*wi,vi=x.real()*wi+x.imag()*wr;
            a[base]={u.real()+vr,u.imag()+vi};a[base+1]={u.real()-vr,u.imag()-vi};
        }
        const auto signs=_mm_setr_ps(-0.f,0.f,-0.f,0.f);
        // std::complex<float> guarantees array-oriented real/imag access.
        // Each vector is two independent butterflies; all indices < n_.
        for(int half=2;half<n_;half*=2)for(int base=0;base<n_;base+=half*2)
            for(int j=0;j<half;j+=2){
                auto* lo=reinterpret_cast<float*>(&a[base+j]);
                auto* hi=reinterpret_cast<float*>(&a[base+j+half]);
                const auto u=_mm_loadu_ps(lo),x=_mm_loadu_ps(hi);
                const auto w=_mm_loadu_ps(reinterpret_cast<const float*>(&stage[half+j]));
                const auto wr=_mm_shuffle_ps(w,w,_MM_SHUFFLE(2,2,0,0));
                const auto wi=_mm_shuffle_ps(w,w,_MM_SHUFFLE(3,3,1,1));
                const auto xs=_mm_shuffle_ps(x,x,_MM_SHUFFLE(2,3,0,1));
                const auto v=_mm_add_ps(_mm_mul_ps(x,wr),_mm_xor_ps(_mm_mul_ps(xs,wi),signs));
                _mm_storeu_ps(lo,_mm_add_ps(u,v));_mm_storeu_ps(hi,_mm_sub_ps(u,v));
            }
        if constexpr(Inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
#else
        transform(a,Inverse);
#endif
    }
private:
    template<bool inverse> void run(std::array<Complex,maxN>& a) noexcept {
        for(int i=0;i<n_;++i)if(i<rev_[i])std::swap(a[i],a[rev_[i]]);
        for(int len=2;len<=n_;len*=2) {
            const int half=len/2, step=n_/len;
            for(int base=0;base<n_;base+=len)for(int j=0;j<half;++j) {
                const auto w=inverse?std::conj(twiddle_[j*step]):twiddle_[j*step];
                const auto u=a[base+j],x=a[base+j+half];
                // Core sanitizes and bounds PCM; a 512-point FFT cannot
                // overflow. Avoid generic complex NaN/Inf recovery branches.
                const Complex v{x.real()*w.real()-x.imag()*w.imag(),
                                x.real()*w.imag()+x.imag()*w.real()};
                a[base+j]=u+v;a[base+j+half]=u-v;
            }
        }
        if(inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
    }
    int n_=256;
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
    int swaps_=0;
    std::array<std::array<int,2>,maxN/2> swapPairs_{};
    std::array<Complex,maxN> stage_{},stageInv_{};
#endif
    std::array<int,maxN> rev_{};
    std::array<Complex,maxN/2> twiddle_{};
};
} // namespace armix::builtin_fx::anti_noiz
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
#undef ARSONMIX_ANTI_NOIZ_PACKED_SSE2
#endif
