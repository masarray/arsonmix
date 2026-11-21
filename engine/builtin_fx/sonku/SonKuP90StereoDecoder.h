// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// SonKu P9 opt-in: complex-coherent two-speaker transfer represented as
// two real FIRs per input. No separate audio engine, HRTF bypass or dry SIDE.
// Measured HRIR kernels contain the complex magnitude/phase relationships;
// the same real coefficient correction to both emitters alters only MID,
// while the full measured L-R SIDE transfer is algebraically invariant.
//
// For input L=M+S, R=M-S, output at either ear is
// [H_L+H_R + 2*c*(H_center-(H_L+H_R)/2)]*M +
// [H_L-H_R]*S. At c=1, coherent mono is precisely 2*H_center*M;
// anti-phase retains full measured (H_L-H_R)*S at ALL c.
// No frequency-domain phase reconstruction or additional convolution.
namespace armix::sonku::p90 {
class StereoDecoder final {
public:
    bool prepare(double rate) noexcept {
        if (!std::isfinite(rate) || rate < 44000. || rate > 48100.) return false;
        evidenceAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.080)));
        smoothAlpha_=static_cast<float>(1.-std::exp(-16./(rate*.120)));
        reset();
        return true;
    }
    // Never opt in the original SonKu / alpha.26 decoder implicitly.
    void enablePanPreservation(bool enabled) noexcept {
        panPreserving_=enabled;
        evidencePrimed_=false;
    }
    // Stage-front candidate: restore center for a real mixed stereo
    // master without making a highly panned instrument the vocalist.
    // Exclusive with older too-strict pan preservation.
    void enableConcertFrontBalance(bool enabled) noexcept {
        concertFrontBalance_=enabled;
        if(enabled)panPreserving_=false;
        evidencePrimed_=false;
    }
    void reset() noexcept {
        energyLeft_=energyRight_=cross_=0.f;
        centerBlend_=1.f; // legacy cold-start coherent mono
        evidencePrimed_=false;
    }
    void observe(float left,float right) noexcept {
        const float l=std::clamp(left,-4.f,4.f);
        const float r=std::clamp(right,-4.f,4.f);
        energyLeft_+=evidenceAlpha_*(l*l-energyLeft_);
        energyRight_+=evidenceAlpha_*(r*r-energyRight_);
        cross_+=evidenceAlpha_*(l*r-cross_);
    }
    float centerBlend() noexcept {
        // 1 sqrt per 16-sample FIR retarget, not each PCM sample.
        if(energyLeft_>1.e-8f || energyRight_>1.e-8f) {
            const float den=std::sqrt(std::max(energyLeft_*energyRight_,1.e-14f));
            const float rho=std::clamp(cross_/den,-1.f,1.f);
            float target=std::clamp((rho-.25f)/.70f,0.f,1.f);
            if(concertFrontBalance_) {
                // Ratio-free broadband coherence alone collapses the stage;
                // the v1 purity gate on the other hand rejected the singer
                // whenever a side guitar was mixed with the center.
                // Soft center evidence from BOTH inter-channel difference
                // energy and left/right power asymmetry; the ONE measured
                // source-to-ear HRIR matrix still renders all MID and SIDE.
                const float total=energyLeft_+energyRight_;
                const float diff=std::max(0.f,total-2.f*cross_);
                const float sideShare=diff/(2.f*total+1.e-10f);
                const float panBias=std::abs(energyLeft_-energyRight_)/
                                    (total+1.e-10f);
                const float stableCenter=std::clamp(
                    (.22f-sideShare)/.20f,0.f,1.f)*
                    std::clamp((.75f-panBias)/.50f,0.f,1.f);
                target*=stableCenter;
                if(!evidencePrimed_) {
                    centerBlend_=target;
                    evidencePrimed_=true;
                }
            }else if(panPreserving_) {
                // A correlated panned source (L=x,R=0.25x) is NOT
                // a centered vocalist. Require low difference energy
                // before projecting music MID into center HRTF.
                // E[(L-R)^2]/(2*(E[L^2]+E[R^2])) is 0 for true mono.
                const float total=energyLeft_+energyRight_;
                const float diff=std::max(0.f,total-2.f*cross_);
                const float sideShare=diff/(2.f*total+1.e-10f);
                const float centerPurity=std::clamp(
                    (.055f-sideShare)/.05f,0.f,1.f);
                target*=centerPurity;
                if(!evidencePrimed_) {
                    centerBlend_=target; // first audible transient
                    evidencePrimed_=true;
                }
            }
            centerBlend_+=smoothAlpha_*(target-centerBlend_);
        }
        return std::clamp(centerBlend_,0.f,1.f);
    }
    template<std::size_t N>
    void compose(const std::array<std::array<float,N>,2>& center,
                 std::array<std::array<float,N>,2>& left,
                 std::array<std::array<float,N>,2>& right) noexcept {
        const float c=centerBlend();
        for(std::size_t ear=0;ear<2;++ear)
            for(std::size_t k=0;k<N;++k) {
                const float mid=.5f*(left[ear][k]+right[ear][k]);
                const float correction=c*(center[ear][k]-mid);
                left[ear][k]+=correction;
                right[ear][k]+=correction;
            }
    }
private:
    float evidenceAlpha_=0.f,smoothAlpha_=0.f;
    float energyLeft_=0.f,energyRight_=0.f,cross_=0.f,centerBlend_=1.f;
    bool panPreserving_=false,evidencePrimed_=false;
    bool concertFrontBalance_=false;
};
} // namespace armix::sonku::p90
