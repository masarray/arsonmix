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
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
        for(int half=1;half<n_;half*=2)for(int j=0;j<half;++j){
            const auto w=twiddle_[j*(n_/(2*half))];
            const int k=2*(half+j);
            stage_[k]=stageInv_[k]=w.real();
            stage_[k+1]=w.imag();stageInv_[k+1]=-w.imag();
        }
#endif
    }
    void transform(std::array<Complex,maxN>& a,bool inverse) noexcept {
        for(int i=0;i<n_;++i)if(i<rev_[i])std::swap(a[i],a[rev_[i]]);
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
    // Only the packed manual-stereo path uses this finite-input kernel.
    // Input PCM is sanitized and bounded to +/-64, with bounded real masks;
    // no fast-math, reciprocal approximation, new buffering or CPU dispatch.
    // Legacy automatic/mono arithmetic remains on transform().
    template<bool Inverse> void transformPacked(std::array<Complex,maxN>& a) noexcept {
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
        const auto& stage=Inverse?stageInv_:stage_;
        // Use actual float-array objects for SIMD access, never pointers
        // derived from individual complex subobjects. GCC Release exposed a
        // large DC error in the old direct complex aliasing path.
        // Fold bit reversal into the copy; scratch is fixed-capacity and
        // owned by this single DSP plan, not an audio delay or FIFO.
        for(int i=0;i<n_;++i){
            const auto z=a[rev_[i]];
            scratch_[2*i]=z.real();scratch_[2*i+1]=z.imag();
        }
        for(int base=0;base<2*n_;base+=4){
            const float ur=scratch_[base],ui=scratch_[base+1];
            const float xr=scratch_[base+2],xi=scratch_[base+3];
            const float wr=stage[2],wi=stage[3];
            const float vr=xr*wr-xi*wi,vi=xr*wi+xi*wr;
            scratch_[base]=ur+vr;scratch_[base+1]=ui+vi;
            scratch_[base+2]=ur-vr;scratch_[base+3]=ui-vi;
        }
        const auto signs=_mm_setr_ps(-0.f,0.f,-0.f,0.f);
        for(int half=2;half<n_;half*=2)for(int base=0;base<n_;base+=half*2)
            for(int j=0;j<half;j+=2){
                auto* lo=scratch_.data()+2*(base+j);
                auto* hi=scratch_.data()+2*(base+j+half);
                const auto u=_mm_loadu_ps(lo),x=_mm_loadu_ps(hi);
                const auto w=_mm_loadu_ps(stage.data()+2*(half+j));
                const auto wr=_mm_shuffle_ps(w,w,_MM_SHUFFLE(2,2,0,0));
                const auto wi=_mm_shuffle_ps(w,w,_MM_SHUFFLE(3,3,1,1));
                const auto xs=_mm_shuffle_ps(x,x,_MM_SHUFFLE(2,3,0,1));
                const auto v=_mm_add_ps(_mm_mul_ps(x,wr),_mm_xor_ps(_mm_mul_ps(xs,wi),signs));
                _mm_storeu_ps(lo,_mm_add_ps(u,v));_mm_storeu_ps(hi,_mm_sub_ps(u,v));
            }
        for(int i=0;i<n_;++i){
            const Complex z{scratch_[2*i],scratch_[2*i+1]};
            if constexpr(Inverse)a[i]=z/float(n_);else a[i]=z;
        }
#else
        transform(a,Inverse);
#endif
    }
private:
    int n_=256;
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
    alignas(16) std::array<float,2*maxN> stage_{},stageInv_{},scratch_{};
#endif
    std::array<int,maxN> rev_{};
    std::array<Complex,maxN/2> twiddle_{};
};
} // namespace armix::builtin_fx::anti_noiz
#if defined(ARSONMIX_ANTI_NOIZ_PACKED_SSE2)
#undef ARSONMIX_ANTI_NOIZ_PACKED_SSE2
#endif
