// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace armix::harmony {
// Backing-only, stereo-linked sample headroom. Current input is already
// 24ms ahead of the delayed lead: use it to begin gentle gain reduction
// BEFORE a loud phrase reaches the output. No extra lookahead or delay.
// The preview is a conservative heuristic, not the actual wet peak;
// the signed per-ear constraint is the final sample-peak safety contract.
// Explicit dry gain above the ceiling is preserved (host owns final gain).
class BackingHeadroom final {
public:
    static constexpr float ceiling=.98f;
    void prepare(double rate) noexcept {
        attack_=static_cast<float>(1.-std::exp(-1./(rate*.003)));
        release_=static_cast<float>(1.-std::exp(-1./(rate*.100)));
        decay_=static_cast<float>(std::exp(-1./(rate*.080)));
        peak_=0.f;gain_=1.f;limited_=0;emergency_=0;minimum_=1.f;
    }
    float process(float inputL,float inputR,float dryL,float dryR,
                  float wetL,float wetR,float outputGain,float wet) noexcept {
        peak_=std::max(std::max(std::abs(inputL),std::abs(inputR)),peak_*decay_);
        const float predictedLead=peak_*outputGain;
        // Preserve the ordinary low-level sound. 2.5 is the maximum
        // approved backing compensation, not a certified renderer gain bound.
        const float predictedBacking=predictedLead*2.5f*wet;
        const float target=predictedBacking>1.e-8f?
            std::clamp((ceiling-predictedLead)/predictedBacking,0.f,1.f):1.f;
        gain_+=(target-gain_)*(target<gain_?attack_:release_);
        if(target==1.f&&gain_>.999999f)gain_=1.f;
        const auto bound=[](float lead,float backing) noexcept {
            const float cap=std::max(ceiling,std::abs(lead));
            if(backing>0.f&&lead+backing>cap)return (cap-lead)/backing;
            if(backing<0.f&&lead+backing< -cap)return (-cap-lead)/backing;
            return 1.f;
        };
        const float safe=std::clamp(std::min(bound(dryL,wetL),bound(dryR,wetR)),0.f,1.f);
        if(safe<gain_){gain_=safe;++emergency_;}
        if(gain_<.99999f){++limited_;minimum_=std::min(minimum_,gain_);}
        return gain_;
    }
    std::uint64_t limited()const noexcept{return limited_;}
    std::uint64_t emergency()const noexcept{return emergency_;}
    float minimum()const noexcept{return minimum_;}
private:
    float attack_=0.f,release_=0.f,decay_=0.f,peak_=0.f,gain_=1.f,minimum_=1.f;
    std::uint64_t limited_=0,emergency_=0;
};
} // namespace armix::harmony
