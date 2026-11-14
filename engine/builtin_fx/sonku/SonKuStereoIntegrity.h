// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// P1.1 experimental native phase guard. A coherent bass can mask a
// phase-inverted treble instrument in a full-band stereo correlation score.
// Independent SonKu DSP: NOT proprietary Clarity/BRIR or externalization.
namespace armix::sonku {
class SonKuStereoIntegrity final {
public:
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<40000.||rate>48000.)return false;
        evidenceAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.020)));
        blendAlpha_=static_cast<float>(1.-std::exp(-1./(rate*.015)));
        constexpr double twoPi=6.28318530717958647692;
        lowAlpha_=static_cast<float>(1.-std::exp(-twoPi*260./rate));
        highAlpha_=static_cast<float>(1.-std::exp(-twoPi*2600./rate));
        reset();
        return true;
    }
    void reset() noexcept {
        energyL_=energyR_=cross_=amount_=coherentAmount_=0.f;
        antiCorrelated_=positivelyCorrelated_=false;
        low_.fill(0.f);high_.fill(0.f);
        for(auto& b:bands_)b=Band{};
    }
    float preservedSide(float x0,float x1) noexcept {
        const float l=std::clamp(std::isfinite(x0)?x0:0.f,-4.f,4.f);
        const float r=std::clamp(std::isfinite(x1)?x1:0.f,-4.f,4.f);
        energyL_+=evidenceAlpha_*(l*l-energyL_);
        energyR_+=evidenceAlpha_*(r*r-energyR_);
        cross_+=evidenceAlpha_*(l*r-cross_);
        const float power=.5f*(energyL_+energyR_);
        if(power<1.e-8f)antiCorrelated_=false;
        else if(antiCorrelated_) {
            if(cross_>-.55f*power)antiCorrelated_=false;
        }else if(cross_<-.78f*power)antiCorrelated_=true;
        if(power<1.e-8f)positivelyCorrelated_=false;
        else if(positivelyCorrelated_) {
            if(cross_<.55f*power)positivelyCorrelated_=false;
        }else if(cross_>.80f*power)positivelyCorrelated_=true;
        amount_+=blendAlpha_*((antiCorrelated_?1.f:0.f)-amount_);
        coherentAmount_+=blendAlpha_*((positivelyCorrelated_?1.f:0.f)-coherentAmount_);
        if(std::abs(amount_)<1.e-6f)amount_=0.f;
        if(std::abs(coherentAmount_)<1.e-6f)coherentAmount_=0.f;
        // Exact complementary three-band decomposition; one-pole
        // coefficients are prepared, not calculated on the audio thread.
        low_[0]+=lowAlpha_*(l-low_[0]);
        low_[1]+=lowAlpha_*(r-low_[1]);
        high_[0]+=highAlpha_*(l-high_[0]);
        high_[1]+=highAlpha_*(r-high_[1]);
        const std::array<float,3> bL{low_[0],high_[0]-low_[0],l-high_[0]};
        const std::array<float,3> bR{low_[1],high_[1]-low_[1],r-high_[1]};
        float selected=0.f;
        for(int i=0;i<3;++i) {
            auto& b=bands_[i];
            const float a=bL[i],c=bR[i];
            b.el+=evidenceAlpha_*(a*a-b.el);
            b.er+=evidenceAlpha_*(c*c-b.er);
            b.cross+=evidenceAlpha_*(a*c-b.cross);
            const float p=.5f*(b.el+b.er);
            if(p<1.e-8f)b.active=false;
            else if(b.active) {
                if(b.cross>-.55f*p)b.active=false;
            }else if(b.cross<-.78f*p)b.active=true;
            b.amount+=blendAlpha_*((b.active?1.f:0.f)-b.amount);
            if(std::abs(b.amount)<1.e-6f)b.amount=0.f;
            selected+=.5f*(a-c)*b.amount;
        }
        // Global phase guard has priority; never duplicate global rescue.
        // The core keeps the existing bounded 0.65 * mix * trim return.
        // The analysis filters retain history for smooth re-entry, but a
        // digital-silence input must never emit a synthetic filter tail.
        // Preserve P0's exact-zero digital silence contract.
        if(l==0.f && r==0.f)return 0.f;
        return .5f*(l-r)*amount_+(1.f-amount_)*selected;
    }
    float recoveryAmount()const noexcept{return amount_;}
    float coherentAmount()const noexcept{return coherentAmount_;}
private:
    struct Band {float el=0.f,er=0.f,cross=0.f,amount=0.f;bool active=false;};
    float evidenceAlpha_=0.f,blendAlpha_=0.f,lowAlpha_=0.f,highAlpha_=0.f;
    float energyL_=0.f,energyR_=0.f,cross_=0.f,amount_=0.f;
    float coherentAmount_=0.f;
    std::array<float,2> low_{},high_{};
    std::array<Band,3> bands_{};
    bool antiCorrelated_=false,positivelyCorrelated_=false;
};
} // namespace armix::sonku
