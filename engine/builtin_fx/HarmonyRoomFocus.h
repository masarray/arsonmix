// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
namespace armix::harmony {
// Backing-ROOM ONLY, no lead/direct FX; prepared off realtime.
class BackingRoomFocus final {
public:
    struct Stereo {float left=0.f,right=0.f;};
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        constexpr double pi=3.14159265358979323846;
        hp_=static_cast<float>(1.-std::exp(-2.*pi*170./rate));
        attack_=static_cast<float>(1.-std::exp(-1./(rate*.012)));
        release_=static_cast<float>(1.-std::exp(-1./(rate*.240)));
        lowL_=lowR_=env_=0.f;
        return hp_>0.f&&hp_<1.f&&attack_>0.f&&release_>0.f;
    }
    Stereo filterSend(float l,float r) noexcept {
        lowL_+=hp_*(l-lowL_);lowR_+=hp_*(r-lowR_);
        if(std::abs(lowL_)<1.e-12f)lowL_=0.f;
        if(std::abs(lowR_)<1.e-12f)lowR_=0.f;
        return {l-lowL_,r-lowR_};
    }
    float returnGain(float dryL,float dryR,float space) noexcept {
        const float level=std::min(4.f,std::max(std::abs(dryL),std::abs(dryR)));
        env_+=(level>env_?attack_:release_)*(level-env_);
        if(env_<1.e-10f)env_=0.f;
        return 1.f-.22f*std::clamp(space,0.f,1.f)*(env_/(env_+.10f));
    }
    float leadEnvelope()const noexcept{return env_;}
private:
    float hp_=0.f,attack_=0.f,release_=0.f,lowL_=0.f,lowR_=0.f,env_=0.f;
};
} // namespace armix::harmony
