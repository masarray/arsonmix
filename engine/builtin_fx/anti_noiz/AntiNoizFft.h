// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <type_traits>
#if defined(__SSE2__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace armix::builtin_fx::anti_noiz {
// Fixed-capacity transform plan; all twiddle design and backend qualification
// occur in prepare, never in a live callback.
class FftPlan {
public:
    static constexpr int maxN=512;
    using Complex=std::complex<float>;
    using Buffer=std::array<Complex,maxN>;
    void prepare(int n) noexcept {
        n_=n;
        const int hop=n_/2;
        for(int i=0;i<n_;++i) {
            unsigned x=static_cast<unsigned>(i),rev=0;
            for(int v=n_;v>1;v>>=1){rev=(rev<<1)|(x&1U);x>>=1;}
            rev_[i]=static_cast<int>(rev);
        }
        for(int i=0;i<hop;++i) {
            const float a=-2.f*std::numbers::pi_v<float>*float(i)/float(n_);
            twiddle_[i]={std::cos(a),std::sin(a)};
        }
#if defined(__SSE2__) || defined(_M_X64)
        // Contiguous per-stage twiddles; store inverse explicitly, avoiding
        // bit-level sign tricks on floating-point values in the inner loop.
        for(int len=4;len<=n_;len*=2){const int half=len/2,step=n_/len;
            for(int j=0;j<half;++j){stage_[half+j]=twiddle_[j*step];inverseStage_[half+j]=std::conj(twiddle_[j*step]);}}
        useSimd_=qualifySimd();
#endif
    }
    bool accelerated() const noexcept {
#if defined(__SSE2__) || defined(_M_X64)
        return useSimd_;
#else
        return false;
#endif
    }
    void transform(Buffer& a,bool inverse) noexcept {
#if defined(__SSE2__) || defined(_M_X64)
        if(useSimd_){runSimd(a,inverse);return;}
#endif
        runScalar(a,inverse);
    }
private:
    void permute(Buffer& a) const noexcept {
        for(int i=0;i<n_;++i)if(i<rev_[i])std::swap(a[i],a[rev_[i]]);
    }
    void runScalar(Buffer& a,bool inverse) const noexcept {
        permute(a);
        for(int len=2;len<=n_;len*=2) {
            const int half=len/2, step=n_/len;
            for(int base=0;base<n_;base+=len)for(int j=0;j<half;++j) {
                const auto w=inverse?std::conj(twiddle_[j*step]):twiddle_[j*step];
                const auto u=a[base+j],v=a[base+j+half]*w;
                a[base+j]=u+v;a[base+j+half]=u-v;
            }
        }
        if(inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
    }
#if defined(__SSE2__) || defined(_M_X64)
    static_assert(sizeof(Complex)==2*sizeof(float)&&std::is_trivially_copyable_v<Complex>);
    static __m128 loadPair(const Complex* p) noexcept {
        __m128 v;std::memcpy(&v,p,sizeof(v));return v;
    }
    static void storePair(Complex* p,__m128 v) noexcept {
        std::memcpy(static_cast<void*>(p),&v,sizeof(v));
    }
    // GCC 15 normal FP contraction produced incorrect output in CI even
    // though strict-FP parity passed. Scope contraction off to the vector
    // kernel and keep the qualified kernel identical at every call site.
#if defined(__GNUC__) && !defined(__clang__)
    __attribute__((noinline,optimize("fp-contract=off")))
#elif defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__clang__)
    __attribute__((noinline))
#endif
    void runSimd(Buffer& a,bool inverse) const noexcept {
        permute(a);
        // Preserve the scalar first stage, including original signed-zero
        // arithmetic. All remaining stages have an even butterfly count.
        const auto w0=inverse?std::conj(twiddle_[0]):twiddle_[0];
        for(int base=0;base<n_;base+=2){const auto u=a[base],v=a[base+1]*w0;a[base]=u+v;a[base+1]=u-v;}
        const auto& stages=inverse?inverseStage_:stage_;
        const __m128 realLanes=_mm_castsi128_ps(_mm_setr_epi32(-1,0,-1,0));
        for(int len=4;len<=n_;len*=2){const int half=len/2;
            for(int base=0;base<n_;base+=len)for(int j=0;j<half;j+=2){
                auto* u=a.data()+base+j;auto* x=u+half;
                const __m128 w=loadPair(stages.data()+half+j),U=loadPair(u),X=loadPair(x);
                const __m128 wr=_mm_shuffle_ps(w,w,_MM_SHUFFLE(2,2,0,0)),wi=_mm_shuffle_ps(w,w,_MM_SHUFFLE(3,3,1,1));
                const __m128 direct=_mm_mul_ps(X,wr),cross=_mm_mul_ps(_mm_shuffle_ps(X,X,_MM_SHUFFLE(2,3,0,1)),wi);
                const __m128 V=_mm_or_ps(_mm_and_ps(realLanes,_mm_sub_ps(direct,cross)),_mm_andnot_ps(realLanes,_mm_add_ps(direct,cross)));
                storePair(u,_mm_add_ps(U,V));storePair(x,_mm_sub_ps(U,V));
            }
        }
        if(inverse)for(int i=0;i<n_;++i)a[i]/=float(n_);
    }
    bool qualifySimd() const noexcept {
        // Bounded deterministic forward/inverse smoke test on the actual
        // kernel; a failed check falls back to the proven scalar path.
        for(int pattern=0;pattern<2;++pattern){
            Buffer a{},b{};std::uint32_t seed=0x7134a2c5u;
            for(int i=0;i<n_;++i){
                auto sample=[&](){seed=1664525u*seed+1013904223u;return float(int(seed>>16)-32768)/1024.f;};
                a[i]=pattern==0?Complex{i==7?1.f:0.f,0.f}:Complex{sample(),sample()};
            }
            b=a;
            for(bool inverse:{false,true}){
                runScalar(a,inverse);runSimd(b,inverse);
                for(int i=0;i<n_;++i){const float error=std::abs(a[i]-b[i]);
                    if(!std::isfinite(error)||error>2e-6f*std::max(1.f,std::abs(a[i])))return false;}
            }
        }
        return true;
    }
    bool useSimd_=false;
    std::array<Complex,maxN> stage_{},inverseStage_{};
#endif
    int n_=256;
    std::array<int,maxN> rev_{};
    std::array<Complex,maxN/2> twiddle_{};
};
} // namespace armix::builtin_fx::anti_noiz
