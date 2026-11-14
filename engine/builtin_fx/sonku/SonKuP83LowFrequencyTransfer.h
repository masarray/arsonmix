// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Experimental, explicitly NON-PHYSICAL LF music monitoring compromise.
// True KEMAR stereo speaker transfer remains the P8.3 physical default.
// Symmetric real loudspeakers cancel opposite-polarity low-frequency pressure
// at a centered listener; output gain cannot repair this acoustic null.
// A 480-Hz complementary crossover conditions the DIRECT two-source
// to two-ear matrix. Both speakers feed BOTH ears, with gains that follow
// source directions relative to head orientation. It is NOT a direct
// headphone-only SIDE return; but the low-frequency ILD is not measured BRIR.
// Above crossover the original measured HRTF and all room DSP remain.
// No detector, heap, IO, mutex, lookahead or new PCM routing authority.
namespace armix::sonku::p83 {
class LowFrequencySpeakerTransfer final {
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate) || rate<44000. || rate>48100.)return false;
        rate_=rate;
        return setCutoff(cutoff_);
    }
    // Called ONLY on prepared, quiescent research cores, not audio callback.
    bool setCutoff(float hz) noexcept {
        if(!std::isfinite(hz)||hz<100.f||hz>750.f||rate_<=0.)return false;
        cutoff_=hz;
        alpha_=static_cast<float>(1.-std::exp(
            -2.*3.14159265358979323846*hz/rate_));
        reset();
        return true;
    }
    float cutoffHz() const noexcept {return cutoff_;}
    void reset() noexcept {
        lowSource_.fill(0.f);
        lowHrtf_.fill(0.f);
    }
    // Coefficients only recalc at 16-frame HRTF target updates.
    void updateDirections(float leftHead,float rightHead) noexcept {
        updateSource(0,leftHead);
        updateSource(1,rightHead);
    }
    void process(float xL,float xR,float& earL,float& earR) noexcept {
        lowSource_[0]+=alpha_*(xL-lowSource_[0]);
        lowSource_[1]+=alpha_*(xR-lowSource_[1]);
        lowHrtf_[0]+=alpha_*(earL-lowHrtf_[0]);
        lowHrtf_[1]+=alpha_*(earR-lowHrtf_[1]);
        // Existing SonKu scene direct gain = 0.5: restore LF pressure
        // through the existing ear path, NOT by another additive dry bus.
        const float lowL=2.f*(weights_[0][0]*lowSource_[0]+
                              weights_[1][0]*lowSource_[1]);
        const float lowR=2.f*(weights_[0][1]*lowSource_[0]+
                              weights_[1][1]*lowSource_[1]);
        earL=earL-lowHrtf_[0]+lowL;
        earR=earR-lowHrtf_[1]+lowR;
    }
private:
    void updateSource(int index,float headAngle) noexcept {
        constexpr float radians=.017453292519943295769f;
        // 30-degree speaker: 0.92 near, 0.08 far. Center: 0.5/0.5.
        // Intentional directional LF compromise, not physical shadowing.
        const float lean=std::clamp(1.68f*std::sin(headAngle*radians),
                                   -.84f,.84f);
        weights_[index][0]=.5f*(1.f-lean);
        weights_[index][1]=.5f*(1.f+lean);
    }
    float alpha_=0.f,cutoff_=480.f;
    double rate_=0.;
    std::array<float,2> lowSource_{},lowHrtf_{};
    std::array<std::array<float,2>,2> weights_{{{{.92f,.08f}},
                                               {{.08f,.92f}}}};
};
} // namespace armix::sonku::p83
