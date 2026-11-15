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
        const double pi=3.1415926535897932384626433832795;
        (void)pi;
        evidenceAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.080)));
        smoothAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.120)));
        reset();
        return true;
    }
    void reset() noexcept {
        energyLeft_=energyRight_=cross_=0.f;
        centerBlend_=1.f; // safe mono on first transient / cold prepare
    }
    void observe(float left,float right) noexcept {
        const float l=std::clamp(left,-4.f,4.f);
        const float r=std::clamp(right,-4.f,4.f);
        energyLeft_+=evidenceAlpha_*(l*l-energyLeft_);
        energyRight_+=evidenceAlpha_*(r*r-energyRight_);
        cross_+=evidenceAlpha_*(l*r-cross_);
        // Silence does not chase arbitrary coherence or inject PCM.
        if(energyLeft_>1.e-8f || energyRight_>1.e-8f) {
            const float den=std::sqrt(std::max(energyLeft_*energyRight_,1.e-14f));
            const float rho=std::clamp(cross_/den,-1.f,1.f);
            const float target=std::clamp((rho-.25f)/.70f,0.f,1.f);
            centerBlend_+=smoothAlpha_*(target-centerBlend_);
        }
    }
    float centerBlend() const noexcept {
        return std::clamp(centerBlend_,0.f,1.f);
    }
    template<std::size_t N>
    void compose(const std::array<std::array<float,N>,2>& center,
                 std::array<std::array<float,N>,2>& left,
                 std::array<std::array<float,N>,2>& right) const noexcept {
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
};
} // namespace armix::sonku::p90
