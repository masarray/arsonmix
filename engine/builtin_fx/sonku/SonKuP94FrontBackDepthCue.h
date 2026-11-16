// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// P9.4 private acoustic A/B (NOT a measured BRIR / dearVR clone).
// A front/rear source can collapse inside the head with non-individual KEMAR.
// At CONSTANT physical radius, adjust only the direct/first-reflection
// energy balance near the interaural median plane; measured HRIR spectra,
// ITD/ILD, speaker azimuth, low-frequency world transfer and tail stay intact.
// Must be validated by loudness-matched headphone FAT before release.
namespace armix::sonku::p94 {
class FrontBackDepthCue final {
public:
    struct Gains {float direct=1.f,early=1.f;};
    bool prepare(double sampleRate) noexcept {
        if(!std::isfinite(sampleRate) ||
          (std::abs(sampleRate-44100.)>.1 && std::abs(sampleRate-48000.)>.1))
            return false;
        constexpr double pi=3.1415926535897932384626433832795;
        slew_=static_cast<float>(1.-std::exp(-1./(sampleRate*.045)));
        for(std::size_t i=0;i<shape_.size();++i){
            const double c=std::cos(double(i)*pi/180.);
            shape_[i]=static_cast<float>((c*c)*(c*c));
        }
        reset();
        return true;
    }
    void reset() noexcept {target_=0.f;weight_=0.f;enabled_=false;}
    // Called at the existing every-16-sample HRTF retarget. Angle MUST be
    // center WORLD azimuth - head yaw, never the ellipse's 2D SVG position.
    void retarget(float headRelativeAzimuth,bool roomWithEarly) noexcept {
        enabled_=roomWithEarly;
        if(!enabled_ || !std::isfinite(headRelativeAzimuth)){
            target_=0.f;return;
        }
        // p83::toHead returns [-180,180], bounded wrap avoids RT fmod/cos.
        float d=std::clamp(headRelativeAzimuth,-360.f,360.f);
        if(d<0.f)d+=360.f;
        if(d>=360.f)d-=360.f;
        const int lo=std::clamp(static_cast<int>(d),0,359);
        const float frac=d-lo;
        target_=shape_[lo]*(1.f-frac)+shape_[lo+1]*frac;
    }
    Gains next() noexcept {
        weight_+=slew_*(target_-weight_);
        if(std::abs(weight_-target_)<1.e-6f)weight_=target_;
        // Room OFF / ER zero must never perturb the golden P9 PCM.
        if(!enabled_)return {};
        const float w=std::clamp(weight_,0.f,1.f);
        // At 0/180: direct -0.40 dB, early +2.14 dB. Ratio shifts by
        // ~2.54 dB; NOT a source distance or headphone gain multiplier.
        // At ±90 the output is mathematically bit-identical to P9.1/P9.2.
        return {1.f-.045f*w,1.f+.28f*w};
    }
private:
    std::array<float,361> shape_{};
    float slew_=0.f,target_=0.f,weight_=0.f;
    bool enabled_=false;
};
} // namespace armix::sonku::p94
